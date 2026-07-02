cmake_minimum_required(VERSION 3.20)

project(camera_iq
  VERSION 0.1.0
  DESCRIPTION "C++ toolkit for camera image-quality analysis"
  LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

add_compile_options(-Wall -Wextra -Wpedantic)

# Core library (algorithms live here; the CLI and tests link against it)
add_library(camera_iq_core STATIC
  src/toolkit.cpp)
target_include_directories(camera_iq_core PUBLIC include)
target_compile_definitions(camera_iq_core PUBLIC
  CAMERA_IQ_VERSION="${PROJECT_VERSION}")

# CLI executable
add_executable(camera_iq src/main.cpp)
target_link_libraries(camera_iq PRIVATE camera_iq_core)

# Tests
enable_testing()
add_executable(camera_iq_tests tests/test_validation.cpp)
target_link_libraries(camera_iq_tests PRIVATE camera_iq_core)
add_test(NAME validation COMMAND camera_iq_tests)
