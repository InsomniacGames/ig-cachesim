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
#include <Windows.h>

// This DLL is loaded at runtime by the engine to perform cache simulation.
// As such there's no static binding to these functions, they're looked up with GetProcAddress().

#ifndef IG_CACHESIM_API
#define IG_CACHESIM_API __declspec(dllimport)
#endif

extern "C"
{
  // Initializes the API. Only call once.
  IG_CACHESIM_API void CacheSimInit();

  // Set what Jaguar core (0-7) this Win32 thread ID will map to.
  // Threads without a jaguar core id will not be recorded, so you'll need to set up atleast one.
  IG_CACHESIM_API void CacheSimSetThreadCoreMapping(uint32_t thread_id, int logical_core_id);

  // Start recording a capture, buffering it to memory.
  IG_CACHESIM_API bool CacheSimStartCapture();

  // Stop recording and optionally save the capture to disk.
  IG_CACHESIM_API void CacheSimEndCapture(bool save);

  // Remove the exception handler machinery.
  IG_CACHESIM_API void CacheSimRemoveHandler(void);
}

//--------------------------------------------------------------------------------------------------
// Shim helper to load the cache simulator dynamically.

namespace CacheSim
{
  class DynamicLoader
  {
  private:
    HMODULE m_Module = nullptr;
    decltype(&CacheSimInit) m_InitFn = nullptr;
    decltype(&CacheSimStartCapture) m_StartCaptureFn = nullptr;
    decltype(&CacheSimEndCapture) m_EndCaptureFn = nullptr;
    decltype(&CacheSimRemoveHandler) m_RemoveHandlerFn = nullptr;
    decltype(&CacheSimSetThreadCoreMapping) m_SetThreadCoreMapping = nullptr;

  public:
    DynamicLoader()
    {}

  public:
    inline bool Init()
    {
      if (!m_Module)
      {
        m_Module = LoadLibraryA("CacheSim.dll");
        if (!m_Module)
        {
          char msg[512];
          _snprintf_s(msg, sizeof msg, "Failed to load CacheSim.dll - Win32 error: %u\n", GetLastError());
          MessageBoxA(NULL, msg, "Error", MB_OK|MB_ICONERROR);
          return false;
        }

        m_InitFn                = (decltype(&CacheSimInit))                 GetProcAddress(m_Module, "CacheSimInit");
        m_StartCaptureFn        = (decltype(&CacheSimStartCapture))         GetProcAddress(m_Module, "CacheSimStartCapture");
        m_EndCaptureFn          = (decltype(&CacheSimEndCapture))           GetProcAddress(m_Module, "CacheSimEndCapture");
        m_RemoveHandlerFn       = (decltype(&CacheSimRemoveHandler))        GetProcAddress(m_Module, "CacheSimRemoveHandler");
        m_SetThreadCoreMapping  = (decltype(&CacheSimSetThreadCoreMapping)) GetProcAddress(m_Module, "CacheSimSetThreadCoreMapping");

        if (!(m_InitFn && m_StartCaptureFn && m_EndCaptureFn && m_RemoveHandlerFn && m_SetThreadCoreMapping))
        {
          MessageBoxA(NULL, "CacheSim API mismatch", "Error", MB_OK|MB_ICONERROR);
          FreeLibrary(m_Module);
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

    inline void SetThreadCoreMapping(uint32_t thread_id, int logical_core)
    {
      m_SetThreadCoreMapping(thread_id, logical_core);
    }
  };
}
