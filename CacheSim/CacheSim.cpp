/*
Copyright (c) 2017, Insomniac Games
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

Redistributions of source code must retain the above copyright notice, this
list of conditions and the following disclaimer.

Redistributions in binary form must reproduce the above copyright notice, this
list of conditions and the following disclaimer in the documentation and/or
other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include "Precompiled.h"

#include "CacheSim.h"
#include "CacheSimInternals.h"
#include "CacheSimData.h"
#include "GenericHashTable.h"
#include "Md5.h"

#include <time.h>
#include <stdarg.h>
#include <psapi.h>
#include <intrin.h>

#include <algorithm>

extern "C"
{
#include "udis86/udis86.h"
}

#define ARRAY_SIZE(a) (sizeof(a)/sizeof(a[0]))

// By default we stomp ntdll!RtlpCallVectoredHandlers with a jump to our handler.
//
// This is dirty, so at your option you can also attempt to use a regular vectored exception handler.
// The problem with this is that there is a SRW lock internally in ntdll that protects the VEH list. 
// When taking an exception for *every instruction* on *every thread* that lock is extremely contended.
// This results in deadlocks on certain combinations of sleeps and syscalls, and only on some versions
// of Windows. We haven't exactly figured that part out, so we developed this ugliness instead.
//
// With USE_VEH_TRAMPOLINE enabled, the vectored exception handling code simply calls our routine directly
// and doesn't bother with any locking or list walking. This can break things in theory, but seems OK
// for us in practice because nothing else uses vectored exception handling. But YMMV.
#define USE_VEH_TRAMPOLINE 1

namespace CacheSim
{
  enum
  {
    kMaxCalls = 128
  };

  struct StackKey
  {
    StackKey() {}

    StackKey(const uintptr_t frames[], size_t frame_count)
    {
      md5_state_t s;
      md5_init(&s);
      md5_append(&s, (const uint8_t*) frames, int(frame_count * sizeof frames[0]));
      md5_finish(&s, m_Hash);
    }

    bool IsValid() const
    {
      return 0 != (m_Qwords[0] | m_Qwords[1]);
    }

    bool Invalidate()
    {
      m_Qwords[0] = m_Qwords[1] = 0;
    }

    union
    {
      uint8_t   m_Hash[16];
      uint32_t  m_Dwords[4];
      uint64_t  m_Qwords[2];
    };
  };


  struct ThreadState
  {
    LONG        m_Generation;
    ud_t        m_Disassembler;
    uint32_t    m_StackIndex;                 // Index of current stack in callstack data. Recomputed whenever the call stack contents changes.
    int         m_LogicalCoreIndex;           // Index of logical core, -1
  };

  static thread_local ThreadState s_ThreadState;

  static volatile LONG g_Generation = 1;
  static DWORD g_TraceEnabled = 0;
#if USE_VEH_TRAMPOLINE
  static uint8_t veh_stash[12];
#else
  static PVOID g_Handler = 0;
#endif

  static volatile LONG g_Lock;
  static CacheSim::JaguarCacheSim g_Cache;
  static HANDLE g_Heap;
  static uintptr_t g_RaiseExceptionAddress;

  static int s_CoreMappingCount = 0;
  static struct { DWORD m_ThreadId; int m_LogicalCore; } s_CoreMappings[128];

  class AutoSpinLock
  {
  public:
    AutoSpinLock()
    {
      int count = 0;
      while (InterlockedCompareExchange(&g_Lock, 1, 0) == 1)
      {
        if (count++ == 1000)
        {
          Sleep(0);
          count = 0;
        }
      }
    }

    ~AutoSpinLock()
    {
      g_Lock = 0;
    }
  };

  struct StackValue
  {
    StackValue() : m_Offset(0), m_Count(0) {}

    uint32_t  m_Offset;
    uint32_t  m_Count;
  };

  struct RipKey
  {
    RipKey() : m_Rip(0), m_StackOffset(0) {}
    RipKey(uintptr_t rip, uint32_t stack_offset) : m_Rip(rip), m_StackOffset(stack_offset) {}
    uintptr_t m_Rip;
    uint32_t  m_StackOffset;
  };

  bool operator==(const RipKey& l, const RipKey& r)
  {
    return l.m_Rip == r.m_Rip && l.m_StackOffset == r.m_StackOffset;
  }

  struct RipStats
  {
    RipStats() { memset(m_Stats, 0, sizeof m_Stats); }

    uint32_t    m_Stats[CacheSim::kAccessResultCount];
  };

  bool operator==(const StackKey& l, const StackKey& r)
  {
    return 0 == memcmp(l.m_Hash, r.m_Hash, sizeof l.m_Hash);
  }

  // Maps 128-bit hash digests to call stacks.
  static GenericHashTable<StackKey, StackValue> g_Stacks;
  // Maps RIP+Stack before that to stats
  static GenericHashTable<RipKey, RipStats> g_Stats;
  // Raw storage array for stack trace values
  static struct
  {
    uintptr_t*  m_Frames;
    uint32_t    m_Count;
    uint32_t    m_ReserveCount;
  } g_StackData;

  RipStats* GetRipNode(uintptr_t pc, uint32_t stack_offset)
  {
    return g_Stats.Insert(RipKey(pc, stack_offset));
  }

  uint32_t InsertStack(const uintptr_t frames[], uint32_t frame_count)
  {
    StackKey key(frames, frame_count);

    if (StackValue* existing = g_Stacks.Find(key))
    {
      return existing->m_Offset;
    }

    // Create a new stack entry.
    uint32_t offset = g_StackData.m_Count;
    if (offset + frame_count + 1 > g_StackData.m_ReserveCount)
    {
      uint32_t new_reserve = g_StackData.m_ReserveCount ? 2 * g_StackData.m_ReserveCount : 65536;
      if (g_StackData.m_Frames)
        g_StackData.m_Frames = (uintptr_t*)HeapReAlloc(g_Heap, 0, g_StackData.m_Frames, new_reserve * sizeof g_StackData.m_Frames[0]);
      else
        g_StackData.m_Frames = (uintptr_t*)HeapAlloc(g_Heap, 0, new_reserve * sizeof g_StackData.m_Frames[0]);

      g_StackData.m_ReserveCount = new_reserve;
    }

    memcpy(g_StackData.m_Frames + offset, frames, frame_count * sizeof frames[0]);
    g_StackData.m_Frames[offset + frame_count] = 0;
    g_StackData.m_Count += frame_count + 1;

    StackValue* val = g_Stacks.Insert(key);
    val->m_Offset = offset;
    val->m_Count = frame_count;

    return offset;
  }
}

namespace HashFunctions
{
  uint32_t Hash(const CacheSim::StackKey& key)
  {
    return key.m_Dwords[0];
  }

  uint32_t Hash(const CacheSim::RipKey& key)
  {
    return uint32_t(key.m_Rip ^ (key.m_Rip >> 32) * 33 + 61 * key.m_StackOffset);
  }
}

static intptr_t ReadReg(ud_type_t reg, const CONTEXT* ctx)
{
  switch (reg)
  {
  case  UD_R_AL:  return (int8_t) (ctx->Rax);
  case  UD_R_AH:  return (int8_t) (ctx->Rax>>8);
  case  UD_R_AX:  return (int16_t)(ctx->Rax);
  case UD_R_EAX:  return (int32_t)(ctx->Rax);
  case UD_R_RAX:  return          (ctx->Rax);

  case  UD_R_BL:  return (int8_t) (ctx->Rbx);
  case  UD_R_BH:  return (int8_t) (ctx->Rbx>>8);
  case  UD_R_BX:  return (int16_t)(ctx->Rbx);
  case UD_R_EBX:  return (int32_t)(ctx->Rbx);
  case UD_R_RBX:  return          (ctx->Rbx);

  case  UD_R_CL:  return (int8_t) (ctx->Rcx);
  case  UD_R_CH:  return (int8_t) (ctx->Rcx>>8);
  case  UD_R_CX:  return (int16_t)(ctx->Rcx);
  case UD_R_ECX:  return (int32_t)(ctx->Rcx);
  case UD_R_RCX:  return          (ctx->Rcx);

  case  UD_R_DL:  return (int8_t) (ctx->Rdx);
  case  UD_R_DH:  return (int8_t) (ctx->Rdx>>8);
  case  UD_R_DX:  return (int16_t)(ctx->Rdx);
  case UD_R_EDX:  return (int32_t)(ctx->Rdx);
  case UD_R_RDX:  return          (ctx->Rdx);

  case UD_R_SIL:  return (int8_t) (ctx->Rsi);
  case UD_R_SI:   return (int16_t)(ctx->Rsi);
  case UD_R_ESI:  return (int32_t)(ctx->Rsi);
  case UD_R_RSI:  return          (ctx->Rsi);

  case UD_R_DIL:  return (int8_t) (ctx->Rdi);
  case UD_R_DI:   return (int16_t)(ctx->Rdi);
  case UD_R_EDI:  return (int32_t)(ctx->Rdi);
  case UD_R_RDI:  return          (ctx->Rdi);

  case UD_R_BPL:  return (int8_t) (ctx->Rbp);
  case UD_R_BP:   return (int16_t)(ctx->Rbp);
  case UD_R_EBP:  return (int32_t)(ctx->Rbp);
  case UD_R_RBP:  return          (ctx->Rbp);

  case UD_R_SPL:  return (int8_t) (ctx->Rsp);
  case UD_R_SP:   return (int16_t)(ctx->Rsp);
  case UD_R_ESP:  return (int32_t)(ctx->Rsp);
  case UD_R_RSP:  return          (ctx->Rsp);

  case UD_R_R8B:  return (int8_t) (ctx->R8 );
  case UD_R_R8W:  return (int16_t)(ctx->R8 );
  case UD_R_R8D:  return (int32_t)(ctx->R8 );
  case UD_R_R8:   return          (ctx->R8 );

  case UD_R_R9B:  return (int8_t) (ctx->R9 );
  case UD_R_R9W:  return (int16_t)(ctx->R9 );
  case UD_R_R9D:  return (int32_t)(ctx->R9 );
  case UD_R_R9:   return          (ctx->R9 );

  case UD_R_R10B: return (int8_t) (ctx->R10);
  case UD_R_R10W: return (int16_t)(ctx->R10);
  case UD_R_R10D: return (int32_t)(ctx->R10);
  case UD_R_R10:  return          (ctx->R10);

  case UD_R_R11B: return (int8_t) (ctx->R11);
  case UD_R_R11W: return (int16_t)(ctx->R11);
  case UD_R_R11D: return (int32_t)(ctx->R11);
  case UD_R_R11:  return          (ctx->R11);

  case UD_R_R12B: return (int8_t) (ctx->R12);
  case UD_R_R12W: return (int16_t)(ctx->R12);
  case UD_R_R12D: return (int32_t)(ctx->R12);
  case UD_R_R12:  return          (ctx->R12);

  case UD_R_R13B: return (int8_t) (ctx->R13);
  case UD_R_R13W: return (int16_t)(ctx->R13);
  case UD_R_R13D: return (int32_t)(ctx->R13);
  case UD_R_R13:  return          (ctx->R13);

  case UD_R_R14B: return (int8_t) (ctx->R14);
  case UD_R_R14W: return (int16_t)(ctx->R14);
  case UD_R_R14D: return (int32_t)(ctx->R14);
  case UD_R_R14:  return          (ctx->R14);

  case UD_R_R15B: return (int8_t) (ctx->R15);
  case UD_R_R15W: return (int16_t)(ctx->R15);
  case UD_R_R15D: return (int32_t)(ctx->R15);
  case UD_R_R15:  return          (ctx->R15);


  case UD_R_RIP:  return          (ctx->Rip);
  }

  DebugBreak();
  return 0;
}

static uintptr_t ComputeEa(const ud_operand_t& op, const CONTEXT* ctx)
{
  uintptr_t addr = 0;

  switch (op.offset)
  {
  case 8:  addr += op.lval.sbyte; break;
  case 16: addr += op.lval.sword; break;
  case 32: addr += op.lval.sdword; break;
  case 64: addr += op.lval.sqword; break;
  }

  if (op.base != UD_NONE)
  {
    addr += ReadReg(op.base, ctx);
  }

  if (op.index != UD_NONE)
  {
    intptr_t regval = ReadReg(op.index, ctx);
    if (UD_NONE != op.scale)
      addr += regval * op.scale;
    else
      addr += regval;
  }

  return addr;
}

static void InvalidateStack()
{
  using namespace CacheSim;
  s_ThreadState.m_StackIndex = ~0u;
}

static void GenerateMemoryAccesses(int core_index, const ud_t* ud, DWORD64 rip, int ilen, const CONTEXT* ctx)
{
  using namespace CacheSim;
  int read_count = 0;
  int write_count = 0;

  struct MemOp { uintptr_t ea; size_t sz; };
  MemOp prefetch_op = { 0, 0 };
  MemOp reads[4];
  MemOp writes[4];

  auto data_r = [&](uintptr_t addr, size_t sz) -> void
  {
    if (sz == 0)
      DebugBreak();
    if (intptr_t(addr) < 0)
      DebugBreak();
    reads[read_count].ea = addr;
    reads[read_count].sz = sz;
    ++read_count;
  };

  auto data_w = [&](uintptr_t addr, size_t sz) -> void
  {
    if (sz == 0)
      DebugBreak();
    if (intptr_t(addr) < 0)
      DebugBreak();
    writes[write_count].ea = addr;
    writes[write_count].sz = sz;
    ++write_count;
  };

  //const int dir = ctx->EFlags & (1 << 10) ? -1 : 1;

  uint32_t existing_stack_index = s_ThreadState.m_StackIndex;

  // Handle instructions with implicit memory operands.
  switch (ud->mnemonic)
  {
    // String instructions.
  case UD_Ilodsb:
  case UD_Iscasb:
    data_r(ctx->Rsi, 1);
    break; 
  case UD_Ilodsw:
  case UD_Iscasw:
    data_r(ctx->Rsi, 2);
    break;
  case UD_Ilodsd:
  case UD_Iscasd:
    data_r(ctx->Rsi, 4);
    break;
  case UD_Ilodsq:
  case UD_Iscasq:
    data_r(ctx->Rsi, 8);
    break;
  case UD_Istosb:
    data_w(ctx->Rdi, 1);
    break; 
  case UD_Istosw:
    data_w(ctx->Rdi, 2);
    break; 
  case UD_Istosd:
    data_w(ctx->Rdi, 4);
    break; 
  case UD_Istosq:
    data_w(ctx->Rdi, 8);
    break; 
  case UD_Imovsb:
    data_r(ctx->Rsi, 1);
    data_w(ctx->Rdi, 1);
    break; 
  case UD_Imovsw:
    data_r(ctx->Rsi, 2);
    data_w(ctx->Rdi, 2);
    break; 
  case UD_Imovsd:
    data_r(ctx->Rsi, 4);
    data_w(ctx->Rdi, 4);
    break; 
  case UD_Imovsq:
    data_r(ctx->Rsi, 8);
    data_w(ctx->Rdi, 8);
    break; 

    // Stack operations.
  case UD_Ipush:    data_w(ctx->Rsp, ud->operand[0].size/8); break;
  case UD_Ipop:     data_w(ctx->Rsp, ud->operand[0].size/8); break;
  case UD_Icall:    data_w(ctx->Rsp, 8); InvalidateStack(); break;
  case UD_Iret:     data_r(ctx->Rsp, 8); InvalidateStack(); break;
  }
  
  // Handle special memory ops operands
  switch (ud->mnemonic)
  {
  case UD_Ipause:
    // This helps to avoid deadlocks.
    {
      static volatile LONG do_ms_step = 0;
      LONG val = InterlockedIncrement(&do_ms_step);
      Sleep((val & 0x1fff) == 0 ? 1 : 0);
    }
    break;
  case UD_Ilea:
  case UD_Inop:
    // LEA doesn't actually access memory even though it has memory operands.
    // There also seem to be NOPs that do crazy things with memory operands.
    break;
  case UD_Iprefetch:
  case UD_Iprefetchnta:
  case UD_Iprefetcht0:
  case UD_Iprefetcht1:
  case UD_Iprefetcht2:
    prefetch_op.ea = ComputeEa(ud->operand[0], ctx);
    prefetch_op.sz = 64;
    break;

  case UD_Imovntq:
    // TODO: Handle this specially?
    data_w(ComputeEa(ud->operand[0], ctx), 8);
    break;

  case UD_Imovntdq:
  case UD_Imovntdqa:
    // TODO: Handle this specially?
    data_w(ComputeEa(ud->operand[0], ctx), 16);
    break;

  case UD_Ifxsave:
    data_w(ComputeEa(ud->operand[0], ctx), 512);
    break;

  case UD_Ifxrstor:
    data_r(ComputeEa(ud->operand[0], ctx), 512);
    break;

  default:
    for (int op = 0; op < ARRAY_SIZE(ud->operand) && ud->operand[op].type != UD_NONE; ++op)
    {
      if (UD_OP_MEM != ud->operand[op].type)
        continue;

      switch (ud->operand[op].access)
      {
      case UD_OP_ACCESS_READ:
        data_r(ComputeEa(ud->operand[op], ctx), ud->operand[op].size / 8);
        break;
      case UD_OP_ACCESS_WRITE:
        data_w(ComputeEa(ud->operand[op], ctx), ud->operand[op].size / 8);
        break;
      }
    }
  }

#if 0
  // This is a good thing to have while developing, but not in production.
  // It's not safe to call printf() here, because the thread we're tracing might be inside printf() too.
  // Hello reentrancy.
  static volatile int64_t insns = 0;
  int64_t count = AtomicInc64(&insns);
  if (0 == (count % 1'000'000))
  {
    printf("%lld million instructions traced\n", count/1'000'000);
  }
#endif

  // Commit stats for this instruction in a critical section.
  AutoSpinLock lock;

  if (!g_TraceEnabled)
    return;

  // Find stats line for the instruction pointer
  RipStats* stats = GetRipNode(rip, existing_stack_index);

  stats->m_Stats[CacheSim::kInstructionsExecuted] += 1;

  // Generate I-cache traffic.
  {
    CacheSim::AccessResult r = g_Cache.Access(core_index, rip, ilen, CacheSim::kCodeRead);
    stats->m_Stats[r] += 1;

    // Generate prefetch traffic. Pretend prefetches are immediate reads and record how effective they were.
    if (prefetch_op.ea)
    {
      switch (g_Cache.Access(core_index, prefetch_op.ea, prefetch_op.sz, CacheSim::kRead))
      {
      case CacheSim::kD1Hit:
        stats->m_Stats[CacheSim::kPrefetchHitD1] += 1;
        break;
      case CacheSim::kL2Hit:
        stats->m_Stats[CacheSim::kPrefetchHitL2] += 1;
        break;
      }
    }
  }

  // Generate D-cache traffic.
  for (int i = 0; i < read_count; ++i)
  {
    CacheSim::AccessResult r = g_Cache.Access(core_index, reads[i].ea, reads[i].sz, CacheSim::kRead);
    stats->m_Stats[r] += 1;
  }

  for (int i = 0; i < write_count; ++i)
  {
    CacheSim::AccessResult r = g_Cache.Access(core_index, writes[i].ea, writes[i].sz, CacheSim::kWrite);
    stats->m_Stats[r] += 1;
  }
}

static void empty_func()
{
}

static int Backtrace2(uintptr_t callstack[], const PCONTEXT ctx)
{
  int i = 0;
  ULONGLONG image_base;
  UNWIND_HISTORY_TABLE target_gp;

  memset(&target_gp, 0, sizeof target_gp);

  CONTEXT ctx_copy;
  ctx_copy = *ctx; // Overkill

  while (i < CacheSim::kMaxCalls)
  {
    callstack[i++] = ctx_copy.Rip;

    PRUNTIME_FUNCTION pfunc = RtlLookupFunctionEntry(ctx_copy.Rip, &image_base, &target_gp);

    if (NULL == pfunc)
    {
      // This is a leaf function.
      ctx_copy.Rip = *(uintptr_t*)ctx_copy.Rsp;
      ctx_copy.Rsp += 8;
    }
    else
    {
      PVOID handler_data;
      DWORD64 establisher_frame;

      RtlVirtualUnwind(UNW_FLAG_NHANDLER, image_base, ctx_copy.Rip, pfunc, &ctx_copy, &handler_data, &establisher_frame, nullptr);
    }

    if (!ctx_copy.Rip)
      break;
  }

  return i;
}

static int FindLogicalCoreIndex(uint32_t thread_id)
{
  using namespace CacheSim;

  int count = s_CoreMappingCount;
  
  for (int i = 0; i < count; ++i)
  {
    if (s_CoreMappings[i].m_ThreadId == thread_id)
    {
      return s_CoreMappings[i].m_LogicalCore;
    }
  }

  return -1;
}

#if !USE_VEH_TRAMPOLINE
static LONG WINAPI StepFilter(_In_ struct _EXCEPTION_POINTERS* ExcInfo)
#else
static LONG WINAPI StepFilter(PEXCEPTION_RECORD ExceptionRecord, PCONTEXT ContextRecord)
#endif
{
#if USE_VEH_TRAMPOLINE
  _EXCEPTION_POINTERS ExcInfoStorage = { ExceptionRecord, ContextRecord };
  _EXCEPTION_POINTERS* ExcInfo = &ExcInfoStorage;
#endif
  using namespace CacheSim;
  if (STATUS_SINGLE_STEP == ExcInfo->ExceptionRecord->ExceptionCode)
  {

    // Make sure the thread state is up to date.
    LONG curr_gen = g_Generation;
    ud_t* ud = &s_ThreadState.m_Disassembler;

    if (s_ThreadState.m_Generation != curr_gen)
    {
      ud_init(ud);
      ud_set_mode(ud, 64);

      s_ThreadState.m_LogicalCoreIndex = FindLogicalCoreIndex(GetCurrentThreadId());

      s_ThreadState.m_Generation = curr_gen;
      InvalidateStack();
    }


    const int core_index = s_ThreadState.m_LogicalCoreIndex;

    // Only trace threads we've mapped to cores. Ignore all others.
    if (g_TraceEnabled && core_index >= 0)
    {
      uintptr_t rip = ExcInfo->ContextRecord->Rip;

      if (rip == g_RaiseExceptionAddress)
      {
        // Patch any attempts to raise an exception so we don't crash.
        // This typically comes up trying to call OutputDebugString, which will raise an exception internally.
        rip = ExcInfo->ContextRecord->Rip = (uintptr_t) &empty_func;
        ExcInfo->ContextRecord->EFlags |= 0x100;
        return EXCEPTION_CONTINUE_EXECUTION;
      }

      if (~0u == s_ThreadState.m_StackIndex)
      {
        // Recompute call stack
        uintptr_t callstack[kMaxCalls];
        static_assert(sizeof(PVOID) == sizeof(uintptr_t), "64-bit required");
        int frame_count = Backtrace2(callstack, ExcInfo->ContextRecord);
        if (0 == frame_count || kMaxCalls == frame_count)
          DebugBreak();

        AutoSpinLock lock;

        // Take off one more frame as we're splitting in two parts, stack and current_rip
        s_ThreadState.m_StackIndex = InsertStack(callstack + 1, frame_count - 1);
      }

      ud_set_input_buffer(ud, (const uint8_t*) rip, 16);
      ud_set_pc(ud, rip);
      int ilen = ud_disassemble(ud);
      GenerateMemoryAccesses(core_index, ud, rip, ilen, ExcInfo->ContextRecord);

      // Keep trapping.
      ExcInfo->ContextRecord->EFlags |= 0x100;
    }

    return EXCEPTION_CONTINUE_EXECUTION;
  }

  return EXCEPTION_CONTINUE_SEARCH;
}

__declspec(dllexport)
void CacheSimInit()
{
  using namespace CacheSim;
  // Note that this heap *has* to be non-serialized, because we can't have it trying to take any locks.
  // Doing so will deadlock the recording. So we rely on spin locks and a non-serialized heap instead.
  g_Heap = HeapCreate(HEAP_NO_SERIALIZE, 0, 0);
  HeapAlloc(g_Heap, 0, 128);
  g_Stats.Init(g_Heap);
  g_Stacks.Init(g_Heap);
  memset(&g_StackData, 0, sizeof g_StackData);

  HMODULE h = LoadLibraryA("kernelbase.dll");
  g_RaiseExceptionAddress = (uintptr_t) GetProcAddress(h, "RaiseException");
}

__declspec(dllexport)
bool CacheSimStartCapture()
{
  using namespace CacheSim;
  if (g_TraceEnabled)
  {
    return false;
  }

  if (IsDebuggerPresent())
  {
    OutputDebugStringA("CacheSimStartCapture: Refusing to trace when the debugger is attached.\n");
    DebugBreak();
    return false;
  }

#if USE_VEH_TRAMPOLINE
  uint8_t* ntdll_base = (uint8_t*) LoadLibraryA("ntdll.dll");
#endif
  
  // Reset.
  g_Cache.Init();

  HANDLE thread_handles[ARRAY_SIZE(s_CoreMappings)];
  int thread_count = 0;

  DWORD my_thread_id = GetCurrentThreadId();
  for (int i = 0, count = s_CoreMappingCount; i < count; ++i)
  {
    const auto& mapping = s_CoreMappings[i];
    if (mapping.m_ThreadId == my_thread_id)
      continue;

    if (HANDLE h = OpenThread(THREAD_ALL_ACCESS, FALSE, mapping.m_ThreadId))
    {
      thread_handles[thread_count++] = h;
    }
  }

  // Suspend all threads that aren't this thread.
  for (int i = 0; i < thread_count; ++i)
  {
    DWORD suspend_count = SuspendThread(thread_handles[i]);
    if (int(suspend_count) < 0)
      DebugBreak(); // Failed to suspend thread
  }

  // Make reasonably sure they've all stopped.
  Sleep(1000);

  InterlockedIncrement(&g_Generation);

  g_TraceEnabled = 1;

#if !USE_VEH_TRAMPOLINE
  // Install exception filter to do the tracing.
  if (!g_Handler)
  {
    g_Handler = AddVectoredExceptionHandler(1, StepFilter);
    if (!g_Handler)
    {
      DebugBreak(); // Failed to install vectored exception handler
    }
  }
#else
  {
    // This table describe the offset inside the ntdll module where we can find the start of the symbol
    // RtlpCallVectoredHandlers. This symbol is not exported, so it's not possible to get its location
    // with a call to GetProcAddress(). If you have a version of NTDLL not in this list and need to
    // use CacheSim; proceed as follows:
    // - In the Watch, enter {,,ntdll}RtlpCallVectoredHandlers and note the address
    // - Go to the Modules debug window and note the base address of NTDLL
    // - Subtract the base from the above address, and record the resulting number. That will go in the `callveh_offset` field below.
    // - Get SizeOfImage and Checksum from the opt_header pointer below (or from dumpbin /headers ntdll.dll)
    // - Enter all three values together in a new `known_ntdlls` entry, with a comment about your version
    static const struct
    {
      DWORD size;
      DWORD checksum;
      DWORD callveh_offset;
    } known_ntdlls[] =
    {
      { 0x1a9000, 0x1a875f, 101552 },   // Win 7 SP1 v6.1 build 7601
      { 0x1ac000, 0x1a7d5d, 351820 },   // Win 8.1 RTM
      { 0x1be000, 0x1cc294,  94928 },   // Win 8.0 RTM
      { 0x1d1000, 0x1d204f, 436668 },   // Win 10 1607 build 14393.222
      { 0x1d1000, 0x1dc01c, 441340 },   // Win 10 1607 build 14393.693
    };

    // This is where the MZ...blah header lives (the DOS header)
    IMAGE_DOS_HEADER* dos_header = (IMAGE_DOS_HEADER*) ntdll_base;

    // We want the PE header.
    IMAGE_FILE_HEADER* file_header = (IMAGE_FILE_HEADER*) (ntdll_base + dos_header->e_lfanew + 4);

    // Straight after that is the optional header (which technically is optional, but in practice always there.)
    IMAGE_OPTIONAL_HEADER* opt_header = (IMAGE_OPTIONAL_HEADER*) (((char*)file_header) + sizeof(IMAGE_FILE_HEADER));

    DWORD our_off = ~0u;

    for (size_t i = 0; i < ARRAY_SIZE(known_ntdlls); ++i)
    {
      if (opt_header->CheckSum == known_ntdlls[i].checksum && opt_header->SizeOfImage == known_ntdlls[i].size)
      {
        our_off = known_ntdlls[i].callveh_offset;
        break;
      }
    }
    
    if (~0u == our_off)
    {
      // We don't know about this version of ntdll.
      // See the comment above on how to add it, or talk to Andreas.
      DebugBreak();
    }

    uint8_t* addr = ntdll_base + our_off;
    DWORD old_prot;
    if (!VirtualProtect((uint8_t*) (uintptr_t(addr) & ~4095ull),  8192, PAGE_EXECUTE_READWRITE, &old_prot))
    {
      DebugBreak();
    }

    uintptr_t target = (uintptr_t) &StepFilter;
    const uint8_t a0 = uint8_t(target >> 56);
    const uint8_t a1 = uint8_t(target >> 48);
    const uint8_t a2 = uint8_t(target >> 40);
    const uint8_t a3 = uint8_t(target >> 32);
    const uint8_t a4 = uint8_t(target >> 24);
    const uint8_t a5 = uint8_t(target >> 16);
    const uint8_t a6 = uint8_t(target >>  8);
    const uint8_t a7 = uint8_t(target >>  0);

    uint8_t replacement[] =
    {
      0x48, 0xb8, a7, a6, a5, a4, a3, a2, a1, a0,   // mov rax, addr (64-bit abs)
      0xff, 0xe0                                    // jmp rax
    };

    static_assert(sizeof replacement == sizeof veh_stash, "This needs to match in size");

    memcpy(veh_stash, addr, sizeof veh_stash);
    memcpy(addr, replacement, sizeof replacement);

    FlushInstructionCache(GetCurrentProcess(), addr, sizeof replacement);
    
    if (!VirtualProtect((uint8_t*) (uintptr_t(addr) & ~4095ull),  8192, old_prot, &old_prot))
    {
      DebugBreak();
    }
  }

#endif

  for (int i = 0; i < thread_count; ++i)
  {
    CONTEXT ctx;
    ZeroMemory(&ctx, sizeof ctx);
    ctx.ContextFlags = CONTEXT_CONTROL;

    // Enable trap flag for thread.
    if (GetThreadContext(thread_handles[i], &ctx))
    {
      ctx.EFlags |= 0x100;
      SetThreadContext(thread_handles[i], &ctx);
    }
  }

  // Resume all other threads.
  for (int i = 0; i < thread_count; ++i)
  {
    ResumeThread(thread_handles[i]);
  }

  for (int i = 0; i < thread_count; ++i)
  {
    CloseHandle(thread_handles[i]);
  }

  // Finally enable trap flag for calling thread.
  __writeeflags(__readeflags() | 0x100);

  return true;
}

__declspec(dllexport)
void CacheSimEndCapture(bool save)
{
  using namespace CacheSim;
  g_TraceEnabled = 0;

  __writeeflags(__readeflags() & ~0x100);

  // Give the thread a few instructions to run so we're definitely not tracing.
  //Thread::NanoPause();
  Sleep(0);
  Sleep(0);
  Sleep(0);

  if (!save)
    return;

  AutoSpinLock lock;

  // It's tempting to remove the handler here, like this:
  //
  //    RemoveVectoredExceptionHandler(g_Handler);
  //    g_Handler = nullptr;
  //
  // ..but that's a mistake. There could be a syscall instruction paused in the kernel that
  // will come back and signal a single step trap at some arbitrary point in the future, so
  // we need our handler to stay in effect.

  char executable_filename[512];
  const char* executable_name = "unknown";
  if (0 != GetModuleFileNameA(NULL, executable_filename, ARRAY_SIZE(executable_filename)))
  {
    if (char* p = strrchr(executable_filename, '\\'))
    {
      ++p;
      if (char* dot = strchr(p, '.'))
      {
        executable_name = p;
        *dot = '\0';
      }
    }
  }

  char fn[512];
  _snprintf_s(fn, ARRAY_SIZE(fn), "%s_%u.csim", executable_name, (uint32_t) time(nullptr));

  if (FILE* f = fopen(fn, "wb"))
  {
    auto align = [&f]()
    {
      if (int needed = (8 - (ftell(f) & 7)) & 7)
      {
        static const uint8_t padding[8] = { 0 };
        fwrite(padding, 1, needed, f);
      }
    };

    auto welem = [&f](const auto& value)
    {
      fwrite(&value, 1, sizeof value, f);
    };

    auto wdata = [&f](const void* data, size_t size)
    {
      fwrite(data, 1, size, f);
    };

    struct PatchWord
    {
      long m_Offset;
      FILE* m_File;

      explicit PatchWord(FILE* f) : m_File(f), m_Offset(ftell(f))
      {
        static const uint8_t placeholder[] = { 0xcc, 0xdd, 0xee, 0xff };
        fwrite(placeholder, 1, sizeof placeholder, f);
      }

      void Update(uint32_t value)
      {
        long pos = ftell(m_File);
        fseek(m_File, m_Offset, SEEK_SET);
        fwrite(&value, 1, sizeof value, m_File);
        fseek(m_File, pos, SEEK_SET);
      }
    };

    welem(0xcace'51afu);
    welem(0x0000'0001u);

    PatchWord module_offset { f };
    PatchWord module_count { f };

    PatchWord module_str_offset { f };

    PatchWord frame_offset { f };
    PatchWord frame_count { f };

    PatchWord stats_offset { f };
    PatchWord stats_count { f };

    welem(0u); // symbol_offset
    welem(0u); // symbol_count
    welem(0u); // symbol_text_offset

    HMODULE modules[1024];
    DWORD bytes_needed = 0;
    if (EnumProcessModules(GetCurrentProcess(), modules, sizeof modules, &bytes_needed))
    {
      align();

      size_t bytes = std::min(sizeof modules, size_t(bytes_needed));
      size_t nmods = bytes / sizeof modules[0];
      module_offset.Update(ftell(f));
      module_count.Update(static_cast<uint32_t>(nmods));
      uint32_t str_section_size = 0;
      for (size_t i = 0; i < nmods; ++i)
      {
        HMODULE mod = modules[i];
        MODULEINFO modinfo;
        if (GetModuleInformation(GetCurrentProcess(), mod, &modinfo, sizeof modinfo))
        {
          char modname[256];
          if (DWORD namelen = GetModuleFileNameExA(GetCurrentProcess(), mod, modname, sizeof modname))
          {
            size_t len = strlen(modname) + 1;
            welem(reinterpret_cast<uintptr_t>(mod));
            welem(static_cast<uint32_t>(modinfo.SizeOfImage));
            welem(static_cast<uint32_t>(str_section_size));
            str_section_size += (uint32_t) len;
          }
        }
      }

      module_str_offset.Update(ftell(f));

      for (size_t i = 0; i < nmods; ++i)
      {
        char modname[256];
        HMODULE mod = modules[i];
        if (DWORD namelen = GetModuleFileNameExA(GetCurrentProcess(), mod, modname, sizeof modname))
        {
          wdata(modname, strlen(modname) + 1);
        }
      }
    }

    align();

    // Write raw values for stack frames
    frame_offset.Update(ftell(f));
    frame_count.Update(g_StackData.m_Count);
    wdata(g_StackData.m_Frames, g_StackData.m_Count * sizeof g_StackData.m_Frames[0]);

    align();
    // Write stats
    stats_offset.Update(ftell(f));
    stats_count.Update((uint32_t) g_Stats.GetCount());
    for (const RipKey& key : g_Stats.Keys())
    {
      welem(key.m_Rip);
      welem(key.m_StackOffset);
      welem(*g_Stats.Find(key));
      welem(static_cast<uint32_t>(0));
    }

    fclose(f);
  }
  else
  {
    fprintf(stderr, "failed to open %s for writing", fn);
  }

  g_Stats.FreeAll();
  g_Stacks.FreeAll();

  HeapFree(g_Heap, 0, g_StackData.m_Frames);
  memset(&g_StackData, 0, sizeof g_StackData);
}

__declspec(dllexport)
void CacheSimRemoveHandler()
{
#if !USE_VEH_TRAMPOLINE
  if (CacheSim::g_Handler)
  {
    RemoveVectoredExceptionHandler(CacheSim::g_Handler);
    CacheSim::g_Handler = nullptr;
  }
#endif
}

__declspec(dllexport)
void CacheSimSetThreadCoreMapping(uint32_t thread_id, int logical_core_id)
{
  using namespace CacheSim;

  AutoSpinLock lock;

  int count = s_CoreMappingCount;
  
  for (int i = 0; i < count; ++i)
  {
    if (s_CoreMappings[i].m_ThreadId == thread_id)
    {
      s_CoreMappings[i].m_LogicalCore = logical_core_id;
    }
  }

  if (count == ARRAY_SIZE(s_CoreMappings))
  {
    DebugBreak(); // Increase array size
    return;
  }

  s_CoreMappings[count].m_ThreadId = thread_id;
  s_CoreMappings[logical_core_id].m_LogicalCore = logical_core_id;

  ++s_CoreMappingCount;
}
