set (TARGET_NAME "mnemonic")

set(MNEMONIC_SRC
    dictionary.cpp
    mnemonic.cpp

    ${PROJECT_SOURCE_DIR}/3rdparty/hmac_sha512.c
    ${PROJECT_SOURCE_DIR}/3rdparty/pkcs5_pbkdf2.c
    ${PROJECT_SOURCE_DIR}/3rdparty/sha512.c
    ${PROJECT_SOURCE_DIR}/3rdparty/zeroize.c
)

add_library(${TARGET_NAME} STATIC ${MNEMONIC_SRC})
target_include_directories(${TARGET_NAME} PRIVATE ${PROJECT_SOURCE_DIR}}/external)
add_dependencies(${TARGET_NAME} core)
target_link_libraries(${TARGET_NAME} core )

add_subdirectory(unittests)
