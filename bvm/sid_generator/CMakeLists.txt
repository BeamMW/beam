cmake_minimum_required(VERSION 3.17)

add_executable(generate-sid sid_generator.cpp)
target_link_libraries(generate-sid PRIVATE bvm utility beam )

install(TARGETS generate-sid
        RUNTIME DESTINATION ${CMAKE_INSTALL_PREFIX}/bin)


