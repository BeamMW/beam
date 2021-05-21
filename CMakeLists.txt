cmake_minimum_required(VERSION 3.13)
cmake_policy(SET CMP0074 NEW)

set(VERSION_MAJOR 6)
set(VERSION_MINOR 1 )
set(VERSION_REVISION 0)
set(GIT_COMMIT_HASH "unknown")

set(BEAM_SUFFIX_LOWER_CASE "masternet" CACHE INTERNAL "")
set(BEAM_SUFFIX_UPPER_CASE "Masternet" CACHE INTERNAL "")
set(BEAM_TARGET_SUFFIX "-${BEAM_SUFFIX_LOWER_CASE}" CACHE INTERNAL "")
set(BEAM_DISPLAY_SUFFIX " ${BEAM_SUFFIX_UPPER_CASE}" CACHE INTERNAL "")

if(IOS)
    add_compile_definitions(_LIBCPP_ENABLE_CXX17_REMOVED_AUTO_PTR)

    if (POLICY CMP0025)
        cmake_policy(SET CMP0025 NEW)
    endif ()
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Winvalid-ios-deployment-target")
endif()

if (BEAM_SIGN_PACKAGE AND WIN32)
  #  add_custom_target(signing ALL DEPENDS beam-wallet-ui COMMENT "Signing Package...")

    find_program(SIGNTOOL_EXECUTABLE signtool.exe)
    message("SignTool: ${SIGNTOOL_EXECUTABLE}" )

    set(WINDOWS_IDENTITIY_PFX_FILE "THE_PFX_FILE_IS_MISSING_HERE!" CACHE STRING "PFX file, which represents the identity of the developer.")
    set(WINDOWS_IDENTITIY_PFX_PASSWORD "" CACHE STRING "Password, which unlocks the PFX file (optional)")
    set(BEAM_SIGN_SUBJECT_NAME "" CACHE STRING "Beam subjet")
    set(SIGNTOOL_OPTIONS /tr http://timestamp.digicert.com /td sha256 /fd sha256 /n "${BEAM_SIGN_SUBJECT_NAME}")
    
    #set(SIGNTOOL_OPTIONS /fd SHA256 /a /f ${WINDOWS_IDENTITIY_PFX_FILE} /tr http://timestamp.digicert.com  )
    
    #if(NOT ${WINDOWS_IDENTITIY_PFX_PASSWORD} EQUAL "")
    #    set(SIGNTOOL_OPTIONS ${SIGNTOOL_OPTIONS} /p ${WINDOWS_IDENTITIY_PFX_PASSWORD})
    #else()
    #    message(FATAL_ERROR "You can't sign your executables without passing your password here!")
    #endif()

    #if(EXISTS ${WINDOWS_IDENTITIY_PFX_FILE})
    #    message(STATUS "Signing executables with: " ${WINDOWS_IDENTITIY_PFX_FILE})
    #    if(${WINDOWS_IDENTITIY_PFX_PASSWORD})
    #        message(WARNING "USE WITH CAUTION: Password for the PFX file has been set!")
    #    endif()
    #else()
    #    message(WARNING "Could not find the PFX file. Can not sign the executables!")
    #endif()
endif()

find_package(Git)
if(Git_FOUND)
    execute_process(
        COMMAND ${GIT_EXECUTABLE} status
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        ERROR_VARIABLE RESULT_STRING
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )

    string(LENGTH "${RESULT_STRING}" LENGTH_RESULT_STRING)

    if(${LENGTH_RESULT_STRING} EQUAL 0)
        execute_process(
            COMMAND ${GIT_EXECUTABLE} log -1 --format=%H
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
            OUTPUT_VARIABLE GIT_COMMIT_HASH
            OUTPUT_STRIP_TRAILING_WHITESPACE
        )
        execute_process(
            COMMAND ${GIT_EXECUTABLE} rev-list HEAD --count
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
            OUTPUT_VARIABLE VERSION_REVISION
            OUTPUT_STRIP_TRAILING_WHITESPACE
        )

        set(APPVEYOR_REPO_BRANCH_EXISTS $ENV{APPVEYOR_REPO_BRANCH})
        set(TRAVIS_BRANCH_EXISTS $ENV{TRAVIS_BRANCH})

        if(NOT DEFINED BRANCH_NAME)
            execute_process(
                COMMAND ${GIT_EXECUTABLE} rev-parse --abbrev-ref HEAD
                WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
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

set(BEAM_VERSION ${PROJECT_VERSION} CACHE INTERNAL "")
set(BEAM_VERSION_MAJOR ${VERSION_MAJOR} CACHE INTERNAL "")
set(BEAM_VERSION_MINOR ${VERSION_MINOR} CACHE INTERNAL "")
set(BEAM_VERSION_REVISION ${VERSION_REVISION} CACHE INTERNAL "")
set(BEAM_BRANCH_NAME ${BRANCH_NAME} CACHE INTERNAL "")

message("BEAM_VERSION: ${PROJECT_VERSION}")

# imported target to setup dependency of beam
add_library(beam INTERFACE IMPORTED GLOBAL)
target_include_directories(beam INTERFACE 
        ${PROJECT_SOURCE_DIR}
        ${PROJECT_SOURCE_DIR}/3rdparty)

target_compile_features(beam INTERFACE cxx_std_17)


file(WRITE ${PROJECT_SOURCE_DIR}/beam_version.gen "${PROJECT_VERSION}")
add_definitions(-DBEAM_LIB_VERSION="${BEAM_VERSION}")

if(UNIX AND NOT APPLE AND NOT ANDROID)
    set(LINUX TRUE CACHE INTERNAL "")
endif()

option(BEAM_TESTS_ENABLED "Enable unit tests" TRUE)
if (BEAM_TESTS_ENABLED)
    enable_testing()

    function(add_test_snippet EXE_NAME LIB_NAME)
        add_executable(${EXE_NAME} ${EXE_NAME}.cpp)
        target_link_libraries(${EXE_NAME} ${LIB_NAME} ${ARGN})
        add_test(NAME ${EXE_NAME} COMMAND $<TARGET_FILE:${EXE_NAME}>)
    endfunction()
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(BEAM_USE_AVX FALSE)
if (BEAM_CPU_ARCH MATCHES "avx")
    set(BEAM_USE_AVX TRUE)
endif()

if(BEAM_USE_AVX)
    add_definitions(-DBEAM_USE_AVX)
endif()

option(BEAM_USE_STATIC "Build with staticaly linked libraries " FALSE)
option(BEAM_USE_STATIC_RUNTIME "Build with staticaly linked runtime" FALSE)
if(BEAM_LINK_TYPE MATCHES "Static")
    set(BEAM_USE_STATIC TRUE)
endif()

if(BEAM_USE_STATIC)
    target_compile_definitions(beam INTERFACE -DBEAM_USE_STATIC)
    add_definitions(-DBEAM_USE_STATIC)
endif()

option(BEAM_WALLET_CLIENT_LIBRARY "Build only binaries required for client app" FALSE)

#it's enabled by default for testnet
#if(DEBUG_MESSAGES_IN_RELEASE_MODE)
    target_compile_definitions(beam INTERFACE -DDEBUG_MESSAGES_IN_RELEASE_MODE)
    add_definitions(-DDEBUG_MESSAGES_IN_RELEASE_MODE)
#endif()
#it's enabled by default for testnet
add_definitions(-DLOG_VERBOSE_ENABLED=1)

if(SHOW_CODE_LOCATION)
    target_compile_definitions(beam INTERFACE -DSHOW_CODE_LOCATION=1)
    add_definitions(-DSHOW_CODE_LOCATION=1)
endif()

option(SWAP_MAINNET "Use mainnet blockchain when perform atomic swaps" FALSE)

# uncomment next line for testnet
#set(BEAM_TESTNET TRUE CACHE INTERNAL "")
# uncomment next line for mainnet
#set(BEAM_MAINNET TRUE CACHE INTERNAL "")

# uncomment next line for swap mainnet
#set(SWAP_MAINNET TRUE)

if(BEAM_TESTNET)
    target_compile_definitions(beam INTERFACE -DBEAM_TESTNET)
    add_definitions(-DBEAM_TESTNET)
    set(BEAM_SUFFIX_LOWER_CASE "testnet" CACHE INTERNAL "")
    set(BEAM_SUFFIX_UPPER_CASE "Testnet" CACHE INTERNAL "")
    set(BEAM_TARGET_SUFFIX "-${BEAM_SUFFIX_LOWER_CASE}" CACHE INTERNAL "")
    set(BEAM_DISPLAY_SUFFIX " ${BEAM_SUFFIX_UPPER_CASE}" CACHE INTERNAL "")
elseif(BEAM_MAINNET)
    target_compile_definitions(beam INTERFACE -DBEAM_MAINNET)
    add_definitions(-DBEAM_MAINNET)
    set(BEAM_SUFFIX_LOWER_CASE "" CACHE INTERNAL "")
    set(BEAM_SUFFIX_UPPER_CASE "" CACHE INTERNAL "")
    set(BEAM_TARGET_SUFFIX "" CACHE INTERNAL "")
    set(BEAM_DISPLAY_SUFFIX "" CACHE INTERNAL "")
endif()

if(SWAP_MAINNET)
    message("SWAP_MAINNET")
    add_definitions(-DSWAP_MAINNET)
endif()

option(BEAM_ATOMIC_SWAP_SUPPORT "Build wallet with atomic swap support" ON)
option(BEAM_LELANTUS_SUPPORT "Build wallet with lelantus support" ON)
option(BEAM_LASER_SUPPORT "Build wallet with laser support" ON)
option(BEAM_CONFIDENTIAL_ASSETS_SUPPORT "Build wallet with confidential assets support" ON)
option(BEAM_HW_WALLET "Build with hardware wallet support" OFF)

if(BEAM_HW_WALLET)

    if(BEAM_USE_STATIC)
        set(BUILD_SHARED_LIBS OFF CACHE BOOL "")

        if(WIN32)
            set(CURL_STATIC_CRT ON CACHE BOOL "")
        endif()

        add_definitions(-DCURL_STATICLIB)
    else()        
        if(WIN32)
            set(protobuf_MSVC_STATIC_RUNTIME OFF CACHE BOOL "")
        endif()
    endif()

    set(protobuf_BUILD_TESTS OFF CACHE BOOL "")
    set(protobuf_BUILD_PROTOC_BINARIES OFF CACHE BOOL "")

    add_subdirectory(3rdparty/protobuf/cmake)
endif()

if(MSVC)
    if(CMAKE_CXX_FLAGS MATCHES "/W[0-4]")
		string(REGEX REPLACE "/W[0-4]" "/W4 /WX" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
    else()
		set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /W4 /WX")
    endif()

    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /wd4100") # unreferenced formal parameter
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /wd4324") # '__blake2b_state': structure was padded due to alignment specifier
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /Zc:__cplusplus")

    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /O2")
    set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} /O2")

    SET(CMAKE_EXE_LINKER_FLAGS_RELEASE "${CMAKE_EXE_LINKER_FLAGS_RELEASE} /OPT:REF /INCREMENTAL:NO")
    SET(CMAKE_EXE_LINKER_FLAGS_RELWITHDEBINFO "${CMAKE_EXE_LINKER_FLAGS_RELWITHDEBINFO} /OPT:REF /INCREMENTAL:NO")
    SET(CMAKE_EXE_LINKER_FLAGS_MINSIZEREL "${CMAKE_EXE_LINKER_FLAGS_MINSIZEREL} /OPT:REF /INCREMENTAL:NO")

    target_compile_definitions(beam INTERFACE -D_WIN32_WINNT=0x0600)
    add_definitions(-D_WIN32_WINNT=0x0600)

    if(BEAM_USE_STATIC_RUNTIME)
        #target_compile_options(beam INTERFACE $<IF:$<CONFIG:Debug>,/MTd, /MT>)
        #target_link_options(beam INTERFACE $<IF:$<NOT:$<CONFIG:Debug>>, /OPT:REF /INCREMENTAL:NO, >)
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
    if(NOT ANDROID AND NOT IOS AND NOT EMSCRIPTEN)
        if(ARCH_NATIVE)
            set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -march=native")
        else()
            set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -march=core2")
        endif()
    endif()

    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -pthread ")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Werror -pthread -fvisibility=hidden")
    set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -O0 -ggdb3")
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3")

    if (EMSCRIPTEN)
        set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -s USE_PTHREADS=1 -s DISABLE_EXCEPTION_CATCHING=0 -s USE_BOOST_HEADERS=1")
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-unused-command-line-argument") # mostly in 3rd-party libs
    endif()

    # disable some warnings
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-overloaded-virtual") # appears alot in our macro expansions
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-unused-const-variable") # so what?
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-unused-function") # mostly in 3rd-party libs
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-unused-value") # proto.h
endif()

if(BEAM_HW_WALLET)
    set(HTTP_ONLY ON CACHE BOOL "")
    set(BUILD_CURL_EXE OFF CACHE BOOL "")
    add_subdirectory(3rdparty/curl)
endif()

if(UNIX OR WIN32)
    set(UV_INTERNAL ON)
    add_definitions(-DUV_INTERNAL)
endif()

#Boost
if (NOT EMSCRIPTEN)
    set(BEAM_BOOST_COMPONENTS system filesystem program_options thread regex log locale date_time)
endif()
function(CreateBoostComponentTargets listOfComponents root libPrefix libSuffix)
    add_library(Boost::boost INTERFACE IMPORTED)
    set (INCLUDE_LOCATION "${root}/include")
    set_target_properties(Boost::boost PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES ${INCLUDE_LOCATION})

    message(STATUS "Boost includes: ${INCLUDE_LOCATION}")

    foreach(COMPONENT ${listOfComponents})
        set(COMPONENT_LOCATION ${root}/${libPrefix}${COMPONENT}${libSuffix})
        message(STATUS "${COMPONENT_LOCATION}")
        if (NOT TARGET Boost::${COMPONENT})
       # if (EXISTS(${COMPONENT_LOCATION}))
            add_library(Boost::${COMPONENT} STATIC IMPORTED)
            set_target_properties(Boost::${COMPONENT} PROPERTIES
                INTERFACE_INCLUDE_DIRECTORIES ${INCLUDE_LOCATION})
            set_target_properties(Boost::${COMPONENT} PROPERTIES
                IMPORTED_LINK_INTERFACE_LANGUAGES "CXX"
                IMPORTED_LOCATION ${COMPONENT_LOCATION})
       else()
           message(STATUS "Boost::${COMPONENT} is already defined")
       endif()
    endforeach()
endfunction()

if(IOS)
    set(BEAM_WALLET_CLIENT_LIBRARY TRUE)
    set(BEAM_TESTS_ENABLED FALSE)
    set(BEAM_ATOMIC_SWAP_SUPPORT OFF)
    set(BEAM_LASER_SUPPORT OFF)
    add_compile_definitions(BOOST_NO_AUTO_PTR)
    add_definitions(-DBOOST_ERROR_CODE_HEADER_ONLY)
    CreateBoostComponentTargets("${BEAM_BOOST_COMPONENTS}" "$ENV{BOOST_ROOT_IOS}" "lib/libboost_" ".a")
elseif(ANDROID)
    message("ANDROID_ABI: ${ANDROID_ABI}")
    set(BEAM_WALLET_CLIENT_LIBRARY TRUE)
    set(BEAM_TESTS_ENABLED FALSE)
    set(BEAM_ATOMIC_SWAP_SUPPORT OFF)
    set(BEAM_LASER_SUPPORT OFF)
    add_compile_definitions(BOOST_ERROR_CODE_HEADER_ONLY)
    add_definitions(-DBOOST_ERROR_CODE_HEADER_ONLY)

    set(ANDROID_SUFFIX "")

    if(ANDROID_ABI STREQUAL "armeabi-v7a")
        set(ANDROID_SUFFIX "-clang-mt-a32-1_68.a")
    elseif(ANDROID_ABI STREQUAL "x86")
        set(ANDROID_SUFFIX "-clang-mt-x32-1_68.a")
    elseif(ANDROID_ABI STREQUAL "x86_64")
        set(ANDROID_SUFFIX "-clang-mt-x64-1_68.a")
    elseif(ANDROID_ABI STREQUAL "arm64-v8a")
        set(ANDROID_SUFFIX "-clang-mt-a64-1_68.a")
    endif()

    CreateBoostComponentTargets("${BEAM_BOOST_COMPONENTS}" "$ENV{BOOST_ROOT_ANDROID}" "libs/${ANDROID_ABI}/libboost_" "${ANDROID_SUFFIX}")   
else()

    if(LINUX)
        target_compile_definitions(beam INTERFACE
            -DBOOST_NO_CXX11_SCOPED_ENUMS
            -DBOOST_NO_AUTO_PTR)
        add_definitions(-DBOOST_NO_CXX11_SCOPED_ENUMS)
        add_definitions(-DBOOST_NO_AUTO_PTR)
    endif()

    if (WIN32)
        target_compile_definitions(beam INTERFACE -DNOMINMAX)
        add_definitions(-DNOMINMAX)
    endif()

    if(BEAM_USE_STATIC)
        set(Boost_USE_MULTITHREADED ON)
        set(Boost_USE_STATIC_LIBS ON)
        set(Boost_USE_STATIC_RUNTIME ${BEAM_USE_STATIC_RUNTIME})
    else()
        if (WIN32)
            target_compile_definitions(beam INTERFACE -DBOOST_ALL_DYN_LINK )
            add_definitions( -DBOOST_ALL_DYN_LINK )
        endif()
    endif()

    #set (Boost_VERBOSE TRUE)
    if(NOT EMSCRIPTEN)
        find_package(Boost 1.66 REQUIRED COMPONENTS ${BEAM_BOOST_COMPONENTS})
    else()
        find_package(Boost 1.66)
    endif()
endif()

#Boost end

include_directories(${PROJECT_SOURCE_DIR})
add_definitions(-DPROJECT_SOURCE_DIR=\"${PROJECT_SOURCE_DIR}\")

include_directories(${PROJECT_SOURCE_DIR}/3rdparty)

#OpenSSL

if(BEAM_USE_STATIC)
    set(OPENSSL_USE_STATIC_LIBS TRUE)
    set(OPENSSL_MSVC_STATIC_RT ${BEAM_USE_STATIC_RUNTIME})
    find_package(OpenSSL REQUIRED)
else()
    if(ANDROID)

        add_library(OpenSSL::Crypto STATIC IMPORTED)
        set_target_properties(OpenSSL::Crypto PROPERTIES
            INTERFACE_INCLUDE_DIRECTORIES "$ENV{OPENSSL_ROOT_DIR_ANDROID}/include")
        set_target_properties(OpenSSL::Crypto PROPERTIES
            IMPORTED_LINK_INTERFACE_LANGUAGES "C"
            IMPORTED_LOCATION "$ENV{OPENSSL_ROOT_DIR_ANDROID}/libs/${ANDROID_ABI}/libcrypto.a")

        add_library(OpenSSL::SSL STATIC IMPORTED)
        set_target_properties(OpenSSL::SSL PROPERTIES
            INTERFACE_INCLUDE_DIRECTORIES "$ENV{OPENSSL_ROOT_DIR_ANDROID}/include")
        set_target_properties(OpenSSL::SSL PROPERTIES
            IMPORTED_LINK_INTERFACE_LANGUAGES "C"
            IMPORTED_LOCATION "$ENV{OPENSSL_ROOT_DIR_ANDROID}/libs/${ANDROID_ABI}/libssl.a")

    else()
        find_package(OpenSSL REQUIRED)
    endif()
endif()

#OpenSSL end

target_compile_definitions(beam INTERFACE -DBC_STATIC)
add_definitions(-DBC_STATIC)

add_subdirectory(3rdparty)
add_subdirectory(utility)
add_subdirectory(core)
add_subdirectory(pow)
add_subdirectory(p2p)
add_subdirectory(http)
add_subdirectory(keykeeper)
add_subdirectory(wallet)
add_subdirectory(hw_crypto)
add_subdirectory(bvm)
add_subdirectory(node)
add_subdirectory(mnemonic)
add_subdirectory(websocket)

if (EMSCRIPTEN)
    add_subdirectory(wasmclient)
endif()

if(ANDROID OR BEAM_BUILD_JNI)
    add_subdirectory(android)
endif()

if(NOT BEAM_WALLET_CLIENT_LIBRARY)
    add_subdirectory(beam)
    add_subdirectory(explorer)
endif()
