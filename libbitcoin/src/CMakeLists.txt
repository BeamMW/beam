#set(TARGET_NAME libbitcoin)

#target_include_directories(${CMAKE_CURRENT_SOURCE_DIR})

add_subdirectory(chain)
add_subdirectory(config)
add_subdirectory(formats)
add_subdirectory(log)
add_subdirectory(machine)
add_subdirectory(math)
add_subdirectory(message)
add_subdirectory(unicode)
add_subdirectory(utility)
add_subdirectory(wallet)

#add_library(${TARGET_NAME} STATIC
#    error.cpp
#    ${CHAIN_SOURCES}
#    ${CHAIN_HEADERS}
#    ${CONFIG_SOURCES}
#    ${CONFIG_HEADERS}
#    ${FORMATS_SOURCES}
#    ${FORMATS_HEADERS}
#    ${LOG_SOURCES}
#    ${LOG_HEADERS}
#    ${MACHINE_SOURCES}
#    ${MACHINE_HEADERS}
#    ${MATH_SOURCES}
#    ${MATH_HEADERS}
#    ${EXTERNAL_MATH_SOURCES}
#    ${EXTERNAL_MATH_HEADERS}
#    ${MESSAGE_SOURCES}
#    ${MESSAGE_HEADERS}
#    ${UNICODE_SOURCES}
#    ${UNICODE_HEADERS}
#    ${UTILITY_SOURCES}
#    ${UTILITY_HEADERS}
#    ${WALLET_HEADERS}
#    ${WALLET_SOURCES}
#    ${PARSE_ENCRYPTED_SOURCES})

#ADD_LIBRARY(libbitcoin STATIC
#	error.cpp
#	$<TARGET_OBJECTS:chain>
#	$<TARGET_OBJECTS:config>
#	$<TARGET_OBJECTS:formats>
#	$<TARGET_OBJECTS:log>
#	$<TARGET_OBJECTS:machine>
#	$<TARGET_OBJECTS:math>
#	$<TARGET_OBJECTS:message>
#	$<TARGET_OBJECTS:unicode>
#	$<TARGET_OBJECTS:utility>
#	$<TARGET_OBJECTS:wallet>
#	)
  
#target_link_libraries(libbitcoin)
#file(GLOB_RECURSE sources)