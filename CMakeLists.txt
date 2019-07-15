cmake_minimum_required(VERSION 3.5)

set(VERSION_MAJOR 3)
set(VERSION_MINOR 0)
set(VERSION_REVISION 0)
set(GIT_COMMIT_HASH "unknown")

set(BEAM_SUFFIX_LOWER_CASE "masternet")
set(BEAM_SUFFIX_UPPER_CASE "Masternet")
set(BEAM_TARGET_SUFFIX "-${BEAM_SUFFIX_LOWER_CASE}")
set(BEAM_DISPLAY_SUFFIX " ${BEAM_SUFFIX_UPPER_CASE}")

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

file(WRITE ${PROJECT_SOURCE_DIR}/beam_version.gen "${PROJECT_VERSION}")

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

set(BEAM_USE_AVX FALSE)
if (BEAM_CPU_ARCH MATCHES "avx")
    set(BEAM_USE_AVX TRUE)
endif()

if(BEAM_USE_AVX)
    add_definitions(-DBEAM_USE_AVX)
endif()

set(BEAM_QT_UI_WALLET TRUE)
if (BEAM_NO_QT_UI_WALLET)
    set(BEAM_QT_UI_WALLET FALSE)    
endif()

set(BEAM_USE_STATIC FALSE)
if(BEAM_LINK_TYPE MATCHES "Static")
    set(BEAM_USE_STATIC TRUE)
endif()

if(BEAM_USE_STATIC)
    add_definitions(-DBEAM_USE_STATIC)
endif()

#it's enabled by default for testnet
#if(DEBUG_MESSAGES_IN_RELEASE_MODE)
    add_definitions(-DDEBUG_MESSAGES_IN_RELEASE_MODE)
#endif()
#it's enabled by default for testnet
#add_definitions(-DLOG_VERBOSE_ENABLED=1)

if(SHOW_CODE_LOCATION)
    add_definitions(-DSHOW_CODE_LOCATION=1)
endif()

# uncoment next line for testnet
#set(BEAM_TESTNET TRUE)
# uncoment next line for mainnet
set(BEAM_MAINNET TRUE)

if(BEAM_TESTNET)
    add_definitions(-DBEAM_TESTNET)
    set(BEAM_SUFFIX_LOWER_CASE "testnet")
    set(BEAM_SUFFIX_UPPER_CASE "Testnet")
    set(BEAM_TARGET_SUFFIX "-${BEAM_SUFFIX_LOWER_CASE}")
    set(BEAM_DISPLAY_SUFFIX " ${BEAM_SUFFIX_UPPER_CASE}")
elseif(BEAM_MAINNET)
    add_definitions(-DBEAM_MAINNET)
    set(BEAM_SUFFIX_LOWER_CASE "")
    set(BEAM_SUFFIX_UPPER_CASE "")
    set(BEAM_TARGET_SUFFIX "")
    set(BEAM_DISPLAY_SUFFIX "")
endif()

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

    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /O2")
    set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} /O2")

    SET(CMAKE_EXE_LINKER_FLAGS_RELEASE "${CMAKE_EXE_LINKER_FLAGS_RELEASE} /OPT:REF /INCREMENTAL:NO")
    SET(CMAKE_EXE_LINKER_FLAGS_RELWITHDEBINFO "${CMAKE_EXE_LINKER_FLAGS_RELWITHDEBINFO} /OPT:REF /INCREMENTAL:NO")
    SET(CMAKE_EXE_LINKER_FLAGS_MINSIZEREL "${CMAKE_EXE_LINKER_FLAGS_MINSIZEREL} /OPT:REF /INCREMENTAL:NO")

    add_definitions(-D_WIN32_WINNT=0x0600)

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
        if(ARCH_NATIVE)
            set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -march=native")
        else()
            set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -march=core2")
        endif()
    endif()

    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Werror -pthread")
    set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -O0 -ggdb3")
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3")

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

if(ANDROID)
    message("ANDROID_ABI: ${ANDROID_ABI}")
    add_definitions(-DBOOST_ERROR_CODE_HEADER_ONLY)
    include_directories($ENV{BOOST_ROOT_ANDROID}/include)
else()

    if(LINUX)
        add_definitions(-DBOOST_NO_CXX11_SCOPED_ENUMS)
        add_definitions(-DBOOST_NO_AUTO_PTR)
    endif()

    if (WIN32)
        add_definitions(-DNOMINMAX)
    endif()

    find_package(Boost REQUIRED)

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

include_directories(${PROJECT_SOURCE_DIR})
add_definitions(-DPROJECT_SOURCE_DIR=\"${PROJECT_SOURCE_DIR}\")

include_directories(${PROJECT_SOURCE_DIR}/3rdparty)

if(BEAM_USE_STATIC)

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
        set(OPENSSL_LIBCRYPTO $ENV{OPENSSL_ROOT_DIR}/lib/libcrypto.a)
        set(OPENSSL_LIBSSL $ENV{OPENSSL_ROOT_DIR}/lib/libssl.a)
    elseif(UNIX)
        set(OPENSSL_LIBCRYPTO /usr/lib/x86_64-linux-gnu/libcrypto.a)
        set(OPENSSL_LIBSSL /usr/lib/x86_64-linux-gnu/libssl.a)
    elseif(MSVC)
        set(OPENSSL_LIBCRYPTO $ENV{OPENSSL_ROOT_DIR}/lib/VC/static/libcrypto64MT.lib crypt32.lib)
        set(OPENSSL_LIBSSL $ENV{OPENSSL_ROOT_DIR}/lib/VC/static/libssl64MT.lib)

        if(MSVC_VERSION GREATER_EQUAL 1900)
            set(OPENSSL_LIBCRYPTO ${OPENSSL_LIBCRYPTO} legacy_stdio_definitions.lib)
        endif()
    endif()
else()
    if(ANDROID)
        include_directories($ENV{OPENSSL_ROOT_DIR_ANDROID}/include)
        set(OPENSSL_LIBCRYPTO $ENV{OPENSSL_ROOT_DIR_ANDROID}/libs/${ANDROID_ABI}/libcrypto.a)
        set(OPENSSL_LIBSSL $ENV{OPENSSL_ROOT_DIR_ANDROID}/libs/${ANDROID_ABI}/libssl.a)
    else()
        find_package(OpenSSL REQUIRED)

        if( OpenSSL_FOUND )
            set(OPENSSL_USE_STATIC_LIBS TRUE)
            link_directories(${OPENSSL_LIBRARIES})
            include_directories(${OPENSSL_INCLUDE_DIR})
            message(STATUS "Using OpenSSL ${OPENSSL_VERSION}")
            set(OPENSSL_LIBCRYPTO OpenSSL::Crypto)
            set(OPENSSL_LIBSSL OpenSSL::SSL)
        else()
            message(FATAL_ERROR "error, OpenSSL not found, please install: 'apt-get install libssl-dev' on Ubuntu")
        endif()
    endif()
endif()

if (UV_INTERNAL)
    include_directories(${PROJECT_SOURCE_DIR}/3rdparty/libuv/include)
    add_subdirectory(3rdparty/libuv)
endif()

if(Boost_FOUND)
    add_library(boost INTERFACE IMPORTED)
    set_property(TARGET boost PROPERTY INTERFACE_INCLUDE_DIRECTORIES ${Boost_INCLUDE_DIR})
endif()

add_definitions(-DBC_STATIC)

add_subdirectory(3rdparty/secp256k1-zkp)
add_subdirectory(3rdparty/libbitcoin)
add_subdirectory(3rdparty/sqlite)
add_subdirectory(utility)
add_subdirectory(core)
add_subdirectory(pow)
add_subdirectory(p2p)
add_subdirectory(http)
add_subdirectory(wallet)
add_subdirectory(node)
add_subdirectory(mnemonic)


if(ANDROID OR BEAM_BUILD_JNI)
    add_subdirectory(android)
endif()

# TODO: uncomment this later
if(NOT ANDROID)
    add_subdirectory(beam)
    add_subdirectory(explorer)

    if(BEAM_QT_UI_WALLET)
        add_subdirectory(3rdparty/qrcode)
        add_subdirectory(3rdparty/quazip)
        add_subdirectory(ui)
    endif()
 endif()

########################################################
### PACKAGING ##########################################
########################################################

if (WIN32)
    set(CPACK_GENERATOR "NSIS")
    set(CPACK_SOURCE_GENERATOR "ZIP")
    set(CPACK_NSIS_MUI_ICON "${PROJECT_SOURCE_DIR}/ui/icon.ico")
    set(CPACK_NSIS_MUI_UNIICON "${PROJECT_SOURCE_DIR}/ui/icon.ico")
    set(CPACK_NSIS_INSTALLED_ICON_NAME "${PROJECT_SOURCE_DIR}/ui/icon.ico")
    set(CPACK_NSIS_EXTRA_INSTALL_COMMANDS "CreateShortCut \\\"$DESKTOP\\\\Beam Wallet${BEAM_DISPLAY_SUFFIX}.lnk\\\" \\\"$INSTDIR\\\\Beam Wallet${BEAM_DISPLAY_SUFFIX}.exe\\\"")
    set(CPACK_NSIS_CREATE_ICONS_EXTRA "CreateShortCut \\\"$SMPROGRAMS\\\\$STARTMENU_FOLDER\\\\Beam Wallet${BEAM_DISPLAY_SUFFIX}.lnk\\\" \\\"$INSTDIR\\\\Beam Wallet${BEAM_DISPLAY_SUFFIX}.exe\\\"")
    set(CPACK_NSIS_EXTRA_UNINSTALL_COMMANDS "Delete \\\"$DESKTOP\\\\Beam Wallet${BEAM_DISPLAY_SUFFIX}.lnk\\\"")
    set(CPACK_NSIS_DELETE_ICONS_EXTRA "Delete \\\"$SMPROGRAMS\\\\$MUI_TEMP\\\\Beam Wallet${BEAM_DISPLAY_SUFFIX}.lnk\\\"")

    set(CPACK_NSIS_EXECUTABLES_DIRECTORY .)
    set(CPACK_NSIS_MUI_FINISHPAGE_RUN "Beam Wallet${BEAM_DISPLAY_SUFFIX}.exe") 
    set(CPACK_NSIS_PACKAGE_NAME "Beam Wallet${BEAM_DISPLAY_SUFFIX}")

elseif (APPLE)
    set(CPACK_GENERATOR "Bundle")
    set(CPACK_BUNDLE_NAME "Beam Wallet${BEAM_DISPLAY_SUFFIX}")
    set(CPACK_BUNDLE_PLIST ui/Info.plist)
    set(CPACK_BUNDLE_ICON ui/beam.icns)
    set(CPACK_BUNDLE_STARTUP_COMMAND "ui/Beam Wallet${BEAM_DISPLAY_SUFFIX}")

    if(BEAM_SIGN_MACOS_BINARY)
        set(CPACK_COMMAND_CODESIGN "codesign -o runtime --timestamp")
        set(CPACK_BUNDLE_APPLE_CERT_APP "Developer ID Application: Beam Development Limited (KNU2R94BJK)")
    endif()
elseif (LINUX)
    set(CPACK_GENERATOR "DEB")
    set(CPACK_DEBIAN_PACKAGE_NAME "beamwallet${BEAM_SUFFIX_LOWER_CASE}")
    set(CPACK_DEBIAN_PACKAGE_VERSION ${PROJECT_VERSION})
    set(CPACK_DEBIAN_PACKAGE_MAINTAINER "Beam <mail@beam-mw.com>")
    set(CPACK_DEBIAN_PACKAGE_SECTION "Network")

endif ()

set(CPACK_PACKAGE_NAME "BeamWallet${BEAM_SUFFIX_UPPER_CASE}")
set(CPACK_PACKAGE_VENDOR "Beam Development Limited")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Scalable confidential cryptocurrency")
set(CPACK_PACKAGE_VERSION_MAJOR "${VERSION_MAJOR}")
set(CPACK_PACKAGE_VERSION_MINOR "${VERSION_MINOR}")
set(CPACK_PACKAGE_VERSION_PATCH "${VERSION_REVISION}")
set(CPACK_PACKAGE_VERSION "${CPACK_PACKAGE_VERSION_MAJOR}.${CPACK_PACKAGE_VERSION_MINOR}.${CPACK_PACKAGE_VERSION_PATCH}")
set(CPACK_PACKAGE_INSTALL_DIRECTORY "Beam Wallet${BEAM_DISPLAY_SUFFIX}")

set(CPACK_RESOURCE_FILE_LICENSE "${PROJECT_SOURCE_DIR}/TERMS.txt")

include(CPack)

#if (BEAM_SIGN_PACKAGE AND WIN32)
#    add_custom_target(signing ALL  COMMENT "Signing Package...")
#
# 
#     # Signing the installer
#     add_custom_target(signing-installer)
#     set(BEAM_INSTALLER_NAME ${CPACK_NSIS_PACKAGE_NAME}-${CPACK_PACKAGE_VERSION}-${CPACK_SYSTEM_NAME}.exe)
#     
#     add_custom_command(
#         TARGET signing-installer
#         COMMAND ${SIGNTOOL_EXECUTABLE} sign ${SIGNTOOL_OPTIONS} ${BEAM_INSTALLER_NAME}
#         ## Other optional options:
#         # /tr timestampServerUrl 
#        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
#     )
#endif()
