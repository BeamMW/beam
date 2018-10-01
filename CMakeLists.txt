cmake_minimum_required(VERSION 3.1)

set(VERSION_MAJOR 1)
set(VERSION_MINOR 0)
set(VERSION_REVISION 0)
set(GIT_COMMIT_HASH "unknown")

find_package(Git)
if(Git_FOUND)
	execute_process(
		COMMAND ${GIT_EXECUTABLE} status
		WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
		ERROR_VARIABLE RESULT_STRING
		OUTPUT_STRIP_TRAILING_WHITESPACE
	)

	string(LENGTH "${RESULT_STRING}" LENGTH_RESULT_STRING)

	if(${LENGTH_RESULT_STRING} EQUAL 0)
		execute_process(
			COMMAND ${GIT_EXECUTABLE} log -1 --format=%H
			WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
			OUTPUT_VARIABLE GIT_COMMIT_HASH
			OUTPUT_STRIP_TRAILING_WHITESPACE
		)
		execute_process(
			COMMAND ${GIT_EXECUTABLE} rev-list HEAD --count
			WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
			OUTPUT_VARIABLE VERSION_REVISION
			OUTPUT_STRIP_TRAILING_WHITESPACE
		)

		set(APPVEYOR_REPO_BRANCH_EXISTS $ENV{APPVEYOR_REPO_BRANCH})
		set(TRAVIS_BRANCH_EXISTS $ENV{TRAVIS_BRANCH})

        if(NOT DEFINED BRANCH_NAME)
            execute_process(
                COMMAND ${GIT_EXECUTABLE} rev-parse --abbrev-ref HEAD
                WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
                OUTPUT_VARIABLE BRANCH_NAME
                OUTPUT_STRIP_TRAILING_WHITESPACE
            )
        endif()

    endif()
else()
	set(BRANCH_NAME "")
endif()

message("BRANCH_NAME: ${BRANCH_NAME}")

project(beam VERSION ${VERSION_MAJOR}.${VERSION_MINOR}.${VERSION_REVISION})

message("BEAM_VERSION: ${PROJECT_VERSION}")

if(UNIX AND NOT APPLE AND NOT ANDROID)
	set(LINUX TRUE)
endif()

enable_testing()

function(add_test_snippet EXE_NAME LIB_NAME)
    add_executable(${EXE_NAME} ${EXE_NAME}.cpp)
    add_dependencies(${EXE_NAME} ${LIB_NAME})
    target_link_libraries(${EXE_NAME} ${LIB_NAME})
    add_test(NAME ${EXE_NAME} COMMAND $<TARGET_FILE:${EXE_NAME}>)
endfunction()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(BEAM_USE_STATIC FALSE)

if(BEAM_LINK_TYPE MATCHES "Static")
	set(BEAM_USE_STATIC TRUE)
endif()

if(BEAM_USE_STATIC)
	add_definitions(-DBEAM_USE_STATIC)
endif()

if(DEBUG_MESSAGES_IN_RELEASE_MODE)
    add_definitions(-DDEBUG_MESSAGES_IN_RELEASE_MODE)
endif()

if(BRANCH_NAME MATCHES "testnet")
    add_definitions(-DBEAM_TESTNET)
endif()

if(BEAM_TESTNET)
    add_definitions(-DBEAM_TESTNET)
endif()

if(MSVC)
    if(CMAKE_CXX_FLAGS MATCHES "/W[0-4]")
		string(REGEX REPLACE "/W[0-4]" "/W4 /WX" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
	else()
		set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /W4 /WX")
	endif()
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /wd4100") # unreferenced formal parameter

    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /O2")
	set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} /O2")

	SET(CMAKE_EXE_LINKER_FLAGS_RELEASE "${CMAKE_EXE_LINKER_FLAGS_RELEASE} /OPT:REF /INCREMENTAL:NO")
	SET(CMAKE_EXE_LINKER_FLAGS_RELWITHDEBINFO "${CMAKE_EXE_LINKER_FLAGS_RELWITHDEBINFO} /OPT:REF /INCREMENTAL:NO")
	SET(CMAKE_EXE_LINKER_FLAGS_MINSIZEREL "${CMAKE_EXE_LINKER_FLAGS_MINSIZEREL} /OPT:REF /INCREMENTAL:NO")

	if(BEAM_USE_STATIC)
		# use static runtime
		foreach(flag_var CMAKE_CXX_FLAGS CMAKE_CXX_FLAGS_DEBUG CMAKE_CXX_FLAGS_RELEASE
			CMAKE_CXX_FLAGS_MINSIZEREL CMAKE_CXX_FLAGS_RELWITHDEBINFO
			flag_var CMAKE_C_FLAGS CMAKE_C_FLAGS_DEBUG CMAKE_C_FLAGS_RELEASE
			CMAKE_C_FLAGS_MINSIZEREL CMAKE_C_FLAGS_RELWITHDEBINFO)

			if(${flag_var} MATCHES "/MD")
				string(REGEX REPLACE "/MD" "/MT" ${flag_var} "${${flag_var}}")
			endif(${flag_var} MATCHES "/MD")
		endforeach(flag_var)
	endif()

else()
	if(NOT ANDROID)
		set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -march=native")
	endif()

	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Werror -pthread")
    set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -O0 -ggdb3")
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3")

	# disable some warnings
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-overloaded-virtual") # appears alot in our macro expansions
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-unused-const-variable") # so what?
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-unused-function") # mostly in 3rd-party libs

endif()

if(UNIX OR WIN32)
    set(UV_INTERNAL ON)
    add_definitions(-DUV_INTERNAL)
endif()

if(ANDROID)
    include_directories($ENV{BOOST_ROOT_ANDROID}/include/boost-1_68)
else()
    if(BEAM_USE_STATIC)
	include(FindBoost)

	set(Boost_USE_MULTITHREADED ON)
	set(Boost_USE_STATIC_LIBS ON)
	set(Boost_USE_STATIC_RUNTIME ON)
    else()
	if (WIN32)
		add_definitions( -DBOOST_ALL_DYN_LINK )
	endif()
    endif()
endif()

if (WIN32)
    add_definitions(-DNOMINMAX)
    find_package(Boost)
    if (Boost_FOUND)
        include_directories(${Boost_INCLUDE_DIRS})
    endif()
endif()

include_directories(${PROJECT_SOURCE_DIR})
add_definitions(-DPROJECT_SOURCE_DIR="${PROJECT_SOURCE_DIR}")

include_directories(${PROJECT_SOURCE_DIR}/utility) # TODO  move to ./3rdparty all that stuff that requires to add this line

add_subdirectory(utility)
add_subdirectory(core)
add_subdirectory(pow)
add_subdirectory(p2p)
add_subdirectory(wallet)
add_subdirectory(sqlite)
add_subdirectory(beam)

# TODO: uncomment this later
if(NOT ANDROID)
	add_subdirectory(ui)
endif()

########################################################
### PACKAGING ##########################################
########################################################

if (WIN32)
    set(CPACK_GENERATOR "NSIS")
    set(CPACK_SOURCE_GENERATOR "ZIP")
    set(CPACK_NSIS_MUI_ICON "${CMAKE_SOURCE_DIR}/ui/icon.ico")
    set(CPACK_NSIS_MUI_UNIICON "${CMAKE_SOURCE_DIR}/ui/icon.ico")
	set(CPACK_NSIS_INSTALLED_ICON_NAME "${CMAKE_SOURCE_DIR}/ui/icon.ico")
	set(CPACK_NSIS_EXTRA_INSTALL_COMMANDS "CreateShortCut \\\"$DESKTOP\\\\Beam Wallet.lnk\\\" \\\"$INSTDIR\\\\Beam Wallet.exe\\\"")
	set(CPACK_NSIS_EXTRA_UNINSTALL_COMMANDS "Delete \\\"$DESKTOP\\\\Beam Wallet.lnk\\\"")

elseif (APPLE)
	set(CPACK_GENERATOR "DragNDrop")
elseif (LINUX)
	set(CPACK_GENERATOR "DEB")
	set(CPACK_DEBIAN_PACKAGE_NAME "beamwallet")
	set(CPACK_DEBIAN_PACKAGE_VERSION ${PROJECT_VERSION})
	set(CPACK_DEBIAN_PACKAGE_MAINTAINER "Beam <mail@beam-mw.com>")
	set(CPACK_DEBIAN_PACKAGE_SECTION "Network")

endif ()


set(CPACK_PACKAGE_NAME "BeamWallet")
set(CPACK_PACKAGE_VENDOR "Beam Vendor")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Scalable confidential cryptocurrency")
set(CPACK_PACKAGE_VERSION_MAJOR "0")
set(CPACK_PACKAGE_VERSION_MINOR "0")
set(CPACK_PACKAGE_VERSION_PATCH "1")
set(CPACK_PACKAGE_VERSION "${CPACK_PACKAGE_VERSION_MAJOR}.${CPACK_PACKAGE_VERSION_MINOR}.${CPACK_PACKAGE_VERSION_PATCH}")
set(CPACK_PACKAGE_INSTALL_DIRECTORY "Beam")

set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_SOURCE_DIR}/LICENSE")

include(CPack)
