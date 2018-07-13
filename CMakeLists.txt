cmake_minimum_required(VERSION 3.1)
project(beam)

if(UNIX AND NOT APPLE)
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
	add_definitions(-DBEAM_USE_STATIC)
endif()

if(MSVC)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /W4")
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /O2")
	set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} /O2")

	SET(CMAKE_EXE_LINKER_FLAGS_RELEASE "${CMAKE_EXE_LINKER_FLAGS_RELEASE} /OPT:REF /INCREMENTAL:NO")
	SET(CMAKE_EXE_LINKER_FLAGS_RELWITHDEBINFO "${CMAKE_EXE_LINKER_FLAGS_RELWITHDEBINFO} /OPT:REF /INCREMENTAL:NO")
	SET(CMAKE_EXE_LINKER_FLAGS_MINSIZEREL "${CMAKE_EXE_LINKER_FLAGS_MINSIZEREL} /OPT:REF /INCREMENTAL:NO")


	if(${BEAM_USE_STATIC})
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
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -march=native -Wall -pthread")
    set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -O0 -ggdb3")
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3")
endif()

if(UNIX OR WIN32)
    set(UV_INTERNAL ON)
    add_definitions(-DUV_INTERNAL)
endif()

if(${BEAM_USE_STATIC})
	include(FindBoost)

	set(Boost_USE_MULTITHREADED ON)
	set(Boost_USE_STATIC_LIBS ON)
	set(Boost_USE_STATIC_RUNTIME ON)
else()
	if (WIN32)
		add_definitions( -DBOOST_ALL_DYN_LINK )
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
add_subdirectory(bbs)

if(NOT LINUX)
	add_subdirectory(ui)
endif()
