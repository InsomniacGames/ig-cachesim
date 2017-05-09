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

// HelloWorld.cpp - the simplest possible demo example of CacheSim

#include "CacheSim/CacheSim.h"
#include <cmath>
#include <string>
#include <list>
#include <thread>

CacheSim::DynamicLoader cachesim;
volatile bool run = true;
volatile bool workFinished = false;
volatile bool canExit = false;

void DoSomeWork()
{
  cachesim.SetThreadCoreMapping(cachesim.GetCurrentThreadId(), 2);
  printf("Thread ID: %jd", (uintmax_t)cachesim.GetCurrentThreadId());
  std::list<uint64_t*> values;
  for (int i = 0; i < 10000; i++)
  {
    values.push_back(new uint64_t(i));
    if ((i % 500) == 0)
    {
      printf("DoSomeWork: %jd\n", (uintmax_t)i);
    }
  }
  uint64_t total = 0;
  int i = 0;
  for (auto& val : values)
  {
    total += *val;
    if ((i % 500) == 0)
    {
      printf("DoSomeWork: %jd\n", (uintmax_t)i);
    }
    i++;
    delete val;
  }
  while (run == true) { std::this_thread::yield(); }
  lgamma(1.f);
  workFinished = true;
  while (canExit == false) { std::this_thread::yield(); }
}

int main(int argc, char* argv[])
{

  if (!cachesim.Init())
    return 1;

  printf("Start\n");

  std::thread thread(
    [&]() {
    cachesim.SetThreadCoreMapping(cachesim.GetCurrentThreadId(), 1);
    printf("Thread ID: %jd", (uintmax_t)cachesim.GetCurrentThreadId());
    int count = 0;
    while (run) {
      std::string s;
      s.append("Hi!\n", count++);
      printf("%s %d\n", s.c_str(), count);
    }
    while (canExit == false) { std::this_thread::yield(); }
  });

  std::thread thread2(
    DoSomeWork
  );

  // This needs to happen to map thread ids to physical cores for cache simulation.
  // If a thread is not mapped, it will not be simulated.
  cachesim.SetThreadCoreMapping(cachesim.GetCurrentThreadId(), 0);

  cachesim.Start();

  printf("Hello, world (with cache simulation)!\n");

  std::chrono::milliseconds delay(500);
  std::this_thread::sleep_for(delay);

  // All this code is obviously racey and such, but I think it gets the point across.
  run = false;
  while (workFinished == false)
  {
    std::this_thread::sleep_for(delay);
  }

  // This must be called before the threads being traced destruct (on windows) or their TLS data will go away
  cachesim.End();
  canExit = true;
  thread.join();
  thread2.join();
  return 0;
}
