# Copyright (c) 2026 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or https://opensource.org/license/mit/.

add_executable(test_bitcoin-qt
  qml/qml_tests_main.cpp
  ../../init/bitcoin-qt.cpp
  test_application.cpp
  test_nodemodel.cpp
  test_testbridge.cpp
  test_unit_tests_main.cpp
)
add_windows_application_manifest(test_bitcoin-qt)

qt6_add_resources(test_bitcoin-qt qml_tests
  PREFIX /test/qml
  FILES qml/tst_mainwindow.qml
)
# See: https://bugreports.qt.io/browse/QTBUG-141858.
get_target_property(warn_flags warn_interface INTERFACE_COMPILE_OPTIONS)
if("-Wtrailing-whitespace=any" IN_LIST warn_flags)
  set_source_files_properties(${CMAKE_CURRENT_BINARY_DIR}/.qt/rcc/qrc_qml_tests.cpp PROPERTIES COMPILE_OPTIONS -Wno-trailing-whitespace)
endif()
unset(warn_flags)

target_compile_definitions(test_bitcoin-qt
  PRIVATE
    QUICK_TEST_SOURCE_DIR=":/test/qml"
)

target_link_libraries(test_bitcoin-qt
  PRIVATE
    core_interface
    bitcoinqml
    test_util
    bitcoin_node
    $<TARGET_NAME_IF_EXISTS:bitcoin_wallet>
    Qt6::QuickTest
    Qt6::Test
)

get_target_property(qt_qml_test_lib_type Qt6::Core TYPE)
if(qt_qml_test_lib_type STREQUAL "STATIC_LIBRARY")
  target_link_libraries(test_bitcoin-qt PRIVATE Qt6::qtquickcontrols2basicstyleplugin)
endif()

import_plugins(test_bitcoin-qt)
qt6_import_qml_plugins(test_bitcoin-qt)

add_test(NAME test_bitcoin-qt
  COMMAND test_bitcoin-qt --suite=unit
)

add_test(NAME test_bitcoin-qt-qml
  COMMAND test_bitcoin-qt --suite=qml -platform minimal
)

add_test(NAME test_bitcoin-qt-integration
  COMMAND test_bitcoin-qt --suite=integration -platform minimal
)

set_property(
  TEST
    test_bitcoin-qt
    test_bitcoin-qt-qml
    test_bitcoin-qt-integration
  APPEND PROPERTY
    ENVIRONMENT_MODIFICATION
      QT_FATAL_WARNINGS=set:1
      QML_DISABLE_DISK_CACHE=set:1
)

install_binary_component(test_bitcoin-qt INTERNAL)
