# Copyright (c) 2017, Insomniac Games
#
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

find_package(Qt5 COMPONENTS Widgets REQUIRED)

set(moc_inputs
  CacheSimMainWindow.h
  CacheSimMainWindow.h
  TraceTab.h
  TraceData.h
  FlatModel.h
  TreeModel.h
  FlatProfileView.h
  TreeProfileView.h
  BaseProfileView.h
)

foreach(moc_input IN LISTS moc_inputs)
  get_filename_component(basename "${moc_input}" NAME_WE)
  list(APPEND moc_output "${CMAKE_CURRENT_BINARY_DIR}/aux_${basename}.moc")
  qt5_generate_moc("${moc_input}" "${CMAKE_CURRENT_BINARY_DIR}/aux_${basename}.moc")
endforeach()

set(ui_inputs
  CacheSimMainWindow.ui
  TraceTab.ui
  FlatProfileView.ui
  TreeProfileView.ui
)

foreach(ui_input IN LISTS ui_inputs)
  get_filename_component(basename "${ui_input}" NAME_WE)
  list(APPEND uic_output "${CMAKE_CURRENT_BINARY_DIR}/ui_${basename}.h")
  qt5_wrap_ui("${CMAKE_CURRENT_BINARY_DIR}/ui_${basename}.h" "${ui_input}")
endforeach()

set(CMAKE_AUTORCC TRUE)

if (WIN32)
  set(PLATFORM_SRC_FILES TraceDataWindows.cpp)
else (WIN32)
 set(PLATFORM_SRC_FILES TraceDataLinux.cpp)
endif (WIN32)

add_executable(CacheSimUI WIN32
  AnnotationView.cpp  AnnotationView.h
  BaseProfileView.cpp BaseProfileView.h
  CacheSimGUIMain.cpp
  CacheSimMainWindow.cpp CacheSimMainWindow.h 
  FlatModel.cpp FlatModel.h
  FlatProfileView.cpp FlatProfileView.h
  NumberFormatters.cpp NumberFormatters.h
  ObjectStack.cpp ObjectStack.h
  Precompiled.cpp Precompiled.h
  TraceData.cpp TraceData.h
  TraceTab.cpp TraceTab.h
  TreeModel.cpp TreeModel.h
  TreeProfileView.cpp TreeProfileView.h
  ${PLATFORM_SRC_FILES}

  Resources.qrc

  ${moc_output}
  ${uic_output}
)

if (WIN32)
  target_compile_definitions(CacheSimUI
    PRIVATE "IG_CACHESIM_API=__declspec(dllimport)" "NOMINMAX" "WIN32_LEAN_AND_MEAN")

  target_compile_options(CacheSimUI
    PRIVATE "/W4" "/wd4127" "/wd4458" "/wd4200" "/wd4718")

  target_link_libraries(CacheSimUI
    PRIVATE Qt5::Widgets "dbghelp")
else (WIN32)
  target_link_libraries(CacheSimUI
    PRIVATE Qt5::Widgets)
  target_compile_options(CacheSimUI
    PRIVATE -g)
endif (WIN32)

target_include_directories(CacheSimUI
  PRIVATE "${CMAKE_SOURCE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}")


set_target_properties(CacheSimUI PROPERTIES FOLDER "UI")
