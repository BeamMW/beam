set(UTILITY_SRC
    logger.cpp
    logger_checkpoints.cpp
    time_helpers.cpp
# ~etc
)
add_subdirectory(io)

add_library(utility STATIC ${UTILITY_SRC})
add_dependencies(utility io)
target_link_libraries(utility io)

add_subdirectory(unittest)
