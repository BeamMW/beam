set(CORE_SRC
    block_crypt.cpp
# ~etc
)
add_library(core STATIC ${CORE_SRC})

include_directories(${PROJECT_SOURCE_DIR}/core)

add_subdirectory(unittest)
