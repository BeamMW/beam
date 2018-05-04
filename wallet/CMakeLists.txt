set(WALLET_SRC
    wallet.h
    wallet.cpp
    sender.cpp
    receiver.cpp
)

set(KEYCHAIN_SRC_DIR keychain/src)

set(KEYCHAIN_SRC
    ${KEYCHAIN_SRC_DIR}/utill.cpp
    ${KEYCHAIN_SRC_DIR}/coin.cpp
    ${KEYCHAIN_SRC_DIR}/private_key.cpp
)

add_subdirectory(sqlite)

add_library(wallet STATIC ${WALLET_SRC} ${KEYCHAIN_SRC})

if (Boost_FOUND)
	find_package(Boost COMPONENTS filesystem)
    target_link_libraries(wallet Boost::filesystem)
else()
    target_link_libraries(wallet boost_filesystem)
endif()

add_dependencies(wallet core sqlite)
target_link_libraries(wallet core sqlite)

add_subdirectory(unittests)

target_include_directories(wallet PRIVATE ${PROJECT_SOURCE_DIR}/wallet/keychain/include)
