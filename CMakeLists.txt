cmake_minimum_required(VERSION 3.22)

project(BitcoinCoreApp
  DESCRIPTION "Bitcoin GUI"
  HOMEPAGE_URL "https://bitcoincore.org/"
  LANGUAGES CXX
)

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
set(qt_components Core Gui LinguistTools Widgets Qml Quick QuickControls2)
find_package(Qt 6.2 MODULE REQUIRED
    COMPONENTS ${qt_components}
)
find_package(Libevent 2.1.8 MODULE REQUIRED)

# Do not build any executable targets from bitcoin submodule
set(BUILD_BENCH OFF)
set(BUILD_BITCOIN_BIN OFF)
set(BUILD_CLI OFF)
set(BUILD_DAEMON OFF)
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
# We need this libraries, can ignore the executable bitcoin-qt
set(BUILD_GUI ON)
set(ENABLE_WALLET ON)

# Bitcoin Core codebase
# Builds libraries: univalue, core_interface, bitcoin_node, bitcoin_wallet
add_subdirectory(bitcoin)

# Qt-specific commands
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOMOC_MOC_OPTIONS "-p${CMAKE_CURRENT_SOURCE_DIR}")
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)
# but don't let Qt interfere with bitcoin core libraries
set_target_properties(bitcoin_wallet PROPERTIES AUTOUIC OFF)

# Compile Qt+QML sources
set(CMAKE_AUTOMOC_MOC_OPTIONS "-I${CMAKE_CURRENT_SOURCE_DIR}/qml")
file(GLOB_RECURSE QML_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/qml/*.cpp" "${CMAKE_CURRENT_SOURCE_DIR}/qml/*.h")
list(FILTER QML_SOURCES EXCLUDE REGEX "androidnotifier\\.(cpp|h)$")
set(QML_QRC "${CMAKE_CURRENT_SOURCE_DIR}/qml/bitcoin_qml.qrc")
qt6_add_resources(QML_QRC_CPP ${QML_QRC})
list(APPEND QML_SOURCES ${QML_QRC_CPP})
list(APPEND QML_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/bitcoin/src/init/bitcoin-qt.cpp")

# Build QML library
add_library(bitcoinqml STATIC ${QML_SOURCES})
# Prevent Qt macros like "signal" from polluting bitcoin core code
target_compile_definitions(bitcoinqml
  PUBLIC
    QT_NO_KEYWORDS
    QT_USE_QSTRINGBUILDER
)
target_include_directories(bitcoinqml
  PRIVATE
    # to keep the convention of #include <qml/*.h>
    ${CMAKE_CURRENT_SOURCE_DIR}

    # for interfaces consensus chainparams etc...
    ${CMAKE_CURRENT_SOURCE_DIR}/bitcoin/src

    # for qt/guiutil.h qt/peertablemodel.h qt/rpcconsole.h etc...
    ${CMAKE_CURRENT_SOURCE_DIR}/bitcoin/src/qt

    # for bitcoin-build-config.h
    ${CMAKE_CURRENT_BINARY_DIR}/bitcoin/src
)
target_link_libraries(bitcoinqml
  PUBLIC
    core_interface
    bitcoin_node
    univalue
    Qt6::Qml
    Qt6::Quick
    Qt6::QuickControls2
    Qt6::Widgets
)

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