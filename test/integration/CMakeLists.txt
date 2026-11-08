add_library(bitcoinqml_test_application STATIC fixture/test_application.cpp)
target_include_directories(bitcoinqml_test_application PUBLIC
  ${PROJECT_SOURCE_DIR}
  ${PROJECT_SOURCE_DIR}/bitcoin/src
  ${PROJECT_BINARY_DIR}/bitcoin/src
)
target_link_libraries(bitcoinqml_test_application PUBLIC bitcoinqml bitcoinqt Qt6::Test)

add_executable(bitcoinqml_integration_tests
  runner/test_main.cpp
  runner/test_cases.cpp
  fixture/test_process.cpp
  foundation/test_lifecycle.cpp
  node_setup/test_navigation.cpp
  node_tools/test_node.cpp
)
target_link_libraries(bitcoinqml_integration_tests PRIVATE bitcoinqml_test_application Qt6::QuickTest)
set(stage 0)
foreach(group IN ITEMS foundation node_setup node_tools)
  math(EXPR stage "${stage} + 1")
  add_test(NAME bitcoinqml_integration_${group} COMMAND bitcoinqml_integration_tests
    --stage=${stage}
    --profile-dir=${PROJECT_BINARY_DIR}/test-profiles
    -o ${PROJECT_BINARY_DIR}/test-reports/integration-${group}.xml,junitxml -o -,txt
  )
  set_tests_properties(bitcoinqml_integration_${group} PROPERTIES
    TIMEOUT 1500
    LABELS "integration;${group}"
    ENVIRONMENT "${gui_test_environment}"
  )
endforeach()

if(qt_lib_type STREQUAL "STATIC_LIBRARY")
  qt6_import_plugins(bitcoinqml_integration_tests INCLUDE Qt6::QMinimalIntegrationPlugin Qt6::QSvgPlugin Qt6::QSvgIconPlugin)
  # Core's Widgets library and this optional remote-message service both
  # export DebugMessageHandler. Tests capture Qt diagnostics locally.
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
