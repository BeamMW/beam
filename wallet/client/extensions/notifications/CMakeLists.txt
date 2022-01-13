cmake_minimum_required(VERSION 3.13)

add_library(notifications STATIC)
target_sources(notifications
    PUBLIC
        notification_center.h
        notification.h
        notification_observer.h
    PRIVATE
        notification_center.cpp
        notification.cpp
)

target_link_libraries(notifications 
    PUBLIC
        wallet_core
)

target_compile_features(notifications PUBLIC cxx_std_17)

