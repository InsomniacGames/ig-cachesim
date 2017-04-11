#include "Platform.h"
#include <sys/mman.h>

void* VirtualMemoryAlloc(size_t size)
{
  return mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
}

void VirtualMemoryFree(void* data, size_t size)
{
  munmap(data, size);
}
