set(POW_SRC
    equihash.cpp
    equihash.h
)

add_library(pow STATIC ${POW_SRC})
add_subdirectory(unittests)