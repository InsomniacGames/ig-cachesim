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
#include "Precompiled.h"

namespace CacheSim
{
  class ObjectStack
  {
    struct Finalizer
    {
      void       (*m_Destructor)(void *object);
      Finalizer*   m_Next;
      size_t       m_ElemSize;
      size_t       m_ElemCount;
      char         m_Object[0];
    };
    static_assert(sizeof(Finalizer) == 32, "wat");

    struct Page
    {
      size_t  m_Size;
      size_t  m_Allocated;
      Page*   m_Next;
      char    m_Data[0];

      size_t avail() const
      {
        return m_Size - m_Allocated;
      }
    };
    static_assert(sizeof(Page) == 24, "wat");

    Finalizer*    m_FinalizerChain;
    Page*         m_CurrentPage;

  public:
    ObjectStack();
    ~ObjectStack();

  public:
    void reset();

  private:
    void* allocRaw(size_t byte_count);
    void* allocFinalized(size_t byte_count, size_t elem_count, void (*dtor)(void*));

    Page* allocPage(size_t min_size, Page* next);
    void freePage(Page* p);

  private:
    template <typename T>
    T* allocStorage(size_t elem_count)
    {
      if (std::is_trivially_destructible<T>::value)
        return static_cast<T*>(allocRaw(sizeof(T) * elem_count));
      else
        return static_cast<T*>(allocFinalized(sizeof(T), elem_count, [](void* ptr) -> void { (void) ptr; static_cast<T*>(ptr)->~T(); }));
    }

  public:
    template <typename T>
    T* alloc()
    {
      return new (allocStorage<T>(1)) T;
    }

    template <typename T>
    T* allocArray(size_t count)
    {
      T* base = allocStorage<T>(count);
      for (size_t i = 0; i < count; ++i)
      {
        new (base + i) T;
      }

      return base;
    }

    template <typename T, typename... Args>
    T* alloc(Args&&... args)
    {
      return new (allocStorage<T>(1)) T(std::forward<Args>(args)...);
    }
  };
}
