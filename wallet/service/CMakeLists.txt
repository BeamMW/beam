cmake_minimum_required(VERSION 3.11)

set(TARGET_NAME wallet-service${BEAM_TARGET_SUFFIX})

add_executable(${TARGET_NAME} service.cpp)

configure_file("${PROJECT_SOURCE_DIR}/version.h.in" "${CMAKE_CURRENT_BINARY_DIR}/version.h")
target_include_directories(${TARGET_NAME} PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
target_link_directories(${TARGET_NAME} PRIVATE ${Boost_LIBRARY_DIRS})
target_link_libraries(${TARGET_NAME}
    PRIVATE 
        wallet 
        utility 
        http
        cli
)
