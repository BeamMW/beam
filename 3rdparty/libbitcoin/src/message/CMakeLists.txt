set(TARGET_NAME libbitcoin_message)

file(GLOB SOURCES "*.cpp")
list(REMOVE_ITEM SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/address.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/alert.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/alert_payload.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/block.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/block_transactions.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/compact_block.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/fee_filter.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/filter_add.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/filter_clear.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/filter_load.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/get_address.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/get_block_transactions.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/get_blocks.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/get_data.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/get_headers.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/header.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/headers.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/heading.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/inventory.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/inventory_vector.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/memory_pool.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/merkle_block.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/network_address.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/not_found.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/ping.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/pong.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/prefilled_transaction.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/reject.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/send_compact.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/send_headers.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/transaction.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/verack.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/version.cpp")
file(GLOB HEADERS "${LIBBITCOIN_INCLUDE_DIR}/message/*.hpp")

add_library(${TARGET_NAME} OBJECT ${SOURCES} ${HEADERS})

if(MSVC)
    target_compile_options(${TARGET_NAME} PUBLIC "/wd4245")
    target_compile_options(${TARGET_NAME} PUBLIC "/wd4505")
    target_compile_options(${TARGET_NAME} PUBLIC "/wd4996")
    target_compile_options(${TARGET_NAME} PUBLIC "/wd4702")
elseif(APPLE)
    target_compile_options(${TARGET_NAME} PUBLIC -Wno-mismatched-tags)
    target_compile_options(${TARGET_NAME} PUBLIC -Wno-missing-braces)
elseif(ANDROID)
    target_compile_options(${TARGET_NAME} PUBLIC -Wno-tautological-constant-compare)
    target_compile_options(${TARGET_NAME} PUBLIC -Wno-mismatched-tags)
else()
    target_compile_options(${TARGET_NAME} PUBLIC -fpermissive)
endif()

target_link_libraries(${TARGET_NAME} Boost::boost)
