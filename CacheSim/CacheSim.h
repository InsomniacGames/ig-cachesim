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

#pragma once

#include <stdint.h>
#include <stdio.h>
#if defined(_MSC_VER)
#include <Windows.h>
#ifndef IG_CACHESIM_API
#define IG_CACHESIM_API __declspec(dllimport)
#endif
#else
#include <dlfcn.h>
#include <unistd.h>
#define IG_CACHESIM_API
#endif


#if defined(_MSC_VER)
#define CACHE_SIM_LIB_NAME "CacheSim.dll"
#define IG_LoadLib(NAME) LoadLibraryA(NAME)
#define IG_UnloadLib(HANDLE) FreeLibrary(HANDLE)
#define IG_GetFuncAddress(HANDLE, NAME) GetProcAddress(HANDLE, NAME)
#define IG_ThreadYield() Sleep(0)
namespace CacheSim
{
  inline int32_t AtomicCompareExchange(volatile int32_t* addr, int32_t new_val, int32_t old_val) { return _InterlockedCompareExchange((volatile LONG*)addr, new_val, old_val); }
  inline int32_t AtomicIncrement(volatile int32_t* addr) { return _InterlockedIncrement((volatile LONG*)addr); }
  inline void PrintError(const char* error) { MessageBoxA(NULL, error, "Error", MB_OK | MB_ICONERROR); }
  inline void SleepMilliseconds(int ms) { Sleep(ms); }
}
#else
#define CACHE_SIM_LIB_NAME "libCacheSim.so"
#define IG_LoadLib(NAME) dlopen(NAME, RTLD_NOW)
#define IG_UnloadLib(HANDLE) dlclose(HANDLE)
#define IG_GetFuncAddress(HANDLE, NAME) dlsym(HANDLE, NAME)
#define IG_ThreadYield() sched_yield()
namespace CacheSim
{
  inline int32_t AtomicCompareExchange(volatile int32_t* addr, int32_t new_val, int32_t old_val) { return __sync_val_compare_and_swap(addr, old_val, new_val); }
  inline int32_t AtomicIncrement(volatile int32_t* addr) { return __sync_fetch_and_add(addr, 1); }
  inline void SleepMilliseconds(int ms) { usleep(ms * 1000); }
  inline void PrintError(const char* error) { fprintf(stderr, "%s\n", error); }
}
#endif

/*! \file
This DLL is loaded at runtime by the engine to perform cache simulation.
As such there's no static binding to these functions, they're looked up with GetProcAddress().
*/

extern "C"
{
  /// Initializes the API. Only call once.
  IG_CACHESIM_API void CacheSimInit();

  /// Returns a thread ID suitable for use with CachesimSetThreadCoreMapping
  IG_CACHESIM_API uint64_t CacheSimGetCurrentThreadId();

  /// Set what Jaguar core (0-7) this Win32 thread ID will map to.
  /// Threads without a jaguar core id will not be recorded, so you'll need to set up atleast one.
  /// A core id of -1 will disable recording the thread (e.g., upon thread completion)
  IG_CACHESIM_API void CacheSimSetThreadCoreMapping(uint64_t thread_id, int logical_core_id);

  /// Start recording a capture, buffering it to memory.
  IG_CACHESIM_API bool CacheSimStartCapture();

  /// Stop recording and optionally save the capture to disk.
  IG_CACHESIM_API void CacheSimEndCapture(bool save);

  /// Remove the exception handler machinery.
  IG_CACHESIM_API void CacheSimRemoveHandler(void);
}

//--------------------------------------------------------------------------------------------------
/// Shim helper to load the cache simulator dynamically.

namespace CacheSim
{
  class DynamicLoader
  {
  private:
#if defined(_MSC_VER)
    HMODULE m_Module = nullptr;
#else
    void* m_Module = nullptr;
#endif
    decltype(&CacheSimInit) m_InitFn = nullptr;
    decltype(&CacheSimStartCapture) m_StartCaptureFn = nullptr;
    decltype(&CacheSimEndCapture) m_EndCaptureFn = nullptr;
    decltype(&CacheSimRemoveHandler) m_RemoveHandlerFn = nullptr;
    decltype(&CacheSimSetThreadCoreMapping) m_SetThreadCoreMapping = nullptr;
    decltype(&CacheSimGetCurrentThreadId) m_GetCurrentThreadId = nullptr;

  public:
    DynamicLoader()
    {}

  public:
    bool Init()
    {
      if (!m_Module)
      {
        m_Module = IG_LoadLib(CACHE_SIM_LIB_NAME);
        if (!m_Module)
        {
          char msg[512];
#if defined(_MSC_VER)
          _snprintf_s(msg, sizeof msg, "Failed to load CacheSim.dll - Win32 error: %u\n", GetLastError());
#else
          snprintf(msg, sizeof msg, "Failed to load libCacheSim.so - Linux error: %s\n", dlerror());
#endif
          PrintError(msg);
          return false;
        }

        m_InitFn =                (decltype(&CacheSimInit))                 IG_GetFuncAddress(m_Module, "CacheSimInit");
        m_StartCaptureFn =        (decltype(&CacheSimStartCapture))         IG_GetFuncAddress(m_Module, "CacheSimStartCapture");
        m_EndCaptureFn =          (decltype(&CacheSimEndCapture))           IG_GetFuncAddress(m_Module, "CacheSimEndCapture");
        m_RemoveHandlerFn =       (decltype(&CacheSimRemoveHandler))        IG_GetFuncAddress(m_Module, "CacheSimRemoveHandler");
        m_SetThreadCoreMapping =  (decltype(&CacheSimSetThreadCoreMapping)) IG_GetFuncAddress(m_Module, "CacheSimSetThreadCoreMapping");
        m_GetCurrentThreadId =    (decltype(&CacheSimGetCurrentThreadId))   IG_GetFuncAddress(m_Module, "CacheSimGetCurrentThreadId");

        if (!(m_InitFn && m_StartCaptureFn && m_EndCaptureFn && m_RemoveHandlerFn && m_SetThreadCoreMapping && m_GetCurrentThreadId))
        {
          PrintError("CacheSim API mismatch");
          IG_UnloadLib(m_Module);
          m_Module = nullptr;
          return false;
        }

        m_InitFn();
      }

      return true;
    }


    inline bool Start()
    {
      return m_StartCaptureFn();
    }

    inline void End()
    {
      m_EndCaptureFn(1);
    }

    inline void Cancel()
    {
      m_EndCaptureFn(0);
    }

    inline void RemoveHandler()
    {
      m_RemoveHandlerFn();
    }

    inline void SetThreadCoreMapping(uint64_t thread_id, int logical_core)
    {
      m_SetThreadCoreMapping(thread_id, logical_core);
    }

    inline uint64_t GetCurrentThreadId()
    {
      return m_GetCurrentThreadId();
    }
  };
}
