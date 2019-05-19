set(TARGET_NAME explorer-node${BEAM_TARGET_SUFFIX})

set(EXPLORER_SRC
    server.cpp
    adapter.cpp
)

configure_file("${PROJECT_SOURCE_DIR}/version.h.in" "${CMAKE_CURRENT_BINARY_DIR}/version.h")

add_library(explorer STATIC ${EXPLORER_SRC})

add_dependencies(explorer node http)
target_link_libraries(explorer node http)

add_executable(${TARGET_NAME} explorer_node.cpp)
target_include_directories(${TARGET_NAME} PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
add_dependencies(${TARGET_NAME} explorer cli)
target_link_libraries(${TARGET_NAME} explorer cli)

add_subdirectory(unittest)

if (BEAM_SIGN_PACKAGE AND WIN32)
    add_custom_command(
        TARGET ${TARGET_NAME} POST_BUILD
        COMMAND ${SIGNTOOL_EXECUTABLE} sign ${SIGNTOOL_OPTIONS} "${CMAKE_CURRENT_BINARY_DIR}\\${TARGET_NAME}.exe"
    )
endif()
