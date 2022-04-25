cmake_minimum_required(VERSION 3.13)

set(SOURCES_PRIVATE
    wallet_client.cpp
    filter.cpp
    apps_api/apps_api.cpp
    apps_api/apps_utils.cpp
)

set(SOURCES_PUBLIC
    wallet_client.h
    filter.h
)

add_library(wallet_client STATIC ${SOURCES_PRIVATE})
add_library(wallet_client_no_ipfs STATIC ${SOURCES_PRIVATE})

target_sources(wallet_client
    PUBLIC  ${SOURCES_PUBLIC}
)

target_sources(wallet_client_no_ipfs
    PUBLIC ${SOURCES_PUBLIC}
)

target_link_libraries(wallet_client 
    PUBLIC wallet
    PRIVATE http
)

target_link_libraries(wallet_client_no_ipfs
    PUBLIC wallet
    PRIVATE http
)

target_compile_features(wallet_client PUBLIC cxx_std_17)
target_compile_features(wallet_client_no_ipfs PUBLIC cxx_std_17)

add_subdirectory(extensions)

target_link_libraries(wallet_client PUBLIC client_extensions)
target_link_libraries(wallet_client_no_ipfs PUBLIC client_extensions)

if(BEAM_IPFS_SUPPORT)
    target_link_libraries(wallet_client PUBLIC wallet_ipfs)
endif()