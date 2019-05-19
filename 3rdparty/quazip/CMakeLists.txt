set (TARGET_NAME "quazip_static")

set (CMAKE_PREFIX_PATH $ENV{QT5_ROOT_DIR})

if(LINUX)
	set(CMAKE_POSITION_INDEPENDENT_CODE ON)
endif()

find_package(Qt5 COMPONENTS Core REQUIRED)

set(QUAZIP_SRCS
    quazip/JlCompress.cpp
    quazip/qioapi.cpp
    quazip/quaadler32.cpp
    quazip/quacrc32.cpp
    quazip/quagzipfile.cpp
    quazip/quaziodevice.cpp
    quazip/quazip.cpp
    quazip/quazipdir.cpp
    quazip/quazipfile.cpp
    quazip/quazipfileinfo.cpp
    quazip/quazipnewinfo.cpp
    quazip/unzip.c
    quazip/zip.c
)

set(PUBLIC_HEADERS
    quazip/ioapi.h
    quazip/JlCompress.h
    quazip/minizip_crypt.h
    quazip/quaadler32.h
    quazip/quachecksum32.h
    quazip/quacrc32.h
    quazip/quagzipfile.h
    quazip/quaziodevice.h
    quazip/quazip.h
    quazip/quazipdir.h
    quazip/quazipfile.h
    quazip/quazipfileinfo.h
    quazip/quazipnewinfo.h
    quazip/unzip.h
    quazip/zip.h
)

qt5_wrap_cpp(MOC_SRCS ${PUBLIC_HEADERS})
set(QUAZIP_ALL_SRCS ${QUAZIP_SRCS} ${MOC_SRCS})

add_library(${TARGET_NAME} STATIC ${QUAZIP_ALL_SRCS})

add_definitions(-DQUAZIP_STATIC)

target_include_directories(${TARGET_NAME} PRIVATE ${Qt5Core_INCLUDE_DIRS})
target_include_directories(${TARGET_NAME} PRIVATE ${CMAKE_PREFIX_PATH}/include/QtZlib)
target_include_directories(${TARGET_NAME} PRIVATE ${PROJECT_SOURCE_DIR}/3rdparty/quazip)

if(MSVC)
    target_compile_options(${TARGET_NAME} PRIVATE "/wd4456")
    target_compile_options(${TARGET_NAME} PRIVATE "/wd4131")
    target_compile_options(${TARGET_NAME} PRIVATE "/wd4244")
endif()
if(UNIX)
	target_link_libraries(${TARGET_NAME} z)
endif()