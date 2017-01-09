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
#include "ObjectStack.h"

CacheSim::ObjectStack::ObjectStack()
  : m_FinalizerChain(nullptr)
  , m_CurrentPage(nullptr)
{}

CacheSim::ObjectStack::~ObjectStack()
{
  reset();
}

void CacheSim::ObjectStack::reset()
{
  Finalizer* f = m_FinalizerChain;
  while (f)
  {
    void* object_ptr = (char*)f + sizeof(Finalizer);
    size_t size = f->m_ElemSize;
    size_t count = f->m_ElemCount;
    char *elem_ptr = (char*)object_ptr + (size * (count - 1));
    while (count--)
    {
      f->m_Destructor(elem_ptr);
      elem_ptr -= size;
    }

    f = f->m_Next;
  }
  m_FinalizerChain = nullptr;

  Page* p = m_CurrentPage;
  while (p)
  {
    Page* next = p->m_Next;
    freePage(p);
    p = next;
  }
  m_CurrentPage = nullptr;
}

void* CacheSim::ObjectStack::allocRaw(size_t byte_count)
{
  if (!m_CurrentPage || m_CurrentPage->avail() < byte_count)
  {
    m_CurrentPage = allocPage(byte_count, m_CurrentPage);
  }

  char* dest = m_CurrentPage->m_Data + m_CurrentPage->m_Allocated;

  m_CurrentPage->m_Allocated += (byte_count + 15) & ~15ull;

  return dest;
}

void* CacheSim::ObjectStack::allocFinalized(size_t byte_count, size_t elem_count, void (*dtor)(void*))
{
  Finalizer* f = (Finalizer*) allocRaw(byte_count + sizeof(Finalizer));
  f->m_Next = m_FinalizerChain;
  f->m_Destructor = dtor;
  f->m_ElemSize = byte_count;
  f->m_ElemCount = elem_count;
  m_FinalizerChain = f;
  return f->m_Object;
}

CacheSim::ObjectStack::Page* CacheSim::ObjectStack::allocPage(size_t min_size, Page* next)
{
  static const size_t kDefaultPageSize = 1024 * 1024;

  size_t size = std::max(sizeof(Page) + min_size, kDefaultPageSize);
  Page* p = (Page*) new char[size];
  p->m_Size = size;
  p->m_Allocated = 0;
  p->m_Next = next;
  return p;
}

void CacheSim::ObjectStack::freePage(Page* p)
{
  delete[] (char*) p;
}
