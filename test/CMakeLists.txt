cmake_minimum_required(VERSION 3.22)

# Tests for the QML/Qt6 app

find_package(Qt 6.2 MODULE REQUIRED COMPONENTS Core Gui Test Qml Quick QuickTest)
find_package(GTest REQUIRED)

if(NOT TARGET GTest::gmock)
  message(FATAL_ERROR "GTest::gmock target is required for app unit tests. Ensure gmock is installed and exported by the active GTest package.")
endif()

add_executable(bitcoinqml_unit_tests
  test_unit_tests_main.cpp
  gmocktestfixture.h
  test_bitcoinaddress.cpp
  test_bitcoinamount.cpp
  test_peerlistmodel.cpp
  test_peerstatsutil.cpp
  test_walletqmlmodel.cpp
  test_walletqmlmodeltransaction.cpp
  test_qmlbitcoinunits.cpp
  test_imageprovider.cpp
  test_networkstyle.cpp
  test_qmlinitexecutor_api.cpp
  test_options_model.cpp
  test_banlistmodel.cpp
  test_walletlistmodel.cpp
  test_bumptransactionmodel.cpp
  test_walletqmlcontroller.cpp
  test_display_settings.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../bitcoin/src/util/chaintype.cpp
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
    GTest::gmock
    Qt6::Core
    Qt6::Gui
    Qt6::Quick
    Qt6::Test
)

add_test(NAME bitcoinqml_unit_tests COMMAND bitcoinqml_unit_tests -platform offscreen)

add_executable(bitcoinqml_qmltests
  qml/qml_tests_main.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../qml/components/blockclockdial.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../qml/controls/linegraph.cpp
)

target_compile_definitions(bitcoinqml_qmltests
  PRIVATE
    BITCOINQML_QML_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../qml"
    QUICK_TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/qml"
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

add_test(NAME bitcoinqml_qmltests COMMAND bitcoinqml_qmltests -platform offscreen)
