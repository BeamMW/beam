set(TARGET_NAME libbitcoin_utility)

file(GLOB SOURCES "*.cpp")
file(GLOB HEADERS "${LIBBITCOIN_INCLUDE_DIR}/utility/*.hpp")

add_library(${TARGET_NAME} OBJECT ${SOURCES} ${HEADERS})

if(MSVC)
    target_compile_options(${TARGET_NAME} PUBLIC "/wd4244")
    target_compile_options(${TARGET_NAME} PUBLIC "/wd4996")
elseif(APPLE)
    target_compile_options(${TARGET_NAME} PUBLIC -Wno-reorder)
    target_compile_options(${TARGET_NAME} PUBLIC -Wno-unused-variable)
elseif(ANDROID)
    target_compile_options(${TARGET_NAME} PUBLIC -Wno-tautological-constant-compare)
    target_compile_options(${TARGET_NAME} PUBLIC -Wno-reorder)
    target_compile_options(${TARGET_NAME} PUBLIC -Wno-unused-variable)
else()
    target_compile_options(${TARGET_NAME} PUBLIC -Wno-reorder)
    target_compile_options(${TARGET_NAME} PUBLIC -fpermissive)
endif()

target_link_libraries(${TARGET_NAME} Boost::boost)
