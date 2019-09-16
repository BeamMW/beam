cmake_minimum_required(VERSION 3.11)

set(TARGET_NAME beam-node${BEAM_TARGET_SUFFIX})

add_executable(${TARGET_NAME} cli.cpp)

if(LINUX)
	target_link_libraries(${TARGET_NAME}
        PRIVATE
            -static-libstdc++ 
            -static-libgcc
        )
endif()

configure_file("${PROJECT_SOURCE_DIR}/version.h.in" "${CMAKE_CURRENT_BINARY_DIR}/version.h")
target_include_directories(${TARGET_NAME} PRIVATE ${CMAKE_CURRENT_BINARY_DIR})

target_link_libraries(${TARGET_NAME} 
    PRIVATE 
        node
        external_pow
        cli
)

if (BEAM_SIGN_PACKAGE AND WIN32)
    add_custom_command(
        TARGET ${TARGET_NAME} POST_BUILD
        COMMAND ${SIGNTOOL_EXECUTABLE} sign ${SIGNTOOL_OPTIONS} "${CMAKE_CURRENT_BINARY_DIR}\\${TARGET_NAME}.exe"
    )
endif()
