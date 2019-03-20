file(GLOB SOURCES "*.cpp")
file(GLOB HEADERS "${LIBBITCOIN_INCLUDE_DIR}/formats/*.hpp")

add_library(libbitcoin_formats OBJECT ${SOURCES} ${HEADERS})

if(MSVC)
    target_compile_options(libbitcoin_formats PUBLIC "/wd4244")
    target_compile_options(libbitcoin_formats PUBLIC "/wd4996")
    target_compile_options(libbitcoin_formats PUBLIC "/wd4273")
elseif(ANDROID)
    target_compile_options(libbitcoin_formats PUBLIC -Wno-tautological-constant-out-of-range-compare)
else()
    target_compile_options(libbitcoin_formats PUBLIC -fpermissive)
endif()