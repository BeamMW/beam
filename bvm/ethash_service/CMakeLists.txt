cmake_minimum_required(VERSION 3.13)

add_library(ethash_utils STATIC ethash_utils.cpp)
target_link_libraries(ethash_utils 
    PUBLIC 
        core
        keccak
        ethash
        cli
        http
        utility
        p2p
    )

add_executable(ethash-service ethash_service.cpp)
target_link_libraries(ethash-service 
    PRIVATE
        wallet_api
        ethash_utils
)

 if (BEAM_IPFS_SUPPORT AND WIN32)
    # TODO:IPFS ethash-service doesn't need ipfs, compile without it
    # TODO: This if hacky AF and beacuse we place everything in different folders.
    get_target_property(IPFS_DLL asio-ipfs IPFS_DLL)
    add_custom_command(
        TARGET ethash-service POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy ${IPFS_DLL} ${CMAKE_CURRENT_BINARY_DIR}
    )
endif()