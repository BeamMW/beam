cmake_minimum_required(VERSION 3.11)

set (TARGET_NAME mnemonic)

set(MNEMONIC_SRC
    dictionary.cpp
    mnemonic.cpp
)

if (MSVC AND (MSVC_VERSION GREATER_EQUAL 1921))
    add_compile_options("/bigobj")
endif()

add_library(${TARGET_NAME} STATIC ${MNEMONIC_SRC})

target_link_libraries(${TARGET_NAME}
    PUBLIC
        core
        Boost::boost
    PRIVATE
        pbkdf
)

add_subdirectory(unittests)
