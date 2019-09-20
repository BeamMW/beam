cmake_minimum_required(VERSION 3.11)

#find_package(Boost 1.67 REQUIRED COMPONENTS system thread regex filesystem program_options log locale)

set(TARGET_NAME libbitcoin)

set(LIBBITCOIN_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/include/bitcoin/bitcoin)

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)

add_subdirectory(src)

add_library(${TARGET_NAME} STATIC
    src/error.cpp
    $<TARGET_OBJECTS:libbitcoin_chain>
	$<TARGET_OBJECTS:libbitcoin_config>
	$<TARGET_OBJECTS:libbitcoin_formats>
	$<TARGET_OBJECTS:libbitcoin_log>
	$<TARGET_OBJECTS:libbitcoin_machine>
	$<TARGET_OBJECTS:libbitcoin_math>
	$<TARGET_OBJECTS:libbitcoin_message>
	$<TARGET_OBJECTS:libbitcoin_unicode>
	$<TARGET_OBJECTS:libbitcoin_utility>
	$<TARGET_OBJECTS:libbitcoin_wallet>)

target_include_directories(${TARGET_NAME} PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")

target_link_libraries(${TARGET_NAME} secp256k1)

if(IOS)
        target_link_libraries(${TARGET_NAME} 
            $ENV{BOOST_ROOT_IOS}/lib/libboost_program_options.a 
            $ENV{BOOST_ROOT_IOS}/lib/libboost_filesystem.a
            $ENV{BOOST_ROOT_IOS}/lib/libboost_system.a
            $ENV{BOOST_ROOT_IOS}/lib/libboost_thread.a
            $ENV{BOOST_ROOT_IOS}/lib/libboost_regex.a
            $ENV{BOOST_ROOT_IOS}/lib/libboost_log.a)
elseif(ANDROID)
    target_link_libraries(${TARGET_NAME} 
            $ENV{BOOST_ROOT_ANDROID}/libs/${ANDROID_ABI}/libboost_program_options${ANDROID_SUFFIX} 
            $ENV{BOOST_ROOT_ANDROID}/libs/${ANDROID_ABI}/libboost_filesystem${ANDROID_SUFFIX}
            $ENV{BOOST_ROOT_ANDROID}/libs/${ANDROID_ABI}/libboost_system${ANDROID_SUFFIX}
            $ENV{BOOST_ROOT_ANDROID}/libs/${ANDROID_ABI}/libboost_thread${ANDROID_SUFFIX}
            $ENV{BOOST_ROOT_ANDROID}/libs/${ANDROID_ABI}/libboost_regex${ANDROID_SUFFIX}
            $ENV{BOOST_ROOT_ANDROID}/libs/${ANDROID_ABI}/libboost_log${ANDROID_SUFFIX}
)
endif()

target_link_libraries(${TARGET_NAME}
    Boost::boost 
    Boost::system 
    Boost::thread 
    Boost::regex 
    Boost::filesystem 
    Boost::program_options 
    Boost::log 
    Boost::locale
)

if(MSVC)
    target_compile_options(${TARGET_NAME} PUBLIC "/wd4245")
    target_compile_options(${TARGET_NAME} PUBLIC "/wd4505")
    target_compile_options(${TARGET_NAME} PUBLIC "/wd4996")
elseif(APPLE)
    target_compile_options(${TARGET_NAME} PUBLIC -Wno-mismatched-tags)
    target_compile_options(${TARGET_NAME} PUBLIC -Wno-missing-braces)
elseif(ANDROID)
    target_compile_options(${TARGET_NAME} PUBLIC -Wno-mismatched-tags)
    target_compile_options(${TARGET_NAME} PUBLIC -Wno-tautological-constant-compare)
endif()

add_subdirectory(examples)