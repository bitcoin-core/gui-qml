# Copyright (c) 2026 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or https://opensource.org/license/mit/.

add_executable(test_bitcoin-qt
  qml/qml_tests_main.cpp
  ../../init/bitcoin-qt.cpp
  test_application.cpp
  test_applicationrouter.cpp
  test_translationmanager.cpp
  test_console_integration.cpp
  test_navigation_integration.cpp
  test_onboarding_integration.cpp
  test_node_integration.cpp
  test_banlistmodel.cpp
  test_blockclockdial.cpp
  test_desktopwindowbehaviormodel.cpp
  test_display_settings.cpp
  test_debuglog.cpp
  test_guiargs.cpp
  test_networkstatusmodel.cpp
  test_networktraffictower.cpp
  test_nodemodel.cpp
  test_noderuntimemodels.cpp
  test_runtimedialogmodel.cpp
  test_mempoolmodel.cpp
  test_onboarding_storage.cpp
  test_onboarding_profile.cpp
  test_startupsettings.cpp
  test_peerlistmodel.cpp
  test_peerstatsutil.cpp
  test_rpccommandexecutor.cpp
  test_testbridge.cpp
  test_unit_tests_main.cpp
)
add_windows_application_manifest(test_bitcoin-qt)

qt6_add_resources(test_bitcoin-qt qml_tests
  PREFIX /test
  FILES
    qml/tst_appfolderdialog.qml
    qml/tst_blockclock.qml
    qml/tst_globalmenu.qml
    qml/tst_navbutton.qml
    qml/tst_proxylocationinput.qml
    qml/tst_valueinput.qml
)
# See: https://bugreports.qt.io/browse/QTBUG-141858.
get_target_property(warn_flags warn_interface INTERFACE_COMPILE_OPTIONS)
if("-Wtrailing-whitespace=any" IN_LIST warn_flags)
  set_source_files_properties(${CMAKE_CURRENT_BINARY_DIR}/.qt/rcc/qrc_qml_tests.cpp PROPERTIES COMPILE_OPTIONS -Wno-trailing-whitespace)
endif()
unset(warn_flags)

target_compile_definitions(test_bitcoin-qt
  PRIVATE
    BITCOINQML_NO_TEST_MAIN
    QUICK_TEST_SOURCE_DIR=":/test/qml"
)

target_link_libraries(test_bitcoin-qt
  PRIVATE
    core_interface
    bitcoinqml
    test_util
    bitcoin_node
    Qt6::QuickTest
    Qt6::Test
)

get_target_property(qt_qml_test_lib_type Qt6::Core TYPE)
if(qt_qml_test_lib_type STREQUAL "STATIC_LIBRARY")
  target_link_libraries(test_bitcoin-qt PRIVATE
    Qt6::qquicklayoutsplugin
    Qt6::qtqmlcoreplugin
    Qt6::qtquickcontrols2basicstyleimplplugin
    Qt6::qtquickcontrols2basicstyleplugin
    Qt6::qtquickcontrols2implplugin
    Qt6::qtquickcontrols2plugin
    Qt6::qtquickdialogsplugin
  )
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

# The ordinary-startup test also needs the production executable when only
# the Qt test target is requested.
add_dependencies(test_bitcoin-qt bitcoin-qt)
add_test(NAME test_bitcoin-qt-startup
  COMMAND ${CMAKE_COMMAND}
    -DGUI=$<TARGET_FILE:bitcoin-qt>
    -DTEST_ROOT=${CMAKE_CURRENT_BINARY_DIR}/startup-test
    -P ${CMAKE_CURRENT_SOURCE_DIR}/startup_invalid_datadir.cmake
)
set_tests_properties(test_bitcoin-qt-startup PROPERTIES
  TIMEOUT 70
  ENVIRONMENT "QT_QPA_PLATFORM=minimal;QML_DISABLE_DISK_CACHE=1"
)

set_property(
  TEST
    test_bitcoin-qt
    test_bitcoin-qt-qml
  APPEND PROPERTY
    ENVIRONMENT_MODIFICATION
      QT_FATAL_WARNINGS=set:1
)

# Avoid stale compiled QML from another worktree. The minimal platform plugin
# may also report unavailable desktop services while constructing the system
# tray controller, so keep integration warnings visible but non-fatal.
set_property(
  TEST
    test_bitcoin-qt
    test_bitcoin-qt-qml
    test_bitcoin-qt-integration
  APPEND PROPERTY
    ENVIRONMENT_MODIFICATION
      QML_DISABLE_DISK_CACHE=set:1
)

install_binary_component(test_bitcoin-qt INTERNAL)
