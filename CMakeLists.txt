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

# LibRaw — RAW decode/metadata. Homebrew (macOS) and libraw-dev (Linux CI)
# both ship a pkg-config file.
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBRAW REQUIRED IMPORTED_TARGET libraw)
if(APPLE)
  # Homebrew's libraw.pc currently advertises -lstdc++; AppleClang already links
  # libc++ as the C++ runtime, so passing it through creates duplicate-runtime
  # linker warnings on macOS without adding a needed dependency.
  get_target_property(_libraw_links PkgConfig::LIBRAW INTERFACE_LINK_LIBRARIES)
  if(_libraw_links)
    list(REMOVE_ITEM _libraw_links stdc++)
    set_target_properties(PkgConfig::LIBRAW PROPERTIES
      INTERFACE_LINK_LIBRARIES "${_libraw_links}")
  endif()
endif()

# Core library (algorithms live here; the CLI and tests link against it)
add_library(camera_iq_core STATIC
  src/toolkit.cpp
  src/filename_meta.cpp
  src/json_writer.cpp
  src/manifest.cpp
  src/raw_meta.cpp
  src/cfa_stats.cpp
  src/demosaic.cpp
  src/cmd_manifest.cpp
  src/cmd_raw_stats.cpp
  src/cmd_demosaic.cpp)
target_include_directories(camera_iq_core PUBLIC include)
target_compile_definitions(camera_iq_core PUBLIC
  CAMERA_IQ_VERSION="${PROJECT_VERSION}")
target_link_libraries(camera_iq_core PUBLIC PkgConfig::LIBRAW)

# CLI executable
add_executable(camera_iq src/main.cpp)
target_link_libraries(camera_iq PRIVATE camera_iq_core)

# Tests — one executable per unit, all registered with CTest.
enable_testing()

function(camera_iq_add_test name source)
  add_executable(${name} ${source})
  target_link_libraries(${name} PRIVATE camera_iq_core)
  add_test(NAME ${name} COMMAND ${name})
endfunction()

camera_iq_add_test(camera_iq_tests tests/test_validation.cpp)
camera_iq_add_test(test_filename_meta tests/test_filename_meta.cpp)
camera_iq_add_test(test_json_writer tests/test_json_writer.cpp)
camera_iq_add_test(test_manifest_scan tests/test_manifest_scan.cpp)
camera_iq_add_test(test_exposure_series tests/test_exposure_series.cpp)
camera_iq_add_test(test_raw_meta tests/test_raw_meta.cpp)
camera_iq_add_test(test_manifest_json tests/test_manifest_json.cpp)
camera_iq_add_test(test_cfa_stats tests/test_cfa_stats.cpp)
camera_iq_add_test(test_demosaic tests/test_demosaic.cpp)
