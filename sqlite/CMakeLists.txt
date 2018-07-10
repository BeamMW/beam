
add_definitions( -DSQLITE_HAS_CODEC)

set(SQLITE_SRC
    sqlite3.c
)

add_library(sqlite STATIC ${SQLITE_SRC})

if(${BEAM_USE_STATIC})

	if(MSVC)
		include_directories($ENV{OPENSSL_ROOT_DIR}/include)
	else()
		find_package(OpenSSL REQUIRED)

		if( OpenSSL_FOUND )
			set(OPENSSL_USE_STATIC_LIBS TRUE)
			link_directories(${OPENSSL_LIBRARIES})
			include_directories(${OPENSSL_INCLUDE_DIR})
			message(STATUS "Using OpenSSL ${OPENSSL_VERSION}")
		else()
			message(FATAL_ERROR "error, OpenSSL not found, please install: 'apt-get install libssl-dev' on Ubuntu")
		endif()
	endif()

	# cmake can't find static lib on macos, harcoded
	if(APPLE)
		set(STATIC_OPENSSL $ENV{OPENSSL_ROOT_DIR}/lib/libcrypto.a)
	elseif(UNIX)
		set(STATIC_OPENSSL /usr/lib/x86_64-linux-gnu/libcrypto.a)
	elseif(MSVC)
		set(STATIC_OPENSSL $ENV{OPENSSL_ROOT_DIR}/lib/VC/static/libcrypto64MT.lib crypt32.lib)

		if(MSVC_VERSION GREATER_EQUAL 1900)
			set(STATIC_OPENSSL ${STATIC_OPENSSL} legacy_stdio_definitions.lib)
		endif()
	endif()

	target_link_libraries(sqlite ${STATIC_OPENSSL})
else()
	find_package(OpenSSL REQUIRED)

	if( OpenSSL_FOUND )
		set(OPENSSL_USE_STATIC_LIBS TRUE)
		link_directories(${OPENSSL_LIBRARIES})
		include_directories(${OPENSSL_INCLUDE_DIR})
		message(STATUS "Using OpenSSL ${OPENSSL_VERSION}")
	else()
		message(FATAL_ERROR "error, OpenSSL not found, please install: 'apt-get install libssl-dev' on Ubuntu")
	endif()

	target_link_libraries(sqlite OpenSSL::Crypto)
endif()

if(UNIX)
    target_link_libraries(sqlite dl pthread)
endif()
