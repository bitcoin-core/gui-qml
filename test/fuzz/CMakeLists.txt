# Fuzzing targets for the QML app backend.
#
# Enable with -DBUILD_FUZZ_TESTS=ON. Requires Clang with libFuzzer support
# (e.g. Homebrew LLVM on macOS, or clang on Ubuntu CI).

if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    message(WARNING "Fuzz tests require a Clang compiler; skipping test/fuzz.")
    return()
endif()

# Reuse the Qt components already located by the parent project when available.
if(NOT TARGET Qt6::Core)
    find_package(Qt 6.2 MODULE REQUIRED COMPONENTS Core Gui Network Qml Quick Widgets)
endif()

set(FUZZ_SANITIZERS "fuzzer,address,undefined" CACHE STRING "Sanitizers enabled for fuzz targets")

function(add_fuzz_target name)
    add_executable(${name} fuzz_common.cpp ${ARGN})
    target_include_directories(${name} PRIVATE
        ${PROJECT_SOURCE_DIR}
        ${PROJECT_SOURCE_DIR}/bitcoin/src
        ${PROJECT_BINARY_DIR}/bitcoin/src
        ${PROJECT_SOURCE_DIR}/test
        ${PROJECT_SOURCE_DIR}/test/mocks
    )
    target_link_libraries(${name} PRIVATE
        bitcoinqml
        bitcoinqt
        Qt6::Core
        Qt6::Gui
        Qt6::Network
        Qt6::Qml
        Qt6::Quick
        Qt6::Svg
        Qt6::Widgets
    )
    target_compile_options(${name} PRIVATE
        -fsanitize=${FUZZ_SANITIZERS}
        -g
        -O1
    )
    target_link_options(${name} PRIVATE
        -fsanitize=${FUZZ_SANITIZERS}
    )
    set_target_properties(${name} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/test/fuzz
    )
endfunction()

add_fuzz_target(fuzz_bitcoin_uri fuzz_bitcoin_uri.cpp)
add_fuzz_target(fuzz_bitcoin_uri_parsing fuzz_bitcoin_uri_parsing.cpp)
add_fuzz_target(fuzz_bitcoin_address fuzz_bitcoin_address.cpp)
add_fuzz_target(fuzz_bitcoin_amount fuzz_bitcoin_amount.cpp)
add_fuzz_target(fuzz_qml_datadir fuzz_qml_datadir.cpp)
add_fuzz_target(fuzz_bitcoin_units fuzz_bitcoin_units.cpp)
add_fuzz_target(fuzz_bitcoin_uri_model fuzz_bitcoin_uri_model.cpp)
add_fuzz_target(fuzz_qml_util fuzz_qml_util.cpp)
add_fuzz_target(fuzz_peerstatsutil fuzz_peerstatsutil.cpp)
add_fuzz_target(fuzz_qrimageprovider fuzz_qrimageprovider.cpp)
add_fuzz_target(fuzz_syncprogresstracker fuzz_syncprogresstracker.cpp)
add_fuzz_target(fuzz_rpc_command_executor fuzz_rpc_command_executor.cpp)
add_fuzz_target(fuzz_onboarding_storage fuzz_onboarding_storage.cpp)
add_fuzz_target(fuzz_onboarding_startup fuzz_onboarding_startup.cpp)
add_fuzz_target(fuzz_bitcoin_address_format fuzz_bitcoin_address_format.cpp)
add_fuzz_target(fuzz_bitcoin_amount_format fuzz_bitcoin_amount_format.cpp)
add_fuzz_target(fuzz_qml_bitcoinunits_extra fuzz_qml_bitcoinunits_extra.cpp)
add_fuzz_target(fuzz_nodemodel fuzz_nodemodel.cpp)
add_fuzz_target(fuzz_nodemodel_info fuzz_nodemodel_info.cpp)
add_fuzz_target(fuzz_bech32 fuzz_bech32.cpp)
add_fuzz_target(fuzz_svg fuzz_svg.cpp)
add_fuzz_target(fuzz_image fuzz_image.cpp)
add_fuzz_target(fuzz_transaction fuzz_transaction.cpp)
add_fuzz_target(fuzz_activityfilter fuzz_activityfilter.cpp)

if(ENABLE_WALLET)
    add_fuzz_target(fuzz_psbtqmlmodel fuzz_psbtqmlmodel.cpp)
    add_fuzz_target(fuzz_walletqmlmodel fuzz_walletqmlmodel.cpp)
endif()
add_fuzz_target(fuzz_options_model fuzz_options_model.cpp)
add_fuzz_target(fuzz_payment_request fuzz_payment_request.cpp)
add_fuzz_target(fuzz_payment_request_deserialization fuzz_payment_request_deserialization.cpp)
add_fuzz_target(fuzz_debug_log_model fuzz_debug_log_model.cpp)
add_fuzz_target(fuzz_debug_log_streaming fuzz_debug_log_streaming.cpp)
add_fuzz_target(fuzz_rpc_console_model fuzz_rpc_console_model.cpp)
add_fuzz_target(fuzz_send_recipient fuzz_send_recipient.cpp)
add_fuzz_target(fuzz_address_list_model fuzz_address_list_model.cpp)
add_fuzz_target(fuzz_chain_model fuzz_chain_model.cpp)
add_fuzz_target(fuzz_network_status_model fuzz_network_status_model.cpp)
add_fuzz_target(fuzz_block_clock_model fuzz_block_clock_model.cpp)
add_fuzz_target(fuzz_onboarding_options_model fuzz_onboarding_options_model.cpp)
add_fuzz_target(fuzz_ban_list_model fuzz_ban_list_model.cpp)
add_fuzz_target(fuzz_peer_list_model fuzz_peer_list_model.cpp)
add_fuzz_target(fuzz_peer_list_sort_proxy fuzz_peer_list_sort_proxy.cpp)

if(ENABLE_WALLET)
    add_fuzz_target(fuzz_bump_transaction_model fuzz_bump_transaction_model.cpp)
    add_fuzz_target(fuzz_coins_list_model fuzz_coins_list_model.cpp)
    add_fuzz_target(fuzz_sign_verify_message_model fuzz_sign_verify_message_model.cpp)
endif()
