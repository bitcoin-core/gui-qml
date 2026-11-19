cmake_minimum_required(VERSION 3.22)

# Tests for the QML/Qt6 app

find_package(Qt 6.2 MODULE REQUIRED COMPONENTS Core Gui Network Test Qml Quick QuickTest Widgets)
find_package(Python3 REQUIRED COMPONENTS Interpreter)

add_test(NAME qml_scenario_runner
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/functional/test_qml_scenarios.py
)

add_test(NAME bitcoinqml_cleanup_tests
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/functional/test_qml_cleanup.py
)
set_tests_properties(bitcoinqml_cleanup_tests PROPERTIES LABELS unit)

add_test(NAME bitcoinqml_process_checks
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/functional/test_qml_process_checks.py
)
add_test(NAME bitcoinqml_dev_resources
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_qml_dev_resources.py
)
add_test(NAME bitcoinqml_focused_runner
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_run_focused.py
)
add_test(NAME bitcoinqml_focused_unit_build
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_focused_unit_build.py
)
add_test(NAME bitcoinqml_tidy_selection
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_clang_tidy_selection.py
)

add_test(NAME bitcoinqml_test_directory_checks
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/functional/test_qml_test_directories.py
)

add_executable(bitcoinqml_bootstrap_tests
  test_bootstrap.cpp
  ../qml/backendexecutor.cpp
  ../qml/execution.cpp
  ../qml/quithandler.h
)
target_include_directories(bitcoinqml_bootstrap_tests PRIVATE ..)
target_link_libraries(bitcoinqml_bootstrap_tests PRIVATE Qt6::Widgets Qt6::Quick Qt6::Test sanitize_interface)
add_test(NAME bitcoinqml_bootstrap_tests COMMAND bitcoinqml_bootstrap_tests -platform minimal)
set_tests_properties(bitcoinqml_bootstrap_tests PROPERTIES TIMEOUT 30)

add_executable(bitcoinqml_unit_tests
  test_unit_tests_main.cpp
  test_persistenteditor.cpp
  test_texteditsessionmodel.cpp
  test_amountparse.cpp
  test_appmode.cpp
  test_backendexecutor.cpp
  test_backuppolicy.cpp
  test_backuppolicyoperation.cpp
  test_fileexportoperation.cpp
  test_execution.cpp
  test_guipresentation.cpp
  test_atomicsharedptr.cpp
  test_persistenttrees.cpp
  test_core_settings_model.cpp
  test_settingspreservation.cpp
  test_effectivesettings.cpp
  test_settingssession.cpp
  test_settingseditormodel.cpp
  test_setupcommit.cpp
  test_prunedraft.cpp
  test_uipreferences.cpp
  test_flowpresentation.cpp
  test_flowdetailsmodel.cpp
  test_startupeffects.cpp
  test_startuppolicy.cpp
  test_startuppreparation.cpp
  test_startuproleprofile.cpp
  test_startupprofile.cpp
  test_qtlogsink.cpp
  test_bitcoinaddress.cpp
  test_blockclockdial.cpp
  test_blockclockhistory.cpp
  test_blockclockmodel.cpp
  test_bitcoinuri.cpp
  test_bitcoinurimodel.cpp
  test_sendimportmodel.cpp
  test_qmlfiledialog.cpp
  test_bitcoinamount.cpp
  test_debuglogmodel.cpp
  test_monospaceoutputmodel.cpp
  test_desktoptrayiconcontroller.cpp
  test_desktopwindowbehaviormodel.cpp
  test_peerlistmodel.cpp
  test_peercommands.cpp
  test_peerstatsutil.cpp
  test_qmlbitcoinunits.cpp
  test_imageprovider.cpp
  test_qrimageprovider.cpp
  test_imageexportoperation.cpp
  test_qmlengine.cpp
  test_nodemodel.cpp
  test_nodelifecycle.cpp
  test_nodelifecycleworker.cpp
  test_runtimeprompt.cpp
  test_runtimepromptworker.cpp
  test_networkcommand.cpp
  test_startupdiagnosticsmodel.cpp
  test_secretinput.cpp
  test_networkstyle.cpp
  test_networkstatusmodel.cpp
  test_networktraffictower.cpp
  test_qmlinitexecutor_api.cpp
  test_shutdownproducers.cpp
  test_guiargs.cpp
  test_desktopintent.cpp
  test_desktopintenttransport.cpp
  test_options_model.cpp
  test_async_options.cpp
  test_onboarding_storage.cpp
  test_onboardingasync.cpp
  test_onboardingflow.cpp
  test_profileinspection.cpp
  test_profileinspectionprocess.cpp
  test_banlistmodel.cpp
  test_display_settings.cpp
  test_qtinfolog.cpp
  test_qtmessagefilter.cpp
  test_rpccommandexecutor.cpp
  test_rpcconsolemodel.cpp
  test_syncprogresstracker.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../bitcoin/src/util/chaintype.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../qml/models/bitcoinuri.cpp
)

if(ENABLE_TEST_AUTOMATION)
  target_sources(bitcoinqml_unit_tests PRIVATE test_testbridge.cpp)
endif()

if(ENABLE_WALLET)
  target_sources(bitcoinqml_unit_tests PRIVATE
    test_activityexportoperation.cpp
    test_profileinspectionowner.cpp
    test_activityfilterproxymodel.cpp
    test_addresslistmodel.cpp
    test_bumptransactionmodel.cpp
    test_signverifymessagemodel.cpp
    test_signertransaction.cpp
    test_signerdiscovery.cpp
    test_coinslistmodel.cpp
    test_psbtfee.cpp
    test_psbtqmlmodel.cpp
    test_psbtwork.cpp
    test_paymentimport.cpp
    test_psbtdocument.cpp
    test_receiverequesthistorymodel.cpp
    test_receiverequestoperation.cpp
    test_paymentrequesteditormodel.cpp
    test_requestsharingmodel.cpp
    test_walletreceivemodel.cpp
    test_sendrecipientslistmodel.cpp
    test_sendrecipient.cpp
    test_feeselection.cpp
    test_transaction.cpp
    test_transactionactivity.cpp
    test_transactionactivitymodel.cpp
    test_activitydetailmodel.cpp
    test_walletlistmodel.cpp
    test_walletselectionmodel.cpp
    test_walletqmlmodel.cpp
    test_walletqmlmodeltransaction.cpp
    test_walletqmlcontroller.cpp
    test_walletlifecycle.cpp
    test_walletopening.cpp
    test_walletsecurity.cpp
    test_walletsend.cpp
    test_walletprepare.cpp
    test_transactionattempt.cpp
    test_wallettransaction.cpp
    test_walletreceipts.cpp
    test_walletrequests.cpp
    test_walletreservations.cpp
    test_walletsettings.cpp
    test_walletbalanceadapter.cpp
    test_walletbalancereader.cpp
  )
endif()

include(${PROJECT_SOURCE_DIR}/cmake/FocusedUnitTests.cmake)
# The executable's early child-process entry point uses this existing provider.
configure_focused_unit_build(bitcoinqml_unit_tests
  ${CMAKE_CURRENT_BINARY_DIR}/unit-build-selection.json
  SUPPORT_SOURCES test_startupprofile.cpp)

target_compile_definitions(bitcoinqml_unit_tests
  PRIVATE
    BITCOINQML_NO_TEST_MAIN
)
set_source_files_properties(test_rpccommandexecutor.cpp PROPERTIES
  COMPILE_DEFINITIONS BITCOINQML_CORE_RPC_CONSOLE_SOURCE="${PROJECT_SOURCE_DIR}/bitcoin/src/qt/rpcconsole.cpp"
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
    $<TARGET_NAME_IF_EXISTS:bitcoin_wallet>
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

if(APPLE)
  # The native Dock regression deliberately links both app and Core metaobjects.
  target_link_libraries(bitcoinqml_unit_tests PRIVATE bitcoinqt)
endif()

add_test(NAME bitcoinqml_unit_tests COMMAND bitcoinqml_unit_tests -platform minimal)
if(APP_UNIT_TEST_CLASSES)
  set_tests_properties(bitcoinqml_unit_tests PROPERTIES LABELS "unit;focused-build")
endif()
add_test(NAME bitcoinqml_unit_targeting
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/functional/test_qml_unit_targeting.py
    $<TARGET_FILE:bitcoinqml_unit_tests>
)
set_tests_properties(bitcoinqml_unit_targeting PROPERTIES TIMEOUT 120)

add_test(NAME bitcoinqml_fatal_exception_tests
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/functional/qml_test_fatal_exceptions.py
    $<TARGET_FILE:bitcoinqml_unit_tests>
)
set_tests_properties(bitcoinqml_fatal_exception_tests PROPERTIES TIMEOUT 120)

if(ENABLE_TEST_AUTOMATION AND UNIX)
  add_test(NAME bitcoinqml_configured_datadir_tests
    COMMAND ${CMAKE_COMMAND} -E env BITCOIN_CORE_APP=$<TARGET_FILE:bitcoin-core-app>
      ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/functional/qml_test_configured_datadir.py
  )
  set_tests_properties(bitcoinqml_configured_datadir_tests PROPERTIES
    TIMEOUT 180
    ENVIRONMENT "QML_TEST_QT_VERSION=${Qt6_VERSION}"
  )

  add_test(NAME bitcoinqml_fatal_shutdown_tests
    COMMAND ${CMAKE_COMMAND} -E env BITCOIN_CORE_APP=$<TARGET_FILE:bitcoin-core-app>
      ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/functional/interface_qml_shutdown.py --case "fatal-*"
  )
  set_tests_properties(bitcoinqml_fatal_shutdown_tests PROPERTIES
    TIMEOUT 180
    ENVIRONMENT "QML_TEST_QT_VERSION=${Qt6_VERSION}"
  )
endif()

option(QML_TEST_DEV_MODE "Load QML tests and components from a live source mirror" OFF)
set(qml_test_manifests ${QML_QRC} ${CMAKE_CURRENT_SOURCE_DIR}/qml/bitcoin_qmltests.qrc)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${qml_test_manifests})
set(qml_test_source_dir "qrc:/qml")
set(qml_test_input_dir ":/test/qml")
if(QML_TEST_DEV_MODE)
  set(qml_test_mirror "${CMAKE_CURRENT_BINARY_DIR}/qml-dev")
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/qml/prepare_dev_resources.py)
  execute_process(
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/qml/prepare_dev_resources.py
      --output ${qml_test_mirror} ${qml_test_manifests}
    COMMAND_ERROR_IS_FATAL ANY
  )
  set(qml_test_source_dir "${qml_test_mirror}/qml")
  set(qml_test_input_dir "${qml_test_mirror}/test/qml")
  qt6_add_resources(QMLTESTS_QRC_CPP ${qml_test_mirror}/assets.qrc)
else()
  qt6_add_resources(QMLTESTS_QRC_CPP ${qml_test_manifests})
endif()

add_executable(bitcoinqml_qmltests
  qml/qml_tests_main.cpp
  qmlcoverage.cpp
  mocks/receiveqmlfixture.h
  ${CMAKE_CURRENT_SOURCE_DIR}/../qml/models/messagesigningprompt.h
  ${CMAKE_CURRENT_SOURCE_DIR}/../qml/models/sendimportports.h
  ${CMAKE_CURRENT_SOURCE_DIR}/../qml/models/sendimportmodel.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../qml/models/sendimportmodel.h
  ${CMAKE_CURRENT_SOURCE_DIR}/../qml/models/exacttextdocument.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../qml/models/exacttextdocument.h
  ${CMAKE_CURRENT_SOURCE_DIR}/../qml/models/texteditsessionmodel.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../qml/models/texteditsessionmodel.h
  ${CMAKE_CURRENT_SOURCE_DIR}/../qml/models/paymentrequesteditormodel.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../qml/models/paymentrequesteditormodel.h
  ${CMAKE_CURRENT_SOURCE_DIR}/../qml/models/receiptreconciliationmodel.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../qml/models/receiptreconciliationmodel.h
  ${CMAKE_CURRENT_SOURCE_DIR}/../qml/models/receivereservationmodel.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../qml/models/receivereservationmodel.h
  ${CMAKE_CURRENT_SOURCE_DIR}/../qml/models/walletreceivemodel.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../qml/models/walletreceivemodel.h
  ${CMAKE_CURRENT_SOURCE_DIR}/../qml/models/requestsharingmodel.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../qml/models/requestsharingmodel.h
  ${CMAKE_CURRENT_SOURCE_DIR}/../qml/models/requestimageexportoperation.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../qml/models/requestimageexportoperation.h
  ${CMAKE_CURRENT_SOURCE_DIR}/../qml/models/activityexportoperation.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../qml/models/activityexportoperation.h
  ${CMAKE_CURRENT_SOURCE_DIR}/../qml/models/backuppolicyoperation.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../qml/models/backuppolicyoperation.h
  ${CMAKE_CURRENT_SOURCE_DIR}/../qml/models/backuppolicy.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../qml/walletrequesterror.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../qml/walletrequesterror.h
  ${CMAKE_CURRENT_SOURCE_DIR}/../qml/requestsharing.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../qml/requestsharing.h
  ${CMAKE_CURRENT_SOURCE_DIR}/../qml/components/ownedqrimage.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../qml/components/ownedqrimage.h
  ${CMAKE_CURRENT_SOURCE_DIR}/../qml/models/feeselectionmodel.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../qml/models/feeselectionmodel.h
  ${CMAKE_CURRENT_SOURCE_DIR}/../qml/signerdiscovery.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../qml/onboarding_storage.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../qml/settingsvalues.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../qml/settingssession.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../qml/models/settingseditormodel.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../qml/signerconfiguration.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../qml/models/signerdiscoverymodel.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../qml/walletcreation.h
  ${CMAKE_CURRENT_SOURCE_DIR}/../qml/models/walletoperationpresentationmodel.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../qml/models/walletoperationpresentationmodel.h
  ${CMAKE_CURRENT_SOURCE_DIR}/../qml/flowpresentation.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../qml/models/monospaceoutputmodel.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../qml/models/monospaceoutputmodel.h
  ${CMAKE_CURRENT_SOURCE_DIR}/../qml/models/bitcoinaddress.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../qml/models/bitcoinaddress.h
  ${CMAKE_CURRENT_SOURCE_DIR}/../qml/models/uipreferences.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../qml/models/uipreferences.h
  ${CMAKE_CURRENT_SOURCE_DIR}/../qml/backendexecutor.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../qml/execution.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../qml/amountparse.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../qml/bitcoinamount.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../qml/bitcoinunits.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../qml/buttonkeyactivation.h
  ${CMAKE_CURRENT_SOURCE_DIR}/../qml/components/blockclockdial.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../qml/presentationservices.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../qml/presentationservices.h
  ${CMAKE_CURRENT_SOURCE_DIR}/../qml/controls/linegraph.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../qml/qrimageprovider.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../qml/globalsettings.h
  ${CMAKE_CURRENT_SOURCE_DIR}/../qml/motionpreference.h
  ${QMLTESTS_QRC_CPP}
)

target_compile_definitions(bitcoinqml_qmltests
  PRIVATE
    BITCOINQML_QML_SOURCE_DIR="${qml_test_source_dir}"
    QUICK_TEST_SOURCE_DIR="${qml_test_input_dir}"
    BITCOINQML_QML_TEST_MOCKS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/mocks/qml"
    BITCOINQML_QML_TEST_DEV_MODE=$<BOOL:${QML_TEST_DEV_MODE}>
)

target_link_libraries(bitcoinqml_qmltests
  PRIVATE
    core_interface
    Qt6::Core
    Qt6::Qml
    Qt6::Quick
    Qt6::QuickTest
    Qt6::Test
    Qt6::Svg
    QRencode::QRencode
    sanitize_interface
)

target_include_directories(bitcoinqml_qmltests
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_CURRENT_SOURCE_DIR}/../bitcoin/src
    ${PROJECT_BINARY_DIR}/bitcoin/src
)

set(QML_TEST_RENDERING_PLATFORM "minimal" CACHE STRING "Platform used by the QML component and warning tests")
set(qml_test_platform "${QML_TEST_RENDERING_PLATFORM}")
option(QML_TEST_USE_XVFB "Run each QML component and warning test on its own Xvfb display" OFF)
set(qml_test_launcher)
if(QML_TEST_USE_XVFB)
  find_program(QML_TEST_XVFB_RUN NAMES xvfb-run REQUIRED)
  set(qml_test_launcher ${QML_TEST_XVFB_RUN} -a)
endif()
# Use the resource manifest as the authoritative list in both modes. Each file
# gets its own process, so CTest can select and schedule tests independently.
file(STRINGS ${CMAKE_CURRENT_SOURCE_DIR}/qml/bitcoin_qmltests.qrc qml_test_entries
  REGEX "<file>tst_.*[.]qml</file>")
set(qml_test_names)
foreach(entry IN LISTS qml_test_entries)
  string(REGEX REPLACE ".*<file>([^<]+)</file>.*" "\\1" filename "${entry}")
  string(REGEX REPLACE "^tst_|[.]qml$" "" test_name "${filename}")
  set(test_name "bitcoinqml_qml_${test_name}")
  if(test_name IN_LIST qml_test_names)
    message(FATAL_ERROR "Duplicate QML component CTest name: ${test_name}")
  endif()
  add_test(NAME ${test_name} COMMAND ${qml_test_launcher} $<TARGET_FILE:bitcoinqml_qmltests> -platform ${qml_test_platform}
    -input "${qml_test_input_dir}/${filename}")
  list(APPEND qml_test_names ${test_name})
endforeach()
if(NOT qml_test_names)
  message(FATAL_ERROR "QML test manifest selected no component test files")
endif()
# Africa/Cairo moved its clocks from 00:00 to 01:00 on 2026-04-24, so that
# local midnight does not exist. The calendar must still draw that day (#1128).
add_test(NAME bitcoinqml_calendar_midnight_dst COMMAND ${qml_test_launcher} $<TARGET_FILE:bitcoinqml_qmltests> -platform ${qml_test_platform}
  -input "${qml_test_input_dir}/tst_activitylist.qml" "ActivityList::test_calendar_draws_every_day_of_every_month_once")
# Seed the longest suites so clean builds do not need timing history to schedule
# them first. These are relative scheduling hints, not timeouts or assertions.
set_tests_properties(bitcoinqml_qml_sendcreate bitcoinqml_qml_requestpayment PROPERTIES COST 20)
set_tests_properties(bitcoinqml_qml_activitylist PROPERTIES COST 16)

# Qt 6.2 has no QTest::failOnWarning(); leave this check disabled there too,
# matching the component runner's version guard.
if(Qt6_VERSION VERSION_GREATER_EQUAL 6.3)
  # console.warn() does not emit QQmlEngine::warnings. Also reject messages
  # printed outside a test function, before failOnWarning() is installed.
  set_tests_properties(${qml_test_names} PROPERTIES
    FAIL_REGULAR_EXPRESSION "(QWARN|QCRITICAL)[^\r\n]*qml:|\\[(warning|critical)\\] qml:")
endif()

add_test(NAME bitcoinqml_qmlwarning_tests
  COMMAND ${qml_test_launcher} ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/qml/test_warnings.py
    $<TARGET_FILE:bitcoinqml_qmltests> ${Qt6_VERSION} ${qml_test_platform} ${PROJECT_BINARY_DIR}
    "${qml_test_input_dir}/warning_probe.qml"
)
set_tests_properties(bitcoinqml_qmlwarning_tests PROPERTIES TIMEOUT 120 LABELS qml
  ENVIRONMENT "QT_QUICK_BACKEND=software;QML_DISABLE_DISK_CACHE=1;BITCOIN_QML_TEST_NO_ANIMATIONS=1")

set(gui_test_environment "QT_QPA_PLATFORM=minimal;QT_QUICK_BACKEND=software;QML_DISABLE_DISK_CACHE=1;BITCOIN_QML_TEST_NO_ANIMATIONS=1")
set_tests_properties(bitcoinqml_unit_tests bitcoinqml_bootstrap_tests bitcoinqml_fatal_exception_tests bitcoinqml_test_directory_checks
  bitcoinqml_process_checks bitcoinqml_dev_resources bitcoinqml_focused_runner bitcoinqml_tidy_selection
  bitcoinqml_unit_targeting PROPERTIES LABELS unit)
set_tests_properties(${qml_test_names} PROPERTIES LABELS qml TIMEOUT 120)
set_tests_properties(bitcoinqml_unit_tests bitcoinqml_bootstrap_tests ${qml_test_names} PROPERTIES
  ENVIRONMENT "${gui_test_environment}")
set_property(TEST ${qml_test_names} PROPERTY ENVIRONMENT
  "QT_QPA_PLATFORM=${qml_test_platform};QT_QUICK_BACKEND=software;QML_DISABLE_DISK_CACHE=1;BITCOIN_QML_TEST_NO_ANIMATIONS=1")
# Include severity and category so the console warning check can distinguish
# console.warn() from console.log() during QML loading and teardown.
set_property(TEST ${qml_test_names} APPEND PROPERTY ENVIRONMENT
  "QT_MESSAGE_PATTERN=[%{type}] %{category}: %{message}")
# The DST entry runs one component case like the others, in a fixed zone. It is
# kept out of qml_test_names and the bitcoinqml_qml_ prefix, whose warning
# policy must be identical with no extra environment, and copies
# that policy here.
list(GET qml_test_names 0 qml_policy_source)
foreach(property IN ITEMS ENVIRONMENT FAIL_REGULAR_EXPRESSION LABELS TIMEOUT)
  get_property(value TEST ${qml_policy_source} PROPERTY ${property})
  if(value)
    set_property(TEST bitcoinqml_calendar_midnight_dst PROPERTY ${property} "${value}")
  endif()
endforeach()
set_property(TEST bitcoinqml_calendar_midnight_dst APPEND PROPERTY ENVIRONMENT "TZ=Africa/Cairo")

get_target_property(qt_lib_type Qt6::Core TYPE)
if(qt_lib_type STREQUAL "STATIC_LIBRARY")
  qt6_import_plugins(bitcoinqml_bootstrap_tests INCLUDE Qt6::QMinimalIntegrationPlugin)
  qt6_import_plugins(bitcoinqml_unit_tests INCLUDE Qt6::QMinimalIntegrationPlugin)
  qt6_import_qml_plugins(bitcoinqml_unit_tests)
  qt6_import_plugins(bitcoinqml_qmltests INCLUDE Qt6::QMinimalIntegrationPlugin Qt6::QSvgPlugin Qt6::QSvgIconPlugin)
  qt6_import_qml_plugins(bitcoinqml_qmltests)
  target_link_libraries(bitcoinqml_qmltests PRIVATE
    Qt6::LabsPlatformplugin
    Qt6::qtquickdialogsplugin
  )
  if(Qt6_VERSION VERSION_GREATER_EQUAL 6.5)
    target_link_libraries(bitcoinqml_qmltests PRIVATE Qt6::qtqmlcoreplugin)
  else()
    target_link_libraries(bitcoinqml_qmltests PRIVATE Qt6::qmlsettingsplugin)
  endif()
endif()

add_subdirectory(integration)

if(TARGET bitcoin-core-profile-inspector)
  add_dependencies(bitcoinqml_unit_tests bitcoin-core-profile-inspector)
  add_dependencies(bitcoinqml_integration_tests bitcoin-core-profile-inspector)
endif()
