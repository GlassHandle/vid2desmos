cmake_minimum_required(VERSION 3.16)
project(vid2desmos VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

add_library(v2d_core STATIC
  src/frame.cpp
  src/filter.cpp
  src/convolution.cpp
  src/threshold.cpp
  src/pipeline.cpp
  src/desmos.cpp
  src/video.cpp
  src/png.cpp
  src/cli.cpp
)

target_include_directories(v2d_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

if(MSVC)
  target_compile_options(v2d_core PRIVATE /W4 /permissive-)
  target_compile_definitions(v2d_core PRIVATE _CRT_SECURE_NO_WARNINGS)
else()
  target_compile_options(v2d_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

option(V2D_USE_OPENMP "Enable OpenMP for convolution" ON)
if(V2D_USE_OPENMP)
  find_package(OpenMP QUIET)
  if(OpenMP_CXX_FOUND)
    target_link_libraries(v2d_core PUBLIC OpenMP::OpenMP_CXX)
    message(STATUS "vid2desmos: OpenMP enabled")
  else()
    message(STATUS "vid2desmos: OpenMP not found, building single-threaded")
  endif()
endif()

add_executable(vid2desmos src/main.cpp)
target_link_libraries(vid2desmos PRIVATE v2d_core)
if(MSVC)
  target_compile_options(vid2desmos PRIVATE /W4 /permissive-)
  target_compile_definitions(vid2desmos PRIVATE _CRT_SECURE_NO_WARNINGS)
else()
  target_compile_options(vid2desmos PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 9.0)
  target_link_libraries(v2d_core PUBLIC stdc++fs)
endif()

enable_testing()
add_subdirectory(tests)
