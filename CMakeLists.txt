cmake_minimum_required(VERSION 3.22)

project(BitcoinCoreApp
  DESCRIPTION "Bitcoin GUI"
  HOMEPAGE_URL "https://bitcoincore.org/"
  LANGUAGES CXX
)

include(CMakeDependentOption)

# Language setup
if(CMAKE_SYSTEM_NAME STREQUAL "Darwin" AND NOT CMAKE_HOST_APPLE)
  # We do not use the install_name_tool when cross-compiling for macOS.
  # So disable this tool check in further enable_language() commands.
  set(CMAKE_PLATFORM_HAS_INSTALLNAME FALSE)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Set top-level target output locations.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/lib)

# Include Find*.cmake files
list(APPEND CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR}/cmake/module)

# Qt dependencies QML
set(qt_components Core Gui LinguistTools Widgets Network Qml Quick QuickControls2 Svg)
find_package(Qt 6.2 MODULE REQUIRED
    COMPONENTS ${qt_components}
)
find_package(QRencode MODULE REQUIRED)

option(ENABLE_TEST_AUTOMATION "Enable test automation bridge for QML UI testing" OFF)
option(ENABLE_QML_LINT "Enable strict QML static analysis (Qt 6.10 or newer)" OFF)
option(ENABLE_APP_COVERAGE "Instrument app and app tests with LLVM source coverage" OFF)
if(ENABLE_APP_COVERAGE)
  if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    message(FATAL_ERROR "ENABLE_APP_COVERAGE requires Clang and matching LLVM profile tools")
  endif()
  include(cmake/AppCoverage.cmake)
endif()

# Do not build any executable targets from bitcoin submodule
set(BUILD_BENCH OFF)
set(BUILD_BITCOIN_BIN OFF)
option(BUILD_CLI "Build bitcoin-cli alongside the app" OFF)
option(BUILD_DAEMON "Build bitcoind daemon alongside the app (required for functional peer tests)" OFF)
set(BUILD_FOR_FUZZING OFF)
set(BUILD_FUZZ_BINARY OFF)
set(BUILD_GUI_TESTS OFF)
set(BUILD_KERNEL_LIB OFF)
set(BUILD_SHARED_LIBS OFF)
set(BUILD_TESTS OFF)
set(BUILD_TX OFF)
set(BUILD_UTIL OFF)
set(BUILD_UTIL_CHAINSTATE OFF)
set(BUILD_WALLET_TOOL OFF)
set(USE_QRCODE TRUE)
# We need this libraries, can ignore the executable bitcoin-qt
set(BUILD_GUI ON)
option(ENABLE_WALLET "Build wallet support" ON)
cmake_dependent_option(BUILD_APP_TESTS "Build tests for the app" ON "BUILD_GUI" OFF)

# Bitcoin Core codebase
# Builds libraries: univalue, core_interface, bitcoin_node, bitcoin_wallet
add_subdirectory(bitcoin)

# Match Core's compiler launchers, DEBUG_LOCKORDER and sanitizer settings.
# Core configures ccache in its directory; propagate it to the app and tests.
foreach(rule CMAKE_C_COMPILER_LAUNCHER CMAKE_CXX_COMPILER_LAUNCHER
    CMAKE_CXX_COMPILE_OBJECT CMAKE_CXX_CREATE_SHARED_LIBRARY CMAKE_CXX_LINK_EXECUTABLE)
  get_directory_property(${rule} DIRECTORY bitcoin DEFINITION ${rule})
endforeach()

# Qt-specific commands
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOMOC_MOC_OPTIONS "-p${CMAKE_CURRENT_SOURCE_DIR}")
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)
# but don't let Qt interfere with bitcoin core libraries
if(ENABLE_WALLET)
  set_target_properties(bitcoin_wallet PROPERTIES AUTOUIC OFF)
endif()

# Compile Qt+QML sources
set(CMAKE_AUTOMOC_MOC_OPTIONS "-I${CMAKE_CURRENT_SOURCE_DIR}/qml")
file(GLOB_RECURSE QML_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/qml/*.cpp" "${CMAKE_CURRENT_SOURCE_DIR}/qml/*.h")
list(FILTER QML_SOURCES EXCLUDE REGEX "androidnotifier\\.(cpp|h)$")
# Wallet implementations and their moc objects must not enter a node-only link.
if(NOT ENABLE_WALLET)
  list(FILTER QML_SOURCES EXCLUDE REGEX "qml/paymentimport[^/]*\\.(cpp|h)$")
  list(FILTER QML_SOURCES EXCLUDE REGEX "qml/models/paymentimportoperation\\.(cpp|h)$")
  list(FILTER QML_SOURCES EXCLUDE REGEX "qml/models/walletimporttarget\\.(cpp|h)$")
  list(FILTER QML_SOURCES EXCLUDE REGEX "qml/walletqmlcontroller\\.(cpp|h)$")
  list(FILTER QML_SOURCES EXCLUDE REGEX "qml/walletnamescontroller\\.(cpp|h)$")
  list(FILTER QML_SOURCES EXCLUDE REGEX "qml/walletcreationcontroller\\.(cpp|h)$")
  list(FILTER QML_SOURCES EXCLUDE REGEX "qml/walletopenoperation\\.(cpp|h)$")
  list(FILTER QML_SOURCES EXCLUDE REGEX "qml/walletlifecycle\\.(cpp|h)$")
  list(FILTER QML_SOURCES EXCLUDE REGEX "qml/walletopening\\.(cpp|h)$")
  list(FILTER QML_SOURCES EXCLUDE REGEX "qml/walletreceiveadapter\\.(cpp|h)$")
  list(FILTER QML_SOURCES EXCLUDE REGEX "qml/receivetyperegistration\\.h$")
  list(FILTER QML_SOURCES EXCLUDE REGEX "qml/models/(walletreceivemodel|paymentrequesteditormodel|receivereservationmodel|receiptreconciliationmodel)\\.(cpp|h)$")
  list(FILTER QML_SOURCES EXCLUDE REGEX "qml/walletsecurity\\.(cpp|h)$")
  list(FILTER QML_SOURCES EXCLUDE REGEX "qml/walletsend\\.(cpp|h)$")
  list(FILTER QML_SOURCES EXCLUDE REGEX "qml/wallettransaction\\.(cpp|h)$")
  list(FILTER QML_SOURCES EXCLUDE REGEX "qml/walletprepare\\.(cpp|h)$")
  list(FILTER QML_SOURCES EXCLUDE REGEX "qml/transactionattempt\\.(cpp|h)$")
  list(FILTER QML_SOURCES EXCLUDE REGEX "qml/psbtpolicy\\.(cpp|h)$")
  list(FILTER QML_SOURCES EXCLUDE REGEX "qml/psbtwork\\.(cpp|h)$")
  list(FILTER QML_SOURCES EXCLUDE REGEX "qml/psbtdocument\\.(cpp|h)$")
  list(FILTER QML_SOURCES EXCLUDE REGEX "qml/psbtsignatures\\.(cpp|h)$")
  list(FILTER QML_SOURCES EXCLUDE REGEX "qml/psbtworktypes\\.h$")
  list(FILTER QML_SOURCES EXCLUDE REGEX "qml/walletmessage\\.(cpp|h)$")
  list(FILTER QML_SOURCES EXCLUDE REGEX "qml/walletbump\\.(cpp|h)$")
  list(FILTER QML_SOURCES EXCLUDE REGEX "qml/walletreview\\.(cpp|h)$")
  list(FILTER QML_SOURCES EXCLUDE REGEX "qml/walletreceipts\\.(cpp|h)$")
  list(FILTER QML_SOURCES EXCLUDE REGEX "qml/walletrequestdata\\.(cpp|h)$")
  list(FILTER QML_SOURCES EXCLUDE REGEX "qml/walletrequests\\.(cpp|h)$")
  list(FILTER QML_SOURCES EXCLUDE REGEX "qml/walletreservations\\.(cpp|h)$")
  list(FILTER QML_SOURCES EXCLUDE REGEX "qml/walletreservationdata\\.h$")
  list(FILTER QML_SOURCES EXCLUDE REGEX "qml/walletbalanceadapter\\.(cpp|h)$")
  list(FILTER QML_SOURCES EXCLUDE REGEX "qml/models/activityfilterproxymodel\\.(cpp|h)$")
  list(FILTER QML_SOURCES EXCLUDE REGEX "qml/models/activitydetailmodel\\.(cpp|h)$")
  list(FILTER QML_SOURCES EXCLUDE REGEX "qml/models/addresslistmodel\\.(cpp|h)$")
  list(FILTER QML_SOURCES EXCLUDE REGEX "qml/models/bumptransactionmodel\\.(cpp|h)$")
  list(FILTER QML_SOURCES EXCLUDE REGEX "qml/models/coinslistmodel\\.(cpp|h)$")
  list(FILTER QML_SOURCES EXCLUDE REGEX "qml/models/paymentrequest\\.(cpp|h)$")
  list(FILTER QML_SOURCES EXCLUDE REGEX "qml/models/psbtqmlmodel\\.(cpp|h)$")
  list(FILTER QML_SOURCES EXCLUDE REGEX "qml/models/receiverequesthistorymodel\\.(cpp|h)$")
  list(FILTER QML_SOURCES EXCLUDE REGEX "qml/models/receiverequestoperation\\.(cpp|h)$")
  list(FILTER QML_SOURCES EXCLUDE REGEX "qml/models/feeestimatevalues\\.(cpp|h)$")
  list(FILTER QML_SOURCES EXCLUDE REGEX "qml/models/feeestimatereader\\.(cpp|h)$")
  list(FILTER QML_SOURCES EXCLUDE REGEX "qml/models/feeestimatepolicy\\.(cpp|h)$")
  list(FILTER QML_SOURCES EXCLUDE REGEX "qml/models/feeselectionmodel\\.(cpp|h)$")
  list(FILTER QML_SOURCES EXCLUDE REGEX "qml/models/sendrecipient\\.(cpp|h)$")
  list(FILTER QML_SOURCES EXCLUDE REGEX "qml/models/sendrecipientslistmodel\\.(cpp|h)$")
  list(FILTER QML_SOURCES EXCLUDE REGEX "qml/models/signverifymessagemodel\\.(cpp|h)$")
  list(FILTER QML_SOURCES EXCLUDE REGEX "qml/models/transaction\\.(cpp|h)$")
  list(FILTER QML_SOURCES EXCLUDE REGEX "qml/models/transactionactivity\\.(cpp|h)$")
  list(FILTER QML_SOURCES EXCLUDE REGEX "qml/models/transactionactivitymodel\\.(cpp|h)$")
  list(FILTER QML_SOURCES EXCLUDE REGEX "qml/models/transactionflow\\.(cpp|h)$")
  list(FILTER QML_SOURCES EXCLUDE REGEX "qml/models/walletlistmodel\\.(cpp|h)$")
  list(FILTER QML_SOURCES EXCLUDE REGEX "qml/models/walletselectionmodel\\.(cpp|h)$")
  list(FILTER QML_SOURCES EXCLUDE REGEX "qml/models/walletsecuritymodel\\.(cpp|h)$")
  list(FILTER QML_SOURCES EXCLUDE REGEX "qml/models/walletpasswordcontroller\\.(cpp|h)$")
  list(FILTER QML_SOURCES EXCLUDE REGEX "qml/models/(walletbackups|walletbackupcontroller|feebumps|feebumpcontroller)\\.(cpp|h)$")
  list(FILTER QML_SOURCES EXCLUDE REGEX "qml/walletcatalog\\.(cpp|h)$")
  list(FILTER QML_SOURCES EXCLUDE REGEX "qml/walletregistry\\.(cpp|h)$")
  list(FILTER QML_SOURCES EXCLUDE REGEX "qml/wallet(creation|restoration|operationpresentation|publication|identity)\\.(cpp|h)$")
  list(FILTER QML_SOURCES EXCLUDE REGEX "qml/models/wallet(creationmodel|operationmodel|operationpresentationmodel|provisioningcheck)\\.(cpp|h)$")
  list(FILTER QML_SOURCES EXCLUDE REGEX "qml/models/walletqmlmodel\\.(cpp|h)$")
  list(FILTER QML_SOURCES EXCLUDE REGEX "qml/models/walletbalancereader\\.(cpp|h)$")
  list(FILTER QML_SOURCES EXCLUDE REGEX "qml/models/walletqmlmodeltransaction\\.(cpp|h)$")
  list(FILTER QML_SOURCES EXCLUDE REGEX "qml/models/wallet(send|psbt)operation\\.(cpp|h)$")
  list(FILTER QML_SOURCES EXCLUDE REGEX "qml/models/walletsenddraft\\.(cpp|h)$")
endif()
if(NOT ENABLE_TEST_AUTOMATION)
  list(FILTER QML_SOURCES EXCLUDE REGEX "qml/test/")
endif()
if(APPLE)
  enable_language(OBJCXX)
  list(APPEND QML_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/qml/models/macdockiconhandler.mm")
  list(APPEND QML_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/qml/motionpreference_mac.mm")
endif()

set(QML_QRC ${CMAKE_CURRENT_SOURCE_DIR}/qml/bitcoin_qml.qrc)
if(Qt6_VERSION VERSION_GREATER_EQUAL 6.5)
  list(APPEND QML_QRC ${CMAKE_CURRENT_SOURCE_DIR}/qml/compat/qt65/bitcoin_compat.qrc)
else()
  list(APPEND QML_QRC ${CMAKE_CURRENT_SOURCE_DIR}/qml/compat/qt62/bitcoin_compat.qrc)
endif()
qt6_add_resources(QML_QRC_CPP ${QML_QRC})
# See: https://bugreports.qt.io/browse/QTBUG-141858.
get_target_property(warn_flags warn_interface INTERFACE_COMPILE_OPTIONS)
if("-Wtrailing-whitespace=any" IN_LIST warn_flags)
  set_source_files_properties(${QML_QRC_CPP} PROPERTIES COMPILE_OPTIONS -Wno-trailing-whitespace)
endif()
unset(warn_flags)
list(APPEND QML_SOURCES ${QML_QRC_CPP})

list(APPEND QML_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/bitcoin/src/init/bitcoin-qt.cpp")

# Build QML library
add_library(bitcoinqml STATIC ${QML_SOURCES})
if(ENABLE_QML_LINT)
  set_property(TARGET bitcoinqml APPEND PROPERTY AUTOMOC_MOC_OPTIONS "--output-json")
endif()
# Prevent Qt macros like "signal" from polluting bitcoin core code
target_compile_definitions(bitcoinqml
  PUBLIC
    QT_NO_KEYWORDS
    QT_USE_QSTRINGBUILDER
)
if(ENABLE_TEST_AUTOMATION)
  target_compile_definitions(bitcoinqml PUBLIC ENABLE_TEST_AUTOMATION)
endif()
target_include_directories(bitcoinqml
  PRIVATE
    # to keep the convention of #include <qml/*.h>
    ${CMAKE_CURRENT_SOURCE_DIR}

    # for interfaces consensus chainparams etc...
    ${CMAKE_CURRENT_SOURCE_DIR}/bitcoin/src

    # for bitcoin-build-config.h
    ${CMAKE_CURRENT_BINARY_DIR}/bitcoin/src
)
target_link_libraries(bitcoinqml
  PUBLIC
    core_interface
    bitcoin_node
    univalue
    Boost::headers
    $<TARGET_NAME_IF_EXISTS:QRencode::QRencode>
    Qt6::Qml
    Qt6::Quick
    Qt6::QuickControls2
    Qt6::Svg
    Qt6::Network
    Qt6::Widgets
  PRIVATE
    bitcoin_cli
    $<TARGET_NAME_IF_EXISTS:bitcoin_wallet>
    $<$<PLATFORM_ID:Windows>:advapi32>
    $<$<PLATFORM_ID:Darwin>:-framework\ AppKit>
)

# Compile QML-app-specific translations (.ts → .qm) and embed as resources.
qt6_add_translation(QML_GUI_QM_FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/qml/locale/bitcoin_qml_es.ts
)
set_source_files_properties(${QML_GUI_QM_FILES} PROPERTIES
    QT_RESOURCE_ALIAS "bitcoin_qml_es.qm"
)
qt6_add_resources(bitcoinqml "qml_gui_translations"
    PREFIX "/translations"
    FILES ${QML_GUI_QM_FILES}
)

# Embed bitcoin-qt translations built by the bitcoin submodule.
# The submodule (BUILD_GUI=ON) compiles .ts -> .qm files into
# ${CMAKE_BINARY_DIR}/bitcoin/src/qt/locale/ at build time. Derive the
# embedded list from the submodule's current source list so Core translation
# additions/removals do not leave stale resource inputs behind.
include("${CMAKE_CURRENT_SOURCE_DIR}/bitcoin/src/qt/locale/ts_files.cmake")
set(BITCOIN_QT_QM_FILES)
foreach(ts_file IN LISTS ts_files)
    string(REGEX REPLACE "^bitcoin_(.+)\\.ts$" "\\1" tag "${ts_file}")
    set(qm_file "${CMAKE_BINARY_DIR}/bitcoin/src/qt/locale/bitcoin_${tag}.qm")
    set_source_files_properties(${qm_file} PROPERTIES
        GENERATED TRUE
        QT_RESOURCE_ALIAS "bitcoin_${tag}.qm"
    )
    list(APPEND BITCOIN_QT_QM_FILES ${qm_file})
endforeach()
unset(ts_files)
qt6_add_resources(bitcoinqml "bitcoin_qt_translations"
    PREFIX "/translations"
    FILES ${BITCOIN_QT_QM_FILES}
)

# RCC-generated translation sources contain trailing whitespace that trips
# -Wtrailing-whitespace=any (GCC >= 15). Suppress it only for those generated
# files, mirroring the submodule's handling of its own resources.
# See: https://bugreports.qt.io/browse/QTBUG-141858.
get_target_property(warn_flags warn_interface INTERFACE_COMPILE_OPTIONS)
if("-Wtrailing-whitespace=any" IN_LIST warn_flags)
    get_target_property(qml_translation_srcs bitcoinqml SOURCES)
    list(FILTER qml_translation_srcs INCLUDE REGEX "qrc_(qml_gui|bitcoin_qt)_translations\\.cpp$")
    set_source_files_properties(${qml_translation_srcs}
        PROPERTIES COMPILE_OPTIONS -Wno-trailing-whitespace
    )
    unset(qml_translation_srcs)
endif()
unset(warn_flags)

add_dependencies(bitcoinqml bitcoin-qt)

qt6_import_qml_plugins(bitcoinqml)
if(Qt6_VERSION VERSION_GREATER_EQUAL 6.5)
  # The Qt 6.2 settings plugin duplicates QtCore's QQmlSettings symbols in static builds.
  get_target_property(qml_libraries bitcoinqml INTERFACE_LINK_LIBRARIES)
  list(REMOVE_ITEM qml_libraries Qt6::qmlsettingsplugin)
  set_property(TARGET bitcoinqml PROPERTY INTERFACE_LINK_LIBRARIES "${qml_libraries}")
endif()

# Put it all together
add_executable(bitcoin-core-app main.cpp)
target_include_directories(bitcoin-core-app
  PRIVATE
    # to keep the convention of #include <qml/*.h>
    ${CMAKE_CURRENT_SOURCE_DIR}

    # for interfaces compat util etc...
    ${CMAKE_CURRENT_SOURCE_DIR}/bitcoin/src

    # for bitcoin-build-config.h
    ${CMAKE_CURRENT_BINARY_DIR}/bitcoin/src
)
target_link_libraries(bitcoin-core-app
  PRIVATE
    univalue
    core_interface
    bitcoin_node
    bitcoinqml
    bitcoinqt
)
qt6_import_plugins(bitcoin-core-app INCLUDE Qt6::QSvgPlugin Qt6::QSvgIconPlugin)

if(ENABLE_QML_LINT)
  include(cmake/QmlLint.cmake)
endif()

if(ENABLE_TEST_AUTOMATION)
  get_target_property(qt_lib_type Qt6::Core TYPE)
  if(qt_lib_type STREQUAL "STATIC_LIBRARY")
    # Functional tests start the application with the minimal platform.
    qt6_import_plugins(bitcoin-core-app INCLUDE Qt6::QMinimalIntegrationPlugin)
  endif()
endif()

if(BUILD_APP_TESTS)
  include(CTest)
  enable_testing()
  add_subdirectory(test)
endif()

include(cmake/Sanitizers.cmake)
include(cmake/AppWindowsManifest.cmake)
