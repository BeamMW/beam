set(TARGET_NAME libbitcoin_formats)

file(GLOB SOURCES "*.cpp")
file(GLOB HEADERS "${LIBBITCOIN_INCLUDE_DIR}/formats/*.hpp")

add_library(${TARGET_NAME} OBJECT ${SOURCES} ${HEADERS})

if(MSVC)
    target_compile_options(${TARGET_NAME} PUBLIC "/wd4244")
    target_compile_options(${TARGET_NAME} PUBLIC "/wd4996")
    target_compile_options(${TARGET_NAME} PUBLIC "/wd4273")
elseif(ANDROID)
    target_compile_options(${TARGET_NAME} PUBLIC -Wno-tautological-constant-out-of-range-compare)
else()
    target_compile_options(${TARGET_NAME} PUBLIC -fpermissive)
endif()

target_link_libraries(${TARGET_NAME} Boost::boost)
