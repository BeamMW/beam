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

find_package(Boost COMPONENTS system thread regex filesystem program_options log locale)
if (Boost_FOUND)    
    target_link_libraries(${TARGET_NAME} Boost::system Boost::thread Boost::regex Boost::filesystem Boost::program_options Boost::log Boost::locale)
endif()

if(MSVC)
    target_compile_options(${TARGET_NAME} PRIVATE "/wd4273")    
endif()

add_subdirectory(examples)