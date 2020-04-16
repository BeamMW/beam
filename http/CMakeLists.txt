cmake_minimum_required(VERSION 3.13)

set(HTTP_SRC
    http_msg_reader.cpp
    http_msg_creator.cpp
    http_client.cpp
    http_json_serializer.cpp
    ${PROJECT_SOURCE_DIR}/3rdparty/picohttpparser/picohttpparser.c)

add_library(http STATIC ${HTTP_SRC})
target_link_libraries(http PUBLIC utility)

if(BEAM_TESTS_ENABLED)
    add_subdirectory(unittests)
endif()
