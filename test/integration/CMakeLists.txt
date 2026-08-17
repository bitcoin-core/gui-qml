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
  test_lifecycle.cpp
  test_navigation.cpp
  test_node.cpp
)
target_link_libraries(bitcoinqml_integration_tests PRIVATE bitcoinqml_test_application)

set_source_files_properties("${PROJECT_SOURCE_DIR}/${BITCOIN_QML_COMPAT_DIR}/AppSettings.qml" PROPERTIES
  QT_RESOURCE_ALIAS "qml/controls/AppSettings.qml"
)
set_source_files_properties("${PROJECT_SOURCE_DIR}/${BITCOIN_QML_COMPAT_DIR}/BitcoinAmountDigit.qml" PROPERTIES
  QT_RESOURCE_ALIAS "qml/controls/BitcoinAmountDigit.qml"
)
qt6_add_resources(bitcoinqml_integration_tests "bitcoin_qml_integration_sources"
  PREFIX "/"
  BASE "${PROJECT_SOURCE_DIR}"
  FILES ${BITCOIN_QML_FILES_ABSOLUTE}
)
add_test(NAME bitcoinqml_integration_tests COMMAND bitcoinqml_integration_tests
  --profile-dir=${PROJECT_BINARY_DIR}/test-profiles
  -o ${PROJECT_BINARY_DIR}/test-reports/integration.xml,junitxml -o -,txt
)
# Bound the whole run in addition to each child's watchdog.
set_tests_properties(bitcoinqml_integration_tests PROPERTIES TIMEOUT 1500)

if(qt_lib_type STREQUAL "STATIC_LIBRARY")
  qt6_import_plugins(bitcoinqml_integration_tests INCLUDE Qt6::QMinimalIntegrationPlugin Qt6::QSvgPlugin Qt6::QSvgIconPlugin)
  target_link_libraries(bitcoinqml_integration_tests PRIVATE Qt6::LabsPlatformplugin Qt6::qtquickdialogsplugin)
  if(Qt6_VERSION VERSION_GREATER_EQUAL 6.5)
    target_link_libraries(bitcoinqml_integration_tests PRIVATE Qt6::qtqmlcoreplugin)
  else()
    target_link_libraries(bitcoinqml_integration_tests PRIVATE Qt6::qmlsettingsplugin)
  endif()
endif()

set_tests_properties(bitcoinqml_integration_tests PROPERTIES
  LABELS integration
  ENVIRONMENT "${gui_test_environment}"
)
add_custom_target(bitcoinqml_integration
  DEPENDS bitcoinqml_integration_tests)
