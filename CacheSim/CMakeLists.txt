# Copyright (c) 2017, Insomniac Games
# All rights reserved.
# 
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:
# 
# Redistributions of source code must retain the above copyright notice, this
# list of conditions and the following disclaimer.
# 
# Redistributions in binary form must reproduce the above copyright notice, this
# list of conditions and the following disclaimer in the documentation and/or
# other materials provided with the distribution.
# 
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
# ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
# WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
# DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
# FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
# DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
# SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
# CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
# OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

set(SRC_FILES 
  CacheSim.h
  CacheSimCommon.inl
  CacheSimData.h
  CacheSimInternals.cpp
  CacheSimInternals.h
  GenericHashTable.h
  Md5.cpp
  Md5.h
  Platform.h
  Precompiled.cpp
  Precompiled.h
  ../README.md
)

set_source_files_properties(../README.md CacheSimCommon.inl PROPERTIES HEADER_FILE_ONLY TRUE)

set(LIB_LIST udis86)

if ( MSVC )
  set(SRC_FILES ${SRC_FILES} CacheSimWindows.cpp PlatformWindows.cpp)
else ( MSVC )
  set(SRC_FILES ${SRC_FILES} CacheSimLinux.cpp PlatformLinux.cpp )
endif (MSVC)

add_library(CacheSim SHARED ${SRC_FILES})

if ( MSVC )
  target_compile_definitions(CacheSim
    INTERFACE   "IG_CACHESIM_API=__declspec(dllimport)"
    PRIVATE     "IG_CACHESIM_API=__declspec(dllexport)" "NOMINMAX" "WIN32_LEAN_AND_MEAN" "_CRT_SECURE_NO_WARNINGS")
  target_compile_options(CacheSim
    PRIVATE "/W4")
else ( MSVC )
  set(LIB_LIST ${LIB_LIST} dl pthread)
  target_compile_options(CacheSim PRIVATE 
    "-std=c++11" 
    -g)
  set(CMAKE_SHARED_LINKER_FLAGS ${CMAKE_SHARED_LINKER_FLAGS} -Wl,-zdefs)
endif (MSVC)

target_link_libraries(CacheSim ${LIB_LIST})
target_include_directories(CacheSim PRIVATE "${CMAKE_SOURCE_DIR}")
set_target_properties(CacheSim PROPERTIES FOLDER "CacheSim")
