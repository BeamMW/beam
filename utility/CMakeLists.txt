set(UTILITY_SRC
    logger.cpp
    logger_checkpoints.cpp
# ~etc
)
add_library(utility STATIC ${UTILITY_SRC})
add_library(crypto STATIC crypto/blake/blake2b.cpp)

include_directories(${PROJECT_SOURCE_DIR}/utility)

add_subdirectory(unittest)
