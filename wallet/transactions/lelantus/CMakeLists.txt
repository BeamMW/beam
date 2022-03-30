cmake_minimum_required(VERSION 3.13)

set(LELANTUS_SRC
    push_transaction.cpp
    pull_transaction.cpp
    push_tx_builder.cpp
    unlink_transaction.cpp
    lelantus_reg_creators.cpp
)

add_library(lelantus STATIC ${LELANTUS_SRC})
target_link_libraries(lelantus PUBLIC wallet_core)
target_include_directories(lelantus PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_definitions(lelantus PUBLIC BEAM_LELANTUS_SUPPORT)

