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

if(ANDROID)
    if(ANDROID_ABI STREQUAL "armeabi-v7a")
        target_link_libraries(${TARGET_NAME} 
            $ENV{BOOST_ROOT_ANDROID}/${ANDROID_ABI}/lib/libboost_program_options-clang-mt-a32-1_68.a 
            $ENV{BOOST_ROOT_ANDROID}/${ANDROID_ABI}/lib/libboost_filesystem-clang-mt-a32-1_68.a
            $ENV{BOOST_ROOT_ANDROID}/${ANDROID_ABI}/lib/libboost_system-clang-mt-a32-1_68.a
            $ENV{BOOST_ROOT_ANDROID}/${ANDROID_ABI}/lib/libboost_thread-clang-mt-a32-1_68.a
            $ENV{BOOST_ROOT_ANDROID}/${ANDROID_ABI}/lib/libboost_regex-clang-mt-a32-1_68.a
            $ENV{BOOST_ROOT_ANDROID}/${ANDROID_ABI}/lib/libboost_log-clang-mt-a32-1_68.a)
    elseif(ANDROID_ABI STREQUAL "x86")
        target_link_libraries(${TARGET_NAME} 
            $ENV{BOOST_ROOT_ANDROID}/${ANDROID_ABI}/lib/libboost_program_options-clang-mt-x32-1_68.a 
            $ENV{BOOST_ROOT_ANDROID}/${ANDROID_ABI}/lib/libboost_filesystem-clang-mt-x32-1_68.a
            $ENV{BOOST_ROOT_ANDROID}/${ANDROID_ABI}/lib/libboost_system-clang-mt-x32-1_68.a
            $ENV{BOOST_ROOT_ANDROID}/${ANDROID_ABI}/lib/libboost_thread-clang-mt-x32-1_68.a
            $ENV{BOOST_ROOT_ANDROID}/${ANDROID_ABI}/lib/libboost_regex-clang-mt-x32-1_68.a
            $ENV{BOOST_ROOT_ANDROID}/${ANDROID_ABI}/lib/libboost_log-clang-mt-x32-1_68.a)
    elseif(ANDROID_ABI STREQUAL "arm64-v8a")
        target_link_libraries(${TARGET_NAME} 
            $ENV{BOOST_ROOT_ANDROID}/${ANDROID_ABI}/lib/libboost_program_options-clang-mt-a64-1_68.a 
            $ENV{BOOST_ROOT_ANDROID}/${ANDROID_ABI}/lib/libboost_filesystem-clang-mt-a64-1_68.a
            $ENV{BOOST_ROOT_ANDROID}/${ANDROID_ABI}/lib/libboost_system-clang-mt-a64-1_68.a
            $ENV{BOOST_ROOT_ANDROID}/${ANDROID_ABI}/lib/libboost_thread-clang-mt-a64-1_68.a
            $ENV{BOOST_ROOT_ANDROID}/${ANDROID_ABI}/lib/libboost_regex-clang-mt-a64-1_68.a
            $ENV{BOOST_ROOT_ANDROID}/${ANDROID_ABI}/lib/libboost_log-clang-mt-a64-1_68.a)
    endif()
else()
    find_package(Boost COMPONENTS system thread regex filesystem program_options log locale)
    if (Boost_FOUND)    
        target_link_libraries(${TARGET_NAME} Boost::system Boost::thread Boost::regex Boost::filesystem Boost::program_options Boost::log Boost::locale)
    endif()
endif()

if(MSVC)
    target_compile_options(${TARGET_NAME} PRIVATE "/wd4273")    
endif()

add_subdirectory(examples)