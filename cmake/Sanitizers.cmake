# Copyright (c) 2026 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang" OR NOT SANITIZERS MATCHES "(^|,)integer(,|$)")
  return()
endif()

set(qt_ubsan_ignorelist ${PROJECT_SOURCE_DIR}/test/sanitizer_suppressions/ubsan.ignorelist)

if(BUILD_APP_TESTS)
  find_package(Python3 REQUIRED COMPONENTS Interpreter)
  add_executable(bitcoinqml_sanitizer_probe ${PROJECT_SOURCE_DIR}/test/test_sanitizer_suppressions.cpp)
  target_link_libraries(bitcoinqml_sanitizer_probe PRIVATE core_interface Qt6::Core)
  set(sanitizer_probe_options)
  if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND SANITIZERS MATCHES "(^|,)(address|leak)(,|$)")
    list(APPEND sanitizer_probe_options --check-leaks)
  endif()
  add_test(NAME bitcoinqml_sanitizer_suppression_tests
    COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/test/test_sanitizer_suppressions.py
      $<TARGET_FILE:bitcoinqml_sanitizer_probe> ${sanitizer_probe_options}
  )
  set_tests_properties(bitcoinqml_sanitizer_suppression_tests PROPERTIES LABELS unit TIMEOUT 30)
endif()

# Include Core's Qt library as well: its inline Qt functions are instrumented
# when compiled into the app. Core libraries without Qt keep their usual flags.
foreach(target bitcoinqt bitcoinqml bitcoin-core-app
    bitcoinqml_bootstrap_tests bitcoinqml_unit_tests bitcoinqml_qmltests
    bitcoinqml_test_application bitcoinqml_integration_tests bitcoinqml_sanitizer_probe)
  if(TARGET ${target})
    target_compile_options(${target} PRIVATE "-fsanitize-ignorelist=${qt_ubsan_ignorelist}")
  endif()
endforeach()
