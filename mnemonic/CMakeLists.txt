cmake_minimum_required(VERSION 3.13)

set (TARGET_NAME mnemonic)

set(MNEMONIC_SRC
    dictionary.cpp
    mnemonic.cpp
)

add_library(${TARGET_NAME} STATIC ${MNEMONIC_SRC})

target_link_libraries(${TARGET_NAME}
    PUBLIC
        core
        Boost::boost
    PRIVATE
        pbkdf
)

if(BEAM_TESTS_ENABLED)
    add_subdirectory(unittests)
endif()