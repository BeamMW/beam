cmake_minimum_required(VERSION 3.11)

set(HTTP_SRC
    http_msg_reader.cpp
    http_msg_creator.cpp
    http_client.cpp
    http_json_serializer.cpp
    ${PROJECT_SOURCE_DIR}/3rdparty/picohttpparser/picohttpparser.c)

add_library(http STATIC ${HTTP_SRC})
target_link_libraries(http utility)

target_include_directories(http
    INTERFACE 
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/>
)

add_subdirectory(unittests)
