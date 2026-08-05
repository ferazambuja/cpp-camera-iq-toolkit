cmake_minimum_required(VERSION 3.20)

project(camera_iq
  VERSION 0.1.0
  DESCRIPTION "C++ toolkit for camera image-quality analysis"
  LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Emit build/compile_commands.json so clangd and the compiler resolve the same
# project include paths and compile options.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

add_compile_options(-Wall -Wextra -Wpedantic)

# LibRaw — RAW decode/metadata. Homebrew (macOS) and libraw-dev (Linux CI)
# both ship a pkg-config file.
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBRAW REQUIRED IMPORTED_TARGET libraw)
option(CAMERA_IQ_ENABLE_LCMS
  "Build optional LittleCMS color-transform cross-checks" OFF)
if(CAMERA_IQ_ENABLE_LCMS)
  pkg_check_modules(LCMS2 REQUIRED IMPORTED_TARGET lcms2)
endif()
# zlib — MATLAB v5 MAT-files store their payload as a deflate stream.
find_package(ZLIB REQUIRED)
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
  src/output_file.cpp
  src/dataset_config.cpp
  src/manifest.cpp
  src/raw_meta.cpp
  src/cfa_stats.cpp
  src/sha256.cpp
  src/mat_file.cpp
  src/spectro_measurement.cpp
  src/spectro_ledger.cpp
  src/spectro_ingest.cpp
  src/spectro_analysis.cpp
  src/spectro_colorimetry.cpp
  src/roi.cpp
  src/flat_field_gate.cpp
  src/demosaic.cpp
  src/dark_calibration.cpp
  src/noise.cpp
  src/sfr.cpp
  src/exposure_response.cpp
  src/oecf_fit.cpp
  src/imatest_stepchart.cpp
  src/stepchart_raw.cpp
  src/color_reference.cpp
  src/colorimetry.cpp
  src/cam16_equation_audit.cpp
  src/gamut_mapping.cpp
  src/gamut_mapping_report.cpp
  src/patches.cpp
  src/chart_localization.cpp
  src/stepchart_localization.cpp
  src/localization_diagnosis.cpp
  src/spectral_response.cpp
  src/spectral_closure.cpp
  src/spectral_quality.cpp
  src/spectral_smi.cpp
  src/cmd_manifest.cpp
  src/cmd_raw_stats.cpp
  src/cmd_demosaic.cpp
  src/cmd_dark_calibration.cpp
  src/cmd_noise.cpp
  src/cmd_sfr.cpp
  src/cmd_exposure_response.cpp
  src/cmd_oecf_fit.cpp
  src/cmd_oecf_stepchart.cpp
  src/cmd_reference_info.cpp
  src/cmd_ccm_fit.cpp
  src/cmd_gamut_map.cpp
  src/cmd_cam16_equation_audit.cpp
  src/cmd_spectral_response.cpp
  src/cmd_spectral_closure.cpp
  src/cmd_spectral_quality.cpp
  src/cmd_spectral_smi.cpp
  src/cmd_patches.cpp
  src/shading.cpp
  src/cmd_shading.cpp
  src/cmd_spectro_ingest.cpp)

target_include_directories(camera_iq_core PUBLIC include)
target_compile_definitions(camera_iq_core PUBLIC
  CAMERA_IQ_VERSION="${PROJECT_VERSION}")
target_link_libraries(camera_iq_core PUBLIC PkgConfig::LIBRAW ZLIB::ZLIB)

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

function(camera_iq_expect_doc_evidence target expectations)
  set(evidence_tmpdir ${CAMERA_IQ_TEST_TMPDIR}/doc-evidence-${target})
  file(MAKE_DIRECTORY ${evidence_tmpdir})
  add_test(NAME check_doc_evidence_${target}
    COMMAND ${CMAKE_COMMAND} -E env
            TMPDIR=${evidence_tmpdir}
            TMP=${evidence_tmpdir}
            TEMP=${evidence_tmpdir}
            CAMERA_IQ_DOC_EVIDENCE_EXPECT=${expectations}
            $<TARGET_FILE:${target}>)
endfunction()

camera_iq_add_test(camera_iq_tests tests/test_cli_basic.cpp)
camera_iq_add_test(test_harness tests/test_harness.cpp)
camera_iq_add_test(test_filename_meta tests/test_filename_meta.cpp)
camera_iq_add_test(test_dataset_config tests/test_dataset_config.cpp)
camera_iq_add_test(test_json_writer tests/test_json_writer.cpp)
camera_iq_add_test(test_output_file tests/test_output_file.cpp)
camera_iq_add_test(test_manifest_scan tests/test_manifest_scan.cpp)
camera_iq_add_test(test_exposure_series tests/test_exposure_series.cpp)
camera_iq_add_test(test_raw_meta tests/test_raw_meta.cpp)
camera_iq_add_test(test_manifest_json tests/test_manifest_json.cpp)
camera_iq_add_test(test_cfa_stats tests/test_cfa_stats.cpp)
camera_iq_add_test(test_sha256 tests/test_sha256.cpp)
camera_iq_add_test(test_mat_file tests/test_mat_file.cpp)
camera_iq_add_test(test_spectro_measurement tests/test_spectro_measurement.cpp)
camera_iq_add_test(test_spectro_ledger tests/test_spectro_ledger.cpp)
camera_iq_add_test(test_spectro_ingest tests/test_spectro_ingest.cpp)
camera_iq_add_test(test_spectro_analysis tests/test_spectro_analysis.cpp)
camera_iq_add_test(test_spectro_colorimetry tests/test_spectro_colorimetry.cpp)
camera_iq_add_test(test_cmd_spectro_ingest tests/test_cmd_spectro_ingest.cpp)
camera_iq_add_test(test_roi tests/test_roi.cpp)
camera_iq_add_test(test_demosaic tests/test_demosaic.cpp)
camera_iq_add_test(test_dark_calibration tests/test_dark_calibration.cpp)
camera_iq_add_test(test_noise tests/test_noise.cpp)
camera_iq_add_test(test_cmd_noise tests/test_cmd_noise.cpp)
camera_iq_add_test(test_sfr tests/test_sfr.cpp)
camera_iq_add_test(test_cmd_sfr tests/test_cmd_sfr.cpp)
camera_iq_add_test(test_exposure_response tests/test_exposure_response.cpp)
camera_iq_add_test(test_cmd_exposure_response tests/test_cmd_exposure_response.cpp)
camera_iq_add_test(test_cmd_dataset_labels tests/test_cmd_dataset_labels.cpp)
camera_iq_add_test(test_oecf_fit tests/test_oecf_fit.cpp)
camera_iq_add_test(test_imatest_stepchart tests/test_imatest_stepchart.cpp)
camera_iq_add_test(test_cmd_oecf_stepchart tests/test_cmd_oecf_stepchart.cpp)
camera_iq_add_test(test_stepchart_raw tests/test_stepchart_raw.cpp)
camera_iq_add_test(test_color_reference tests/test_color_reference.cpp)
camera_iq_add_test(test_colorimetry tests/test_colorimetry.cpp)
camera_iq_add_test(test_gamut_mapping tests/test_gamut_mapping.cpp)
camera_iq_add_test(test_gamut_mapping_report tests/test_gamut_mapping_report.cpp)
camera_iq_add_test(test_cmd_gamut_map tests/test_cmd_gamut_map.cpp)
camera_iq_add_test(test_cam16_equation_audit tests/test_cam16_equation_audit.cpp)
camera_iq_add_test(test_cmd_cam16_equation_audit tests/test_cmd_cam16_equation_audit.cpp)
if(CAMERA_IQ_ENABLE_LCMS)
  camera_iq_add_test(test_gamut_lcms tests/test_gamut_lcms.cpp)
  target_link_libraries(test_gamut_lcms PRIVATE PkgConfig::LCMS2)
endif()
camera_iq_add_test(test_cmd_ccm_fit tests/test_cmd_ccm_fit.cpp)
camera_iq_add_test(test_cmd_reference_info tests/test_cmd_reference_info.cpp)
camera_iq_add_test(test_patches tests/test_patches.cpp)
camera_iq_add_test(test_chart_localization tests/test_chart_localization.cpp)
camera_iq_add_test(test_stepchart_localization tests/test_stepchart_localization.cpp)
camera_iq_add_test(test_localization_diagnosis tests/test_localization_diagnosis.cpp)
camera_iq_add_test(test_spectral_response tests/test_spectral_response.cpp)
camera_iq_add_test(test_cmd_spectral_response tests/test_cmd_spectral_response.cpp)
camera_iq_add_test(test_spectral_closure tests/test_spectral_closure.cpp)
camera_iq_add_test(test_spectral_quality tests/test_spectral_quality.cpp)
camera_iq_add_test(test_spectral_smi tests/test_spectral_smi.cpp)
camera_iq_add_test(test_cmd_spectral_smi tests/test_cmd_spectral_smi.cpp)
camera_iq_add_test(test_cmd_spectral_quality tests/test_cmd_spectral_quality.cpp)
camera_iq_add_test(test_cmd_spectral_closure tests/test_cmd_spectral_closure.cpp)
camera_iq_add_test(test_cmd_patches tests/test_cmd_patches.cpp)
camera_iq_add_test(test_flat_field_gate tests/test_flat_field_gate.cpp)
camera_iq_add_test(test_shading tests/test_shading.cpp)
camera_iq_add_test(test_cmd_shading tests/test_cmd_shading.cpp)

# Test-only producer for the cross-language shading publication contract. It
# serializes a synthetic accepted field through the real C++ JSON writer; no
# private RAW input is needed.
add_executable(emit_shading_contract tests/emit_shading_contract.cpp)
target_link_libraries(emit_shading_contract PRIVATE camera_iq_core)
add_executable(emit_spectro_schema tests/emit_spectro_schema.cpp)
target_link_libraries(emit_spectro_schema PRIVATE camera_iq_core)

add_test(NAME check_public_paths
  COMMAND bash ${CMAKE_SOURCE_DIR}/tools/check_public_paths.sh)
add_test(NAME check_sample_fixtures
  COMMAND bash ${CMAKE_SOURCE_DIR}/tools/check_sample_fixtures.sh)
add_test(NAME check_verify_ccsg_vs_xrite_behavior
  COMMAND bash ${CMAKE_SOURCE_DIR}/tools/check_verify_ccsg_vs_xrite_behavior.sh)
set_tests_properties(
  check_public_paths
  check_sample_fixtures
  check_verify_ccsg_vs_xrite_behavior
  PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
add_test(NAME check_portfolio_docs
  COMMAND python3 ${CMAKE_SOURCE_DIR}/tools/check_portfolio_docs.py
          --repo-root ${CMAKE_SOURCE_DIR})
add_test(NAME check_portfolio_figures
  COMMAND python3 ${CMAKE_SOURCE_DIR}/tools/generate_portfolio_figures.py
          --repo-root ${CMAKE_SOURCE_DIR} --check)
add_test(NAME check_public_paths_behavior
  COMMAND bash ${CMAKE_SOURCE_DIR}/tools/test_check_public_paths.sh
          ${CMAKE_SOURCE_DIR}/tools/check_public_paths.sh)
add_test(NAME check_cie_cmf_1nm
  COMMAND python3 ${CMAKE_SOURCE_DIR}/tools/check_cie_cmf_1nm.py
          --repo-root ${CMAKE_SOURCE_DIR})
add_test(NAME test_check_cie_cmf_1nm
  COMMAND python3 ${CMAKE_SOURCE_DIR}/tools/test_check_cie_cmf_1nm.py)
add_test(NAME check_spectro_identity_ledger
  COMMAND python3 ${CMAKE_SOURCE_DIR}/tools/generate_spectro_identity_ledger.py
          --check ${CMAKE_SOURCE_DIR}/data/spectro_identity_ledger.csv)
add_test(NAME test_generate_spectro_identity_ledger
  COMMAND python3 ${CMAKE_SOURCE_DIR}/tools/test_generate_spectro_identity_ledger.py)
add_test(NAME test_compare_spectro_crosscheck
  COMMAND python3 ${CMAKE_SOURCE_DIR}/tools/test_compare_spectro_crosscheck.py)
add_test(NAME check_spectro_matlab_crosscheck_receipt
  COMMAND python3 ${CMAKE_SOURCE_DIR}/tools/check_spectro_matlab_crosscheck_receipt.py)
add_test(NAME test_check_spectro_matlab_crosscheck_receipt
  COMMAND python3 ${CMAKE_SOURCE_DIR}/tools/test_check_spectro_matlab_crosscheck_receipt.py)
add_test(NAME check_spectro_receipt
  COMMAND python3 ${CMAKE_SOURCE_DIR}/tools/check_spectro_receipt.py
          --repo-root ${CMAKE_SOURCE_DIR})
add_test(NAME test_generate_spectro_receipt
  COMMAND python3 ${CMAKE_SOURCE_DIR}/tools/test_generate_spectro_receipt.py)
add_test(NAME test_check_spectro_receipt
  COMMAND python3 ${CMAKE_SOURCE_DIR}/tools/test_check_spectro_receipt.py)
add_test(NAME check_spectro_report_figure
  COMMAND python3 ${CMAKE_SOURCE_DIR}/tools/generate_spectro_report_figure.py
          --repo-root ${CMAKE_SOURCE_DIR} --check)
add_test(NAME check_patch_baseline
  COMMAND python3 ${CMAKE_SOURCE_DIR}/tools/check_patch_baseline.py
          --repo-root ${CMAKE_SOURCE_DIR})
add_test(NAME test_check_patch_baseline
  COMMAND python3 ${CMAKE_SOURCE_DIR}/tools/test_check_patch_baseline.py)
add_test(NAME check_schema_contract
  COMMAND python3 ${CMAKE_SOURCE_DIR}/tools/check_schema_contract.py
          --repo-root ${CMAKE_SOURCE_DIR}
          --shading-producer $<TARGET_FILE:emit_shading_contract>
          --spectro-producer $<TARGET_FILE:emit_spectro_schema>)
add_test(NAME test_check_schema_contract
  COMMAND python3 ${CMAKE_SOURCE_DIR}/tools/test_check_schema_contract.py
          --shading-producer $<TARGET_FILE:emit_shading_contract>
          --spectro-producer $<TARGET_FILE:emit_spectro_schema>)
add_test(NAME test_portfolio_docs
  COMMAND python3 ${CMAKE_SOURCE_DIR}/tools/test_portfolio_docs.py)
add_test(NAME test_portfolio_figures
  COMMAND python3 ${CMAKE_SOURCE_DIR}/tools/test_portfolio_figures.py)
add_test(NAME test_export_shading_portfolio
  COMMAND python3 ${CMAKE_SOURCE_DIR}/tools/test_export_shading_portfolio.py)
add_test(NAME test_generate_gamut_portfolio
  COMMAND python3 ${CMAKE_SOURCE_DIR}/tools/test_generate_gamut_portfolio.py)
add_test(NAME test_generate_cam16_equation_audit
  COMMAND python3 ${CMAKE_SOURCE_DIR}/tools/test_generate_cam16_equation_audit.py)
add_test(NAME check_cam16_equation_audit
  COMMAND python3 ${CMAKE_SOURCE_DIR}/tools/generate_cam16_equation_audit.py
          --camera-iq $<TARGET_FILE:camera_iq> --check)
add_test(NAME check_gamut_portfolio
  COMMAND python3 ${CMAKE_SOURCE_DIR}/tools/generate_gamut_portfolio.py
          --camera-iq $<TARGET_FILE:camera_iq> --check)

# Several hermetic tests use stable names below the operating-system temporary
# directory. Give each build configuration its own temporary root so Release,
# sanitizer, gcc, and clang suites can run concurrently without deleting one
# another's fixtures.
set(CAMERA_IQ_TEST_TMPDIR "${CMAKE_CURRENT_BINARY_DIR}/test-tmp")
file(MAKE_DIRECTORY "${CAMERA_IQ_TEST_TMPDIR}")
get_property(CAMERA_IQ_REGISTERED_TESTS DIRECTORY PROPERTY TESTS)
set_tests_properties(${CAMERA_IQ_REGISTERED_TESTS} PROPERTIES
  ENVIRONMENT
    "TMPDIR=${CAMERA_IQ_TEST_TMPDIR};TMP=${CAMERA_IQ_TEST_TMPDIR};TEMP=${CAMERA_IQ_TEST_TMPDIR}")

# Register dedicated runtime evidence checks after shared test properties are
# complete, so a later property assignment cannot disable or rewrite them.
camera_iq_expect_doc_evidence(test_raw_meta
  raw_foundation_row_pitch=2,raw_foundation_black_2x2=4,raw_foundation_black_repeat_periodicity=6)
camera_iq_expect_doc_evidence(test_sfr
  sfr_broad_gaussian_bounds=4,sfr_nyquist_accuracy=1)
camera_iq_expect_doc_evidence(test_cmd_ccm_fit
  color_characterization_ccm_external_labels=2,color_characterization_ccm_provenance=3,color_characterization_ccm_refusals=6)
camera_iq_expect_doc_evidence(test_patches
  color_characterization_localization_shifted=3,color_characterization_localization_offset=3,color_characterization_localization_verdict=2)
camera_iq_expect_doc_evidence(test_spectral_quality
  spectral_fidelity_luther_scale_invariance=5)
camera_iq_expect_doc_evidence(test_flat_field_gate
  flat_field_cfa_balanced_roi=4,flat_field_threshold_boundaries=21)
camera_iq_expect_doc_evidence(test_shading
  flat_field_radial_asymmetry=2)
