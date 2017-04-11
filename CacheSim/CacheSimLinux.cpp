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

#include <asm/prctl.h>
#include <execinfo.h>
#include <link.h>
#include <signal.h>
#include <sys/auxv.h>
#include <sys/prctl.h>
#include <sys/ptrace.h>
#include <sys/syscall.h>
#include <sys/user.h>
#include <sys/wait.h>

struct CONTEXT
{
  int64_t Rax;
  int64_t Rcx;
  int64_t Rdx;
  int64_t Rbx;
  int64_t Rsp;
  int64_t Rbp;
  int64_t Rsi;
  int64_t Rdi;
  int64_t R8;
  int64_t R9;
  int64_t R10;
  int64_t R11;
  int64_t R12;
  int64_t R13;
  int64_t R14;
  int64_t R15;
  int64_t Rip;
};


static struct sigaction g_OldSigAction;
static bool g_SignalHandlerInstalled = false;
extern "C" void CacheSimRemoveHandler();

static void DebugBreak()
{
  if ( g_SignalHandlerInstalled )
  {
    CacheSimRemoveHandler();
  }
  asm volatile("int $3\n");
} 

// Must include this AFTER declaring CONTEXT
#include "CacheSimCommon.inl"

// Missing libc syscalls
static int arch_prctl(int code, unsigned long* addr)
{
  return syscall(SYS_arch_prctl, code, addr);
}

uint64_t CacheSimGetCurrentThreadId()
{
  return (uint64_t)syscall(SYS_gettid);
}

static void ConvertToWinStyleContext(CONTEXT* out, const mcontext_t* in)
{
  out->Rax = in->gregs[REG_RAX];
  out->Rcx = in->gregs[REG_RCX];
  out->Rdx = in->gregs[REG_RDX];
  out->Rbx = in->gregs[REG_RBX];
  out->Rsp = in->gregs[REG_RSP];
  out->Rbp = in->gregs[REG_RBP];
  out->Rsi = in->gregs[REG_RSI];
  out->Rdi = in->gregs[REG_RDI];
  out->R8 = in->gregs[REG_R8];
  out->R9 = in->gregs[REG_R9];
  out->R10 = in->gregs[REG_R10];
  out->R11 = in->gregs[REG_R11];
  out->R12 = in->gregs[REG_R12];
  out->R13 = in->gregs[REG_R13];
  out->R14 = in->gregs[REG_R14];
  out->R15 = in->gregs[REG_R15];
  out->Rip = in->gregs[REG_RIP];
}

static uintptr_t AdjustFsSegment(uintptr_t address)
{
  unsigned long fsbase;
  arch_prctl(ARCH_GET_FS, &fsbase);
  return (uintptr_t)fsbase + address;
}

static uintptr_t AdjustGsSegment(uintptr_t address)
{
  unsigned long gsbase;
  arch_prctl(ARCH_GET_GS, &gsbase);
  return (uintptr_t)gsbase + address;
}

static void HandleTrap(int signo, siginfo_t* siginfo, void* ucontext_param)
{
  using namespace CacheSim;

  if ( g_TraceEnabled == false )
  {
    // Clear the trap bit
    ((ucontext_t*)ucontext_param)->uc_mcontext.gregs[REG_EFL] &= ~(0x100ull);
    return;
  }

  // Make sure the thread state is up to date.
  int curr_gen = g_Generation;
  ud_t* ud = &s_ThreadState.m_Disassembler;

  if ( s_ThreadState.m_Generation != curr_gen )
  {
    ud_init(ud);
    ud_set_mode(ud, 64);

    s_ThreadState.m_LogicalCoreIndex = FindLogicalCoreIndex(CacheSimGetCurrentThreadId());

    s_ThreadState.m_Generation = curr_gen;
    InvalidateStack();
  }


  const int core_index = s_ThreadState.m_LogicalCoreIndex;

  // Only trace threads we've mapped to cores. Ignore all others.
  if ( g_TraceEnabled && core_index >= 0 )
  {
    CONTEXT context;
    ConvertToWinStyleContext(&context, &((ucontext_t*)ucontext_param)->uc_mcontext);

    uintptr_t rip = context.Rip;
    if ( ~0u == s_ThreadState.m_StackIndex )
    {
      // Recompute call stack
      void* callstack[kMaxCalls];
      int frame_count = backtrace(callstack, kMaxCalls);
      if ( 0 == frame_count || kMaxCalls == frame_count )
        DebugBreak();

      AutoSpinLock lock;
      // Take off one more frame as we're splitting in two parts, stack and current_rip
      s_ThreadState.m_StackIndex = InsertStack((const uintptr_t*)&callstack[1], frame_count - 1);
    }

    ud_set_input_buffer(ud, (const uint8_t*)rip, 16);
    ud_set_pc(ud, rip);
    int ilen = ud_disassemble(ud);
    GenerateMemoryAccesses(core_index, ud, rip, ilen, &context);
  }
}

static char executable_filename[512];

void CacheSimInit()
{
  using namespace CacheSim;
  g_Stats.Init();
  g_Stacks.Init();
  memset(&g_StackData, 0, sizeof g_StackData);

  int len = readlink("/proc/self/exe", executable_filename, ARRAY_SIZE(executable_filename));
  
  if (len == -1 || len == ARRAY_SIZE(executable_filename))
  {
    DebugBreak();
  }

  executable_filename[len] = '\0';

  // Force backtrace to do its initialization here, outside of the signal handler
  void* callstack[kMaxCalls];
  backtrace(callstack, kMaxCalls);

}

static void ContinueProcess(const pid_t pid)
{
  int   status;
  pid_t p;

  do
  {
    if ( kill(pid, SIGCONT) == -1 )
    {
      DebugBreak();
    }

    do
    {
      status = 0;
      p = waitpid(pid, &status, WUNTRACED | WCONTINUED);
    } while ( p == (pid_t)-1 && errno == EINTR );

    if ( p != pid )
    {
      DebugBreak();
    }

  } while ( WIFSTOPPED(status) );

  return;
}

bool CacheSimStartCapture()
{
  using namespace CacheSim;
  if ( g_TraceEnabled )
  {
    return false;
  }

  // Reset.
  g_Cache.Init();

  pid_t child = fork();
  if ( child != 0 )
  {
    // Parent process
    __sync_fetch_and_add(&g_Generation, 1);

    g_TraceEnabled = 1;

    if ( g_SignalHandlerInstalled == false )
    {
      struct sigaction action;
      action.sa_sigaction = HandleTrap;
      sigemptyset(&action.sa_mask);
      action.sa_flags = SA_SIGINFO;
      int ok = sigaction(SIGTRAP, &action, &g_OldSigAction);
  
      if ( ok != 0 )
      {
        DebugBreak(); // Failed to install signal handler
      }

      g_SignalHandlerInstalled = true;
    }

    // We're in the main process. We need to make ourselves traceable
    prctl(PR_SET_DUMPABLE, (long)1);
    prctl(PR_SET_PTRACER, (long)child);

    int status;
    // Wait for child to finish hooking us
    wait(&status);

    // Wait for child to exit
    wait(&status);
  }
  else
  {
    // Trace parent and then die.
    pid_t tids[ARRAY_SIZE(s_CoreMappings)];
    int thread_count = 0;

    for ( int i = 0, count = s_CoreMappingCount; i < count; ++i )
    {
      const auto& mapping = s_CoreMappings[i];

      int ok = -1;
      do
      {
        ok = ptrace(PTRACE_ATTACH, mapping.m_ThreadId, nullptr, nullptr);
        if ( errno == ESRCH )
        {
          fprintf(stderr, "Thread %ld no longer exists.\n", mapping.m_ThreadId);
        }
      } while ( ok == -1 && (errno == EFAULT || errno == ESRCH) );

      if ( ok != -1 )
      {
        tids[thread_count++] = mapping.m_ThreadId;
        // Wait for the attachment to thread to stop
        int status;
        waitpid((pid_t)mapping.m_ThreadId, &status, 0);
      }
      else
      {
        fprintf(stderr, "Failed to stop thread %ld: %s\n", mapping.m_ThreadId, strerror(errno));
      }
    }

    for ( int i = 0; i < thread_count; i++ )
    {
      struct user_regs_struct regs;
      int ok = ptrace(PTRACE_GETREGS, tids[i], nullptr, &regs);
      if ( ok != 0 ) DebugBreak();
      regs.eflags |= 0x100;
      ok = ptrace(PTRACE_SETREGS, tids[i], nullptr, &regs);
      if ( ok != 0 ) DebugBreak();
    }

    // Detach all threads
    for ( int i = 0; i < thread_count; ++i )
    {
      int ok = -1;
      do
      {
        ok = ptrace(PTRACE_DETACH, tids[i], nullptr, nullptr);
      } while ( ok == -1 && (errno == EBUSY || errno == EFAULT || errno == ESRCH) );
    }
    ContinueProcess(getppid());
    exit(0);
  }

  return true;
}

struct ModuleInfo
{
  const char* m_Filename;
  void* m_StartAddrInMemory;
  void* m_SegmentOffset;
  size_t m_Length;
};

struct ModuleList
{
  ModuleInfo m_Infos[1024];
  int m_Count = 0;
  int m_ModuleCallbacks = 0;
};

static int RecordModule(struct dl_phdr_info* info, size_t size, void* data)
{
  ModuleList* modules = reinterpret_cast<ModuleList*>(data);
  modules->m_ModuleCallbacks++;
  if ( modules->m_Count == ARRAY_SIZE(modules->m_Infos) )
  {
    fprintf(stderr, "Cannot record additional modules. Increase the ModuleList::m_Infos buffer size.\n");
    return -1;
  }

  for ( ElfW(Half) i = 0; i < info->dlpi_phnum; i++ )
  {
    const ElfW(Phdr)& header = info->dlpi_phdr[i];

    // Find the executable segment
    if ( (header.p_flags & PF_X) && (header.p_type == PT_LOAD) )
    {
      ModuleInfo& module = modules->m_Infos[modules->m_Count];
      if ( info->dlpi_addr == getauxval(AT_SYSINFO_EHDR) )
      {
        // vdso section.
        continue;
      }
      
      module.m_StartAddrInMemory = (void*)info->dlpi_addr;
      module.m_SegmentOffset = (void*)header.p_vaddr;
      module.m_Length = header.p_memsz;

      bool hasName = (info->dlpi_name != nullptr) && (info->dlpi_name[0] != '\0');
      if ( !hasName && (modules->m_ModuleCallbacks == 1) ) 
      {
        // This is the main executable.
        module.m_Filename = executable_filename;
      }
      else if (hasName)
      {
        module.m_Filename = info->dlpi_name; // dlpi_name will continue to exist--at least on this implementation of linux :)
      }
      else
      {
        fprintf(stderr, "Failed to get name for module at address!!: %p\nSkipping\n", (int*)info->dlpi_addr);
        continue;
      }
      modules->m_Count++;
      return 0;
    }
  }

  return 0;
}

namespace
{
  template<typename T> void WriteHelper(FILE* f, const T& val)
  {
    fwrite(&val, 1, sizeof val, f);
  }
}

void CacheSimEndCapture(bool save)
{
  using namespace CacheSim;
  g_TraceEnabled = 0;

  // Clear our trap flag
  asm volatile("pushf\n"
    "andl $0xFFFFFFFFFFFFFEFF, (%rsp)\n"
    "popf\n");


  // Give the thread a few instructions to run so we're definitely not tracing.
  //Thread::NanoPause();
  usleep(0);
  usleep(0);
  usleep(0);

  if ( !save )
    return;

  AutoSpinLock lock;

  // It's tempting to remove the signal handler here
  //
  //    RemoveVectoredExceptionHandler(g_Handler);
  //    g_Handler = nullptr;
  //
  // ..but that's a mistake. There could be a syscall instruction paused in the kernel that
  // will come back and signal a single step trap at some arbitrary point in the future, so
  // we need our handler to stay in effect.

  const char* executable_name = "unknown";
  if ( char* p = strrchr(executable_filename, '/') )
  {
    ++p;
    executable_name = p;
  }

  char fn[512];
  snprintf(fn, ARRAY_SIZE(fn), "%s_%u.csim", executable_name, (uint32_t)time(nullptr));

  if ( FILE* f = fopen(fn, "w") )
  {
    auto align = [&f]()
    {
      if ( int needed = (8 - (ftell(f) & 7)) & 7 )
      {
        static const uint8_t padding[8] = { 0 };
        fwrite(padding, 1, needed, f);
      }
    };

#define welem(value) WriteHelper(f, value)

#define wdata(data, size) fwrite(data, 1, size, f);

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
        if ( ftell(m_File) != m_Offset )
        {
          DebugBreak();
        }
        fwrite(&value, 1, sizeof value, m_File);
        fseek(m_File, pos, SEEK_SET);
      }
    };

    welem(0xcace51afu);
    welem(0x00000002u);

    PatchWord module_offset{ f };
    PatchWord module_count{ f };

    PatchWord module_str_offset{ f };

    PatchWord frame_offset{ f };
    PatchWord frame_count{ f };

    PatchWord stats_offset{ f };
    PatchWord stats_count{ f };

    welem(0u); // symbol_offset
    welem(0u); // symbol_count
    welem(0u); // symbol_text_offset

    // Special handling for the main executable.
    ModuleList modules;
    int ok = dl_iterate_phdr(RecordModule, &modules);

    if ( modules.m_Count > 0 )
    {
      align();

      module_offset.Update(ftell(f));
      module_count.Update(static_cast<uint32_t>(modules.m_Count));
      uint32_t str_section_size = 0;

      for ( size_t i = 0; i < modules.m_Count; ++i )
      {
        ModuleInfo& info = modules.m_Infos[i];
        size_t len = strlen(info.m_Filename) + 1;
        welem(reinterpret_cast<uintptr_t>(info.m_StartAddrInMemory));
        welem(reinterpret_cast<uintptr_t>(info.m_SegmentOffset));
        welem(static_cast<uint32_t>(info.m_Length));
        welem(static_cast<uint32_t>(str_section_size));
        str_section_size += (uint32_t)len;
      }

      module_str_offset.Update(ftell(f));
      for ( size_t i = 0; i < modules.m_Count; ++i )
      {
        wdata(modules.m_Infos[i].m_Filename, strlen(modules.m_Infos[i].m_Filename) + 1);
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
    stats_count.Update((uint32_t)g_Stats.GetCount());
    for ( const RipKey& key : g_Stats.Keys() )
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
    fprintf(stderr, "Failed to open %s for writing", fn);
  }

  g_Stats.FreeAll();
  g_Stacks.FreeAll();

  VirtualMemoryFree(g_StackData.m_Frames, g_StackData.m_ReserveCount);
  memset(&g_StackData, 0, sizeof g_StackData);
}

void CacheSimRemoveHandler()
{
  sigaction(SIGTRAP, &g_OldSigAction, nullptr);
  g_SignalHandlerInstalled = false;
}
