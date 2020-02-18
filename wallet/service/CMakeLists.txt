cmake_minimum_required(VERSION 3.13)

add_library(websocket STATIC websocket_server.h websocket_server.cpp)
target_link_libraries(websocket PRIVATE Boost::boost)

set(TARGET_NAME wallet-service${BEAM_TARGET_SUFFIX})

add_executable(${TARGET_NAME} service.cpp)


target_compile_definitions(${TARGET_NAME} PRIVATE _SILENCE_ALL_CXX17_DEPRECATION_WARNINGS)

if(MSVC)
    target_compile_options(${TARGET_NAME} PRIVATE /bigobj)
    target_compile_options(${TARGET_NAME} PRIVATE "/wd4996")
endif()

configure_file("${PROJECT_SOURCE_DIR}/version.h.in" "${CMAKE_CURRENT_BINARY_DIR}/version.h")
target_include_directories(${TARGET_NAME} PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
target_link_directories(${TARGET_NAME} PRIVATE ${Boost_LIBRARY_DIRS})
target_link_libraries(${TARGET_NAME}
    PRIVATE 
        wallet_api
        websocket
        wallet 
        utility 
        http
        cli
)


set(MONITOR_TARGET_NAME sbbs-monitor${BEAM_TARGET_SUFFIX})
add_executable(${MONITOR_TARGET_NAME} monitor.cpp)

target_include_directories(${MONITOR_TARGET_NAME} PRIVATE ${CMAKE_CURRENT_BINARY_DIR})

target_link_libraries(${MONITOR_TARGET_NAME}
    PRIVATE
        wallet_api
        websocket
        wallet 
        utility 
    #   http
        cli
) 
