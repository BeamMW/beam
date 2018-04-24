set(KEYCHAIN_SRC
    src/utill.cpp
    src/utxo.cpp
    src/private_key.cpp
)

add_library(keychain STATIC ${KEYCHAIN_SRC})
add_dependencies(keychain core)
target_include_directories(keychain PRIVATE ${PROJECT_SOURCE_DIR}/keychain/include)
target_link_libraries(keychain core)

add_subdirectory(unittests)
