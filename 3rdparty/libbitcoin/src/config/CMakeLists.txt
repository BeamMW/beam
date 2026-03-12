set(TARGET_NAME libbitcoin_config)

file(GLOB SOURCES "*.cpp")
list(REMOVE_ITEM SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/authority.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/base16.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/base2.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/base58.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/base64.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/directory.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/endpoint.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/hash160.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/hash256.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/parameter.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/parser.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/printer.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/sodium.cpp")
file(GLOB HEADERS "${LIBBITCOIN_INCLUDE_DIR}/config/*.hpp")

add_library(${TARGET_NAME} OBJECT ${SOURCES} ${HEADERS})

if(MSVC)
    target_compile_options(${TARGET_NAME} PUBLIC "/wd4996")
    target_compile_options(${TARGET_NAME} PUBLIC "/wd4251")
    target_compile_options(${TARGET_NAME} PUBLIC "/wd4273")
    #target_compile_options(libbitcoin_config PRIVATE "/wd2491")
elseif(APPLE OR CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    target_compile_options(${TARGET_NAME} PUBLIC -Wno-reorder)
    target_compile_options(${TARGET_NAME} PUBLIC -Wno-delete-non-virtual-dtor)
elseif(ANDROID)
    target_compile_options(${TARGET_NAME} PUBLIC -Wno-delete-non-virtual-dtor)
    target_compile_options(${TARGET_NAME} PUBLIC -Wno-reorder)
    target_compile_options(${TARGET_NAME} PUBLIC -Wno-tautological-constant-out-of-range-compare)
else()
    target_compile_options(${TARGET_NAME} PUBLIC -Wno-reorder)
    target_compile_options(${TARGET_NAME} PUBLIC -fpermissive)
endif()

target_link_libraries(${TARGET_NAME} Boost::boost)
