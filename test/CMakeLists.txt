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
)

target_include_directories(bitcoinqml_qmltests
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

add_test(NAME bitcoinqml_qmltests COMMAND bitcoinqml_qmltests -platform minimal)

get_target_property(qt_lib_type Qt6::Core TYPE)
if(qt_lib_type STREQUAL "STATIC_LIBRARY")
  qt6_import_plugins(bitcoinqml_unit_tests INCLUDE Qt6::QMinimalIntegrationPlugin)
  qt6_import_qml_plugins(bitcoinqml_unit_tests)
  qt6_import_plugins(bitcoinqml_qmltests INCLUDE Qt6::QMinimalIntegrationPlugin)
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
