cmake_minimum_required(VERSION 3.10)
project(xrender_graph_compiler LANGUAGES C CXX)
set(TARGET_PROJECT "xrender_graph_compiler")
set(COMPONENT_REGISTRY "")

# /MT as shaderc's (the custom nodes' shaders are compiled with it, as the material compiler does; MSVC)
if(MSVC)
  set(CMAKE_C_FLAGS_RELEASE "/MT")
  set(CMAKE_C_FLAGS_DEBUG "/MT")
  set(CMAKE_CXX_FLAGS_RELEASE "/MT")
  set(CMAKE_CXX_FLAGS_DEBUG "/MT")
endif()

include(FetchContent)
FetchContent_Declare(
  xcmake_tools
  GIT_REPOSITORY https://github.com/LIONant-depot/xcmake_tools.git
  GIT_TAG main
  GIT_SHALLOW TRUE
  SOURCE_DIR "${CMAKE_BINARY_DIR}/_deps/xcmake_tools"
)
FetchContent_MakeAvailable(xcmake_tools)
include(${CMAKE_BINARY_DIR}/_deps/xcmake_tools/Common.cmake)

add_executable(${TARGET_PROJECT}
  "source/Compiler/main.cpp"
  "source/Compiler/xrender_graph_compiler.cpp"
  "source/Compiler/xrender_graph_compiler.h"
)

source_group("" FILES
  "source/Compiler/main.cpp"
)

# Allow access to other plugins
target_include_directories(${TARGET_PROJECT} PRIVATE $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/../../>)

target_include_directories(${TARGET_PROJECT} PRIVATE "${CMAKE_SOURCE_DIR}/dependencies/shaderc/include")
add_compile_definitions(_ITERATOR_DEBUG_LEVEL=0)
if(MSVC)
    set_target_properties(${TARGET_PROJECT} PROPERTIES LINK_FLAGS "/IGNORE:4099")
endif()

add_subdirectory("build/dependency" "${CMAKE_CURRENT_BINARY_DIR}/xrender_graph_compiler")

ProcessComponents()
