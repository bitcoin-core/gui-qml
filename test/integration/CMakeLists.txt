add_library(bitcoinqml_test_application STATIC fixture/test_application.cpp)
target_include_directories(bitcoinqml_test_application PUBLIC
  ${PROJECT_SOURCE_DIR}
  ${PROJECT_SOURCE_DIR}/bitcoin/src
  ${PROJECT_BINARY_DIR}/bitcoin/src
)
target_link_libraries(bitcoinqml_test_application PUBLIC bitcoinqml Qt6::Test)

add_executable(bitcoinqml_integration_tests
  runner/test_main.cpp
  runner/test_cases.cpp
  fixture/test_process.cpp
  foundation/test_lifecycle.cpp
  node_setup/test_navigation.cpp
  node_setup/test_blockclock.cpp
  node_setup/test_settings.cpp
  node_tools/test_node.cpp
  node_tools/test_debug_log.cpp
  node_tools/test_console.cpp
  node_tools/test_blocksonly.cpp

)
if(ENABLE_WALLET)
  target_sources(bitcoinqml_integration_tests PRIVATE
  wallet_management/test_wallet.cpp
  wallet_activity/test_activity.cpp
  wallet_activity/test_receive.cpp
  wallet_sending/test_send.cpp
  wallet_sending/test_transactionpolicy.cpp
  wallet_sending/test_bump.cpp
  psbt/test_psbt.cpp
  signers/test_watchonly.cpp
  payment_import/test_payment_import.cpp
  )
endif()
target_link_libraries(bitcoinqml_integration_tests PRIVATE bitcoinqml_test_application Qt6::QuickTest)
# Lets the crash backtrace name functions linked into the executable.
set_target_properties(bitcoinqml_integration_tests PROPERTIES ENABLE_EXPORTS ON)

# Resolve the pinned Core signer mock independently of the runtime working
# directory: ctest starts this binary in a build subdirectory, so a __FILE__-
# relative traversal depends on where the build tree happens to live.
set_source_files_properties(signers/test_watchonly.cpp PROPERTIES
  COMPILE_DEFINITIONS BITCOINQML_SIGNER_MOCK_SOURCE="${PROJECT_SOURCE_DIR}/bitcoin/test/functional/mocks/signer.py"
)
set(stage 0)
set(integration_groups foundation node_setup node_tools)
if(ENABLE_WALLET)
  list(APPEND integration_groups wallet_management wallet_activity wallet_sending psbt signers payment_import)
endif()
foreach(group IN LISTS integration_groups)
  math(EXPR stage "${stage} + 1")
  set(shard_count 1)
  if(group STREQUAL "wallet_sending")
    set(shard_count 2)
  endif()
  math(EXPR last_shard "${shard_count} - 1")
  foreach(shard RANGE ${last_shard})
    set(report_group ${group})
    set(shard_args)
    if(shard_count GREATER 1)
      set(report_group ${group}_${shard})
      set(shard_args --shard=${shard}/${shard_count})
    endif()
    add_test(NAME bitcoinqml_integration_${report_group} COMMAND bitcoinqml_integration_tests
      --stage=${stage} ${shard_args}
      --profile-dir=${PROJECT_BINARY_DIR}/test-profiles
      -o ${PROJECT_BINARY_DIR}/test-reports/integration-${report_group}.xml,junitxml -o -,txt
    )
    set_tests_properties(bitcoinqml_integration_${report_group} PROPERTIES
      TIMEOUT 1500
      LABELS "integration;${group}"
      ENVIRONMENT "${gui_test_environment}"
    )
  endforeach()
endforeach()

if(ENABLE_WALLET)
  add_test(NAME bitcoinqml_shard_selection
    COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/test/functional/test_qml_integration_shards.py
      $<TARGET_FILE:bitcoinqml_integration_tests>
  )
  set_tests_properties(bitcoinqml_shard_selection PROPERTIES TIMEOUT 60 LABELS unit)

  add_test(NAME bitcoinqml_prepared_wallet_setup
    COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/test/functional/test_qml_prepared_wallet.py
      $<TARGET_FILE:bitcoinqml_integration_tests>
  )
  set_tests_properties(bitcoinqml_prepared_wallet_setup PROPERTIES TIMEOUT 60 LABELS unit)
endif()

if(qt_lib_type STREQUAL "STATIC_LIBRARY")
  qt6_import_plugins(bitcoinqml_integration_tests INCLUDE Qt6::QMinimalIntegrationPlugin Qt6::QSvgPlugin Qt6::QSvgIconPlugin)
  # Tests capture Qt diagnostics locally.
  qt6_import_plugins(bitcoinqml_integration_tests EXCLUDE Qt6::QDebugMessageServiceFactoryPlugin)
  target_link_libraries(bitcoinqml_integration_tests PRIVATE Qt6::LabsPlatformplugin Qt6::qtquickdialogsplugin)
  if(Qt6_VERSION VERSION_GREATER_EQUAL 6.5)
    target_link_libraries(bitcoinqml_integration_tests PRIVATE Qt6::qtqmlcoreplugin)
  else()
    target_link_libraries(bitcoinqml_integration_tests PRIVATE Qt6::qmlsettingsplugin)
  endif()
endif()

add_custom_target(bitcoinqml_integration
  DEPENDS bitcoinqml_integration_tests)
