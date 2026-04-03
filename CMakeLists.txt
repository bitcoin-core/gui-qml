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
find_package(QRencode MODULE REQUIRED)

option(ENABLE_TEST_AUTOMATION "Enable test automation bridge for QML UI testing" OFF)

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
if(NOT ENABLE_TEST_AUTOMATION)
  list(FILTER QML_SOURCES EXCLUDE REGEX "qml/test/")
endif()
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
if(ENABLE_TEST_AUTOMATION)
  find_package(Qt6 REQUIRED COMPONENTS Network)
  target_compile_definitions(bitcoinqml PUBLIC ENABLE_TEST_AUTOMATION)
  target_link_libraries(bitcoinqml PUBLIC Qt6::Network)
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
    Qt6::Widgets
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
# The submodule (BUILD_GUI=ON) compiles .ts → .qm files into
# ${CMAKE_BINARY_DIR}/bitcoin/src/qt/locale/ at build time.
# We mark each file as GENERATED so CMake does not require it to exist
# at configure time, and add a dependency on the bitcoin-qt target so
# the .qm files are built before the resource compiler runs.
set(BITCOIN_LOCALE_TAGS
    am ar ast_ES ay az@latin az be bg bn br bs ca cmn cs cy da
    de_AT de_CH de el en eo es_CL es_CO es_DO es es_SV es_VE et eu
    fa fil fi fo fr_CM fr_LU fr ga_IE ga gd gl_ES gl gu hak ha he hi
    hr hu id is it ja ka kk@latin kk kl km kn ko ku_IQ ku ky la lb
    lt lv mg mi mk ml mn mr_IN mr ms mt my nb ne nl no or pam pa pl
    ps pt_BR pt ro ru sa sc sd si sk sl sm sn so sq
    sr@ijekavianlatin sr@latin sr sv sw szl ta te th tk tl tn tr ug uk ur
    uz@Cyrl uz@Latn uz ve vi yi yo yue zh_CN zh-Hans zh-Hant zh_HK zh zh_TW zu
)
set(BITCOIN_QT_QM_FILES)
foreach(tag IN LISTS BITCOIN_LOCALE_TAGS)
    set(qm_file "${CMAKE_BINARY_DIR}/bitcoin/src/qt/locale/bitcoin_${tag}.qm")
    set_source_files_properties(${qm_file} PROPERTIES
        GENERATED TRUE
        QT_RESOURCE_ALIAS "bitcoin_${tag}.qm"
    )
    list(APPEND BITCOIN_QT_QM_FILES ${qm_file})
endforeach()
qt6_add_resources(bitcoinqml "bitcoin_qt_translations"
    PREFIX "/translations"
    FILES ${BITCOIN_QT_QM_FILES}
)
add_dependencies(bitcoinqml bitcoin-qt)

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

option(BUILD_APP_TESTS "Build unit tests for the app" ON)
if(BUILD_APP_TESTS)
  include(CTest)
  enable_testing()
  add_subdirectory(test)
endif()
