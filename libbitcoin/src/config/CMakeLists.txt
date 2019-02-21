file(GLOB SOURCES "*.cpp")
file(GLOB HEADERS "${LIBBITCOIN_INCLUDE_DIR}/config/*.hpp")

add_library(libbitcoin_config OBJECT ${SOURCES} ${HEADERS})

if(MSVC)
    target_compile_options(libbitcoin_config PRIVATE "/wd4996")
    target_compile_options(libbitcoin_config PRIVATE "/wd4251")
    target_compile_options(libbitcoin_config PRIVATE "/wd4273")
    #target_compile_options(libbitcoin_config PRIVATE "/wd2491")
elseif(APPLE)
    target_compile_options(libbitcoin_config PRIVATE -Wno-reorder)
    target_compile_options(libbitcoin_config PRIVATE -Wno-delete-non-virtual-dtor)
endif()