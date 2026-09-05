# Copyright (c) 2026 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or https://opensource.org/license/mit/.

include(ImportQtPlugins)

set(CMAKE_AUTOMOC ON)

qt6_add_resources(BITCOIN_QML_QRC bitcoin_qml.qrc)
if(Qt6_VERSION VERSION_GREATER_EQUAL 6.5)
  qt6_add_resources(BITCOIN_COMPAT_QRC compat/qt65/bitcoin_compat.qrc)
else()
  qt6_add_resources(BITCOIN_COMPAT_QRC compat/qt62/bitcoin_compat.qrc)
endif()
# See: https://bugreports.qt.io/browse/QTBUG-141858.
get_target_property(warn_flags warn_interface INTERFACE_COMPILE_OPTIONS)
if("-Wtrailing-whitespace=any" IN_LIST warn_flags)
  set_source_files_properties(
    ${BITCOIN_QML_QRC}
    ${BITCOIN_COMPAT_QRC}
    PROPERTIES COMPILE_OPTIONS -Wno-trailing-whitespace
  )
endif()
unset(warn_flags)

add_library(bitcoinqml STATIC
  applicationrouter.cpp
  applicationrouter.h
  appmode.h
  bitcoin.cpp
  bitcoin.h
  bitcoinqmlapplication.cpp
  bitcoinqmlapplication.h
  bitcoinunits.cpp
  bitcoinunits.h
  buildinfo.h
  clipboard.h
  components/blockclockdial.cpp
  components/blockclockdial.h
  controls/linegraph.cpp
  controls/linegraph.h
  core_settings.cpp
  core_settings.h
  datadir.cpp
  datadir.h
  guiargs.cpp
  guiargs.h
  guiconstants.h
  imageprovider.cpp
  imageprovider.h
  initexecutor.cpp
  initexecutor.h
  legacy_settings_migration.cpp
  legacy_settings_migration.h
  models/banlistmodel.cpp
  models/banlistmodel.h
  models/chainmodel.cpp
  models/chainmodel.h
  models/chainsyncmodel.cpp
  models/chainsyncmodel.h
  models/core_settings_model.cpp
  models/core_settings_model.h
  models/desktoptrayiconcontroller.cpp
  models/desktoptrayiconcontroller.h
  models/desktopwindowbehaviormodel.cpp
  models/desktopwindowbehaviormodel.h
  models/languagesettingsmodel.cpp
  models/languagesettingsmodel.h
  models/macdockiconhandler.h
  models/networkstatusmodel.cpp
  models/networkstatusmodel.h
  models/networktraffictower.cpp
  models/networktraffictower.h
  models/nodelifecyclemodel.cpp
  models/nodelifecyclemodel.h
  models/nodemodel.cpp
  models/nodemodel.h
  models/nodenetworkmodel.cpp
  models/nodenetworkmodel.h
  models/onboardingoptionsmodel.cpp
  models/onboardingoptionsmodel.h
  models/options_model.cpp
  models/options_model.h
  models/peerdetailsmodel.cpp
  models/peerdetailsmodel.h
  models/peerlistmodel.cpp
  models/peerlistmodel.h
  models/peerlistsortproxy.cpp
  models/peerlistsortproxy.h
  models/runtimedialogmodel.cpp
  models/runtimedialogmodel.h
  models/settings_keys.h
  networkstyle.cpp
  networkstyle.h
  onboarding_settings.cpp
  onboarding_settings.h
  onboarding_storage.cpp
  onboarding_storage.h
  peerstatsutil.cpp
  peerstatsutil.h
  translationmanager.cpp
  translationmanager.h
  ${BITCOIN_QML_QRC}
  ${BITCOIN_COMPAT_QRC}
)

# Compile and embed the QML catalog alongside Bitcoin Core's existing Qt
# catalogs. OptionsQmlModel discovers the latter from this resource prefix.
qt6_add_translation(BITCOIN_QML_QM_FILES
  locale/bitcoin_qml_es.ts
  OPTIONS -silent
)
set_source_files_properties(${BITCOIN_QML_QM_FILES} PROPERTIES
  QT_RESOURCE_ALIAS bitcoin_qml_es.qm
)
qt6_add_resources(bitcoinqml qml_gui_translations
  PREFIX /translations
  FILES ${BITCOIN_QML_QM_FILES}
)

include(${PROJECT_SOURCE_DIR}/src/qt/locale/ts_files.cmake)
set(BITCOIN_QT_TS_FILES)
foreach(ts_file IN LISTS ts_files)
  list(APPEND BITCOIN_QT_TS_FILES ${PROJECT_SOURCE_DIR}/src/qt/locale/${ts_file})
endforeach()
qt6_add_translation(BITCOIN_QT_QM_FILES
  ${BITCOIN_QT_TS_FILES}
  OPTIONS -silent
)
foreach(qm_file IN LISTS BITCOIN_QT_QM_FILES)
  get_filename_component(qm_name ${qm_file} NAME)
  set_source_files_properties(${qm_file} PROPERTIES QT_RESOURCE_ALIAS ${qm_name})
endforeach()
qt6_add_resources(bitcoinqml bitcoin_qt_translations
  PREFIX /translations
  FILES ${BITCOIN_QT_QM_FILES}
)
get_target_property(warn_flags warn_interface INTERFACE_COMPILE_OPTIONS)
if("-Wtrailing-whitespace=any" IN_LIST warn_flags)
  set_source_files_properties(
    ${CMAKE_CURRENT_BINARY_DIR}/.qt/rcc/qrc_qml_gui_translations.cpp
    ${CMAKE_CURRENT_BINARY_DIR}/.qt/rcc/qrc_bitcoin_qt_translations.cpp
    PROPERTIES COMPILE_OPTIONS -Wno-trailing-whitespace
  )
endif()
unset(warn_flags)
unset(ts_files)

if(APPLE)
  target_sources(bitcoinqml PRIVATE
    models/macdockiconhandler.h
    models/macdockiconhandler.mm
  )
endif()

target_compile_definitions(bitcoinqml
  PUBLIC
    QT_NO_KEYWORDS
    QT_NO_CONTEXTLESS_CONNECT
    QT_USE_QSTRINGBUILDER
)

target_sources(bitcoinqml
  PRIVATE
    test/testbridge.cpp
    test/testbridge.h
)

target_link_libraries(bitcoinqml
  PRIVATE
    core_interface
    bitcoin_node
  PUBLIC
    Boost::headers
    core_interface
    univalue
    Qt6::Qml
    Qt6::Quick
    Qt6::QuickControls2
    Qt6::Network
    Qt6::Widgets
)

add_executable(bitcoin-qt WIN32
  main.cpp
  res/bitcoin-qt-res.rc
  ../init/bitcoin-qt.cpp
)
add_windows_application_manifest(bitcoin-qt)

target_link_libraries(bitcoin-qt
  PRIVATE
    core_interface
    bitcoinqml
    bitcoin_node
)

qt6_import_qml_plugins(bitcoin-qt)
import_plugins(bitcoin-qt)
install_binary_component(bitcoin-qt HAS_MANPAGE)
if(WIN32 AND VCPKG_TARGET_TRIPLET AND CMAKE_VERSION VERSION_GREATER_EQUAL 3.27)
  get_target_property(qt_core_type Qt6::Core TYPE)
  if(qt_core_type STREQUAL "SHARED_LIBRARY")
    file(GENERATE
      OUTPUT $<TARGET_FILE_DIR:bitcoin-qt>/qt.conf
      CONTENT "[Paths]\nPrefix = $<PATH:GET_PARENT_PATH,$<PATH:GET_PARENT_PATH,$<TARGET_FILE_DIR:Qt6::QWindowsIntegrationPlugin>>>\n"
    )
    add_custom_command(TARGET bitcoin-qt POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different
              $<TARGET_RUNTIME_DLL_DIRS:bitcoin-qt> $<TARGET_FILE_DIR:bitcoin-qt>
      COMMAND_EXPAND_LISTS
      VERBATIM
    )
  endif()
  unset(qt_core_type)
endif()

if(ENABLE_IPC)
  add_executable(bitcoin-gui WIN32
    main.cpp
    ../init/bitcoin-gui.cpp
  )
  target_link_libraries(bitcoin-gui
    PRIVATE
      core_interface
      bitcoinqml
      bitcoin_node
      bitcoin_ipc
  )
  qt6_import_qml_plugins(bitcoin-gui)
  import_plugins(bitcoin-gui)
  install_binary_component(bitcoin-gui INTERNAL)
endif()

if(BUILD_GUI_TESTS)
  add_subdirectory(test)
endif()
