# Copyright (c) 2026 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or https://opensource.org/license/mit/.

# Exercise the real, ordinary GUI entry point without the Python test bridge.
if(NOT DEFINED GUI OR NOT DEFINED TEST_ROOT)
  message(FATAL_ERROR "GUI and TEST_ROOT are required")
endif()
string(RANDOM LENGTH 12 ALPHABET 0123456789abcdef test_id)
set(profile "${TEST_ROOT}/invalid-datadir-${test_id}")
file(MAKE_DIRECTORY "${profile}/settings")
file(WRITE "${profile}/not-a-directory" "fixture")
foreach(datadir IN ITEMS "relative-missing" "~/missing" "${profile}/absolute-missing" "${profile}/not-a-directory")
  execute_process(
    COMMAND "${GUI}" -regtest -qml_onboarded=1 -noconf -nosettings -lang=en
      "-test-settings-dir=${profile}/settings" "-datadir=${datadir}"
    WORKING_DIRECTORY "${profile}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE stdout
    ERROR_VARIABLE stderr
    TIMEOUT 15
  )
  if(NOT result STREQUAL "1" OR NOT stderr MATCHES "Specified data directory.*does not exist")
    message(FATAL_ERROR "Invalid datadir did not produce a normal startup error (${result}): ${stdout}${stderr}")
  endif()
endforeach()

# Qt options must reach Core's argument validation unchanged. In particular,
# an invalid platform must not be consumed by Qt before Core can reject it.
foreach(option IN ITEMS -qwindowtitle -platform)
  execute_process(
    COMMAND "${GUI}" -regtest -qml_onboarded=1 -noconf -nosettings
      "-test-settings-dir=${profile}/settings" "-datadir=${profile}"
      "${option}" "invalid-qt-option-value"
    WORKING_DIRECTORY "${profile}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE stdout
    ERROR_VARIABLE stderr
    TIMEOUT 15
  )
  if(NOT result STREQUAL "1" OR NOT stderr MATCHES "Error parsing command line arguments: Invalid parameter ${option}")
    message(FATAL_ERROR "Qt option did not reach Core's argument parser (${result}): ${stdout}${stderr}")
  endif()
endforeach()
file(REMOVE_RECURSE "${profile}")
