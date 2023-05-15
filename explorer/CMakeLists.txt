cmake_minimum_required(VERSION 3.13)

set(TARGET_NAME explorer-node)

#find_package(Boost 1.67 REQUIRED)

set(EXPLORER_SRC
    server.cpp
    adapter.cpp
)

add_library(explorer STATIC ${EXPLORER_SRC})

target_link_libraries(explorer node http swap_offers_board)

add_executable(${TARGET_NAME} explorer_node.cpp)
if(LINUX)
    target_link_libraries(${TARGET_NAME}
        PRIVATE 
            -static-libstdc++
            -static-libgcc)
endif()
target_link_libraries(${TARGET_NAME} PRIVATE explorer Boost::boost cli)

if(BEAM_TESTS_ENABLED)
    add_subdirectory(unittest)
endif()

if (BEAM_SIGN_PACKAGE AND WIN32)
    add_custom_command(
        TARGET ${TARGET_NAME} POST_BUILD
        COMMAND ${SIGNTOOL_EXECUTABLE} sign ${SIGNTOOL_OPTIONS} "${CMAKE_CURRENT_BINARY_DIR}\\${TARGET_NAME}.exe"
    )
endif()
