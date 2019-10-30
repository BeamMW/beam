set(TARGET_NAME libbitcoin_math)

file(GLOB SOURCES "*.cpp")
file(GLOB EXTERNAL_SOURCES "external/*.c")
file(GLOB EXTERNAL_HEADERS "external/*.h")
file(GLOB HEADERS "${LIBBITCOIN_INCLUDE_DIR}/math/*.hpp")

add_library(${TARGET_NAME} OBJECT ${SOURCES} ${EXTERNAL_SOURCES} ${HEADERS} ${EXTERNAL_HEADERS})

target_include_directories(${TARGET_NAME} PRIVATE "${CMAKE_SOURCE_DIR}/3rdparty/secp256k1-zkp/include")

if(MSVC)
    target_compile_options(${TARGET_NAME} PUBLIC "/wd4701")
    target_compile_options(${TARGET_NAME} PUBLIC "/wd4245")
    target_compile_options(${TARGET_NAME} PUBLIC "/wd4505")
    target_compile_options(${TARGET_NAME} PUBLIC "/wd4996")
    target_compile_options(${TARGET_NAME} PUBLIC "/wd4244")
elseif(APPLE)
    target_compile_options(${TARGET_NAME} PUBLIC -Wno-sometimes-uninitialized)
    target_compile_options(${TARGET_NAME} PUBLIC -Wno-missing-braces)
elseif(ANDROID)
    target_compile_options(${TARGET_NAME} PUBLIC -Wno-sometimes-uninitialized)
    target_compile_options(${TARGET_NAME} PUBLIC -Wno-tautological-constant-compare)
else()
    target_compile_options(${TARGET_NAME} PUBLIC -fpermissive)
endif()

target_link_libraries(${TARGET_NAME} Boost::boost)
