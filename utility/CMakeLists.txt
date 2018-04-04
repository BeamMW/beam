set(UTILITY_SRC
    logger.cpp
    logger_checkpoints.cpp
# ~etc
)
add_library(utility STATIC ${UTILITY_SRC})
