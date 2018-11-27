set(HTTP_SRC
    http_msg_reader.cpp
    http_msg_creator.cpp
    http_client.cpp
    http_json_serializer.cpp
    ${PROJECT_SOURCE_DIR}/3rdparty/picohttpparser/picohttpparser.c)

add_library(http STATIC ${HTTP_SRC})
add_dependencies(http utility)
target_link_libraries(http utility)

add_subdirectory(unittests)
