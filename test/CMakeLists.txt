cmake_minimum_required(VERSION 3.22)

# Tests for the QML/Qt6 app

find_package(Qt 6.2 MODULE REQUIRED COMPONENTS Core Gui Network Test Qml Quick QuickTest Widgets)

add_executable(bitcoinqml_unit_tests
  test_unit_tests_main.cpp
  test_bitcoinaddress.cpp
  test_blockclockdial.cpp
  test_bitcoinuri.cpp
  test_bitcoinamount.cpp
  test_debuglogmodel.cpp
  test_desktoptrayiconcontroller.cpp
  test_desktopwindowbehaviormodel.cpp
  test_peerlistmodel.cpp
  test_peerstatsutil.cpp
  test_psbtqmlmodel.cpp
  test_walletqmlmodel.cpp
  test_walletqmlmodeltransaction.cpp
  test_qmlbitcoinunits.cpp
  test_imageprovider.cpp
  test_nodemodel.cpp
  test_networkstyle.cpp
  test_networkstatusmodel.cpp
  test_networktraffictower.cpp
  test_qmlinitexecutor_api.cpp
  test_guiargs.cpp
  test_options_model.cpp
  test_onboarding_storage.cpp
  test_banlistmodel.cpp
  test_walletlistmodel.cpp
  test_bumptransactionmodel.cpp
  test_walletqmlcontroller.cpp
  test_display_settings.cpp
  test_addresslistmodel.cpp
  test_receiverequesthistorymodel.cpp
  test_transaction.cpp
  test_transactionactivity.cpp
  test_transactionactivitymodel.cpp
  test_activityfilterproxymodel.cpp
  test_qtinfolog.cpp
  test_qtmessagefilter.cpp
  test_rpccommandexecutor.cpp
  test_rpcconsolemodel.cpp
  test_sendrecipientslistmodel.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../bitcoin/src/util/chaintype.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../qml/models/bitcoinuri.cpp
)

target_compile_definitions(bitcoinqml_unit_tests
  PRIVATE
    BITCOINQML_NO_TEST_MAIN
)

target_include_directories(bitcoinqml_unit_tests
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_CURRENT_SOURCE_DIR}/../bitcoin/src
    ${CMAKE_CURRENT_SOURCE_DIR}/../bitcoin/src/univalue/include
    ${CMAKE_CURRENT_BINARY_DIR}/../bitcoin/src
)

target_link_libraries(bitcoinqml_unit_tests
  PRIVATE
    bitcoinqml
    bitcoin_wallet
    bitcoin_common
    bitcoin_util
    bitcoin_node
    Qt6::Core
    Qt6::Gui
    Qt6::Quick
    Qt6::Network
    Qt6::Test
    Qt6::Widgets
)

add_test(NAME bitcoinqml_unit_tests COMMAND bitcoinqml_unit_tests -platform minimal)

qt6_add_resources(QMLTESTS_QRC_CPP ${QML_QRC} qml/bitcoin_qmltests.qrc)

add_executable(bitcoinqml_qmltests
  qml/qml_tests_main.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../qml/components/blockclockdial.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../qml/controls/linegraph.cpp
  ${QMLTESTS_QRC_CPP}
)

target_compile_definitions(bitcoinqml_qmltests
  PRIVATE
    BITCOINQML_QML_SOURCE_DIR="qrc:/qml"
    QUICK_TEST_SOURCE_DIR=":/test/qml"
    BITCOINQML_QML_TEST_MOCKS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/mocks/qml"
)

target_link_libraries(bitcoinqml_qmltests
  PRIVATE
    Qt6::Core
    Qt6::Qml
    Qt6::Quick
    Qt6::QuickTest
    Qt6::Svg
)

target_include_directories(bitcoinqml_qmltests
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

add_test(NAME bitcoinqml_qmltests COMMAND bitcoinqml_qmltests -platform minimal)

add_executable(bitcoinqml_integration_tests
  test_application.cpp
  thread_audit.cpp
  test_navigation_integration.cpp
  test_node_integration.cpp
  test_onboarding_integration.cpp
)
target_include_directories(bitcoinqml_integration_tests PRIVATE
  ${PROJECT_SOURCE_DIR}
  ${PROJECT_SOURCE_DIR}/bitcoin/src
  ${PROJECT_BINARY_DIR}/bitcoin/src
)
target_link_libraries(bitcoinqml_integration_tests PRIVATE bitcoinqml bitcoinqt Qt6::Test)
qt6_import_qml_plugins(bitcoinqml_integration_tests)
add_test(NAME bitcoinqml_integration_tests COMMAND bitcoinqml_integration_tests)
add_executable(bitcoinqml_startup_tests test_startup.cpp thread_audit.cpp)
target_include_directories(bitcoinqml_startup_tests PRIVATE
  ${PROJECT_SOURCE_DIR}
  ${PROJECT_SOURCE_DIR}/bitcoin/src
  ${PROJECT_BINARY_DIR}/bitcoin/src
)
target_link_libraries(bitcoinqml_startup_tests PRIVATE bitcoinqml bitcoinqt Qt6::Test)
qt6_import_qml_plugins(bitcoinqml_startup_tests)
add_test(NAME bitcoinqml_startup_tests COMMAND bitcoinqml_startup_tests)
set_tests_properties(bitcoinqml_startup_tests PROPERTIES
  TIMEOUT 360
  ENVIRONMENT "QT_QPA_PLATFORM=minimal;QT_QUICK_BACKEND=software;QML_DISABLE_DISK_CACHE=1"
)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_test(NAME bitcoinqml_thread_policy COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/generate_thread_audit.py)
add_executable(bitcoinqml_thread_tests test_thread_audit.cpp thread_audit.cpp)
target_include_directories(bitcoinqml_thread_tests PRIVATE
  ${PROJECT_SOURCE_DIR}
  ${PROJECT_SOURCE_DIR}/bitcoin/src
  ${PROJECT_BINARY_DIR}/bitcoin/src
)
target_link_libraries(bitcoinqml_thread_tests PRIVATE bitcoinqml bitcoinqt Qt6::Test)
add_test(NAME bitcoinqml_thread_tests COMMAND bitcoinqml_thread_tests)
set_tests_properties(bitcoinqml_thread_tests PROPERTIES TIMEOUT 90)
add_executable(bitcoinqml_audited_app audited_application.cpp thread_audit.cpp)
target_include_directories(bitcoinqml_audited_app PRIVATE
  ${PROJECT_SOURCE_DIR}
  ${PROJECT_SOURCE_DIR}/bitcoin/src
  ${PROJECT_BINARY_DIR}/bitcoin/src
)
target_link_libraries(bitcoinqml_audited_app PRIVATE bitcoinqml bitcoinqt Qt6::Test)
qt6_import_qml_plugins(bitcoinqml_audited_app)
foreach(app bitcoin-core-app bitcoinqml_audited_app)
  add_test(NAME ${app}_lifecycle COMMAND ${Python3_EXECUTABLE}
    ${CMAKE_CURRENT_SOURCE_DIR}/functional/gui_process_check.py
    $<TARGET_FILE:${app}> ${CMAKE_CURRENT_BINARY_DIR}/${app}-lifecycle.json
    --qt-version ${Qt6_VERSION})
  set_tests_properties(${app}_lifecycle PROPERTIES TIMEOUT 120)
endforeach()
add_test(NAME bitcoinqml_stderr_check COMMAND ${Python3_EXECUTABLE}
  ${CMAKE_CURRENT_SOURCE_DIR}/functional/test_gui_process_check.py)
if(ENABLE_TEST_AUTOMATION)
  add_test(NAME bitcoinqml_audited_python COMMAND ${Python3_EXECUTABLE}
    ${CMAKE_CURRENT_SOURCE_DIR}/functional/qml_test_disablewallet_boot.py --node-only)
  set_tests_properties(bitcoinqml_audited_python PROPERTIES
    TIMEOUT 120
    ENVIRONMENT "BITCOIN_CORE_APP=$<TARGET_FILE:bitcoinqml_audited_app>;QML_TEST_CHECK_EXIT=1;QT_QPA_PLATFORM=minimal;QT_QUICK_BACKEND=software;QML_DISABLE_DISK_CACHE=1"
  )
endif()
set_tests_properties(bitcoinqml_integration_tests PROPERTIES
  TIMEOUT 120
  ENVIRONMENT "QT_QPA_PLATFORM=minimal;QT_QUICK_BACKEND=software;QML_DISABLE_DISK_CACHE=1"
)

if(SANITIZERS)
  # Keep the pinned Core suppressions and add the QML dependency suppression
  # used by staging, without modifying the Core submodule.
  file(READ "${PROJECT_SOURCE_DIR}/bitcoin/test/sanitizer_suppressions/ubsan" core_ubsan_suppressions)
  file(READ "${CMAKE_CURRENT_SOURCE_DIR}/sanitizer_suppressions/ubsan" qml_ubsan_suppressions)
  file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/ubsan_suppressions" "${core_ubsan_suppressions}\n${qml_ubsan_suppressions}")
  set_property(TEST bitcoinqml_unit_tests bitcoinqml_qmltests bitcoinqml_integration_tests bitcoinqml_startup_tests bitcoinqml_thread_tests bitcoin-core-app_lifecycle bitcoinqml_audited_app_lifecycle
    APPEND PROPERTY ENVIRONMENT_MODIFICATION
      "UBSAN_OPTIONS=string_append::suppressions=${CMAKE_CURRENT_BINARY_DIR}/ubsan_suppressions:print_stacktrace=1:halt_on_error=1"
  )
  if(ENABLE_TEST_AUTOMATION)
    set_property(TEST bitcoinqml_audited_python APPEND PROPERTY ENVIRONMENT_MODIFICATION
      "UBSAN_OPTIONS=string_append::suppressions=${CMAKE_CURRENT_BINARY_DIR}/ubsan_suppressions:print_stacktrace=1:halt_on_error=1")
  endif()
endif()

get_target_property(qt_lib_type Qt6::Core TYPE)
if(qt_lib_type STREQUAL "STATIC_LIBRARY")
  # The production binary also runs headlessly in the lifecycle test.
  qt6_import_plugins(bitcoin-core-app INCLUDE Qt6::QMinimalIntegrationPlugin)
  qt6_import_plugins(bitcoinqml_unit_tests INCLUDE Qt6::QMinimalIntegrationPlugin)
  qt6_import_qml_plugins(bitcoinqml_unit_tests)
  qt6_import_qml_plugins(bitcoinqml_qmltests)
  foreach(app bitcoinqml_qmltests bitcoinqml_audited_app bitcoinqml_integration_tests bitcoinqml_startup_tests)
    qt6_import_plugins(${app} INCLUDE Qt6::QMinimalIntegrationPlugin Qt6::QSvgPlugin Qt6::QSvgIconPlugin)
    # These imports live in application resources outside the test source tree.
    target_link_libraries(${app} PRIVATE Qt6::LabsPlatformplugin Qt6::qtquickdialogsplugin)
    if(Qt6_VERSION VERSION_GREATER_EQUAL 6.5)
      target_link_libraries(${app} PRIVATE Qt6::qtqmlcoreplugin)
    else()
      target_link_libraries(${app} PRIVATE Qt6::qmlsettingsplugin)
    endif()
  endforeach()
endif()
