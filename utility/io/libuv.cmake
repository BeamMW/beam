set(UV_SRC_DIR ${PROJECT_SOURCE_DIR}/utility/io/libuv/src)
set(UV_INCLUDE_DIRS ${PROJECT_SOURCE_DIR}/utility/io/libuv/include)

set(UV_SOURCES
    ${UV_SRC_DIR}/fs-poll.c
    ${UV_SRC_DIR}/inet.c
    ${UV_SRC_DIR}/threadpool.c
    ${UV_SRC_DIR}/uv-common.c
    ${UV_SRC_DIR}/version.c)

if(${CMAKE_SYSTEM_NAME} STREQUAL "Windows")
    set(UV_COMPILE_DEFS WIN32_LEAN_AND_MEAN _WIN32_WINNT=0x0600 _CRT_SECURE_NO_WARNINGS)
    set(UV_INCLUDE_DIRS ${UV_INCLUDE_DIRS} ${UV_SRC_DIR}/src/win)
    set(UV_SOURCES ${UV_SOURCES}
        ${UV_SRC_DIR}/win/async.c
        ${UV_SRC_DIR}/win/core.c
        ${UV_SRC_DIR}/win/dl.c
        ${UV_SRC_DIR}/win/error.c
        ${UV_SRC_DIR}/win/fs-event.c
        ${UV_SRC_DIR}/win/fs.c
        ${UV_SRC_DIR}/win/getaddrinfo.c
        ${UV_SRC_DIR}/win/getnameinfo.c
        ${UV_SRC_DIR}/win/handle.c
        ${UV_SRC_DIR}/win/loop-watcher.c
        ${UV_SRC_DIR}/win/pipe.c
        ${UV_SRC_DIR}/win/poll.c
        ${UV_SRC_DIR}/win/process-stdio.c
        ${UV_SRC_DIR}/win/process.c
        ${UV_SRC_DIR}/win/req.c
        ${UV_SRC_DIR}/win/signal.c
        ${UV_SRC_DIR}/win/stream.c
        ${UV_SRC_DIR}/win/tcp.c
        ${UV_SRC_DIR}/win/thread.c
        ${UV_SRC_DIR}/win/timer.c
        ${UV_SRC_DIR}/win/tty.c
        ${UV_SRC_DIR}/win/udp.c
        ${UV_SRC_DIR}/win/util.c
        ${UV_SRC_DIR}/win/winapi.c
        ${UV_SRC_DIR}/win/winsock.c)

        set(UV_LDFLAGS advapi32 iphlpapi psapi userenv shell32 ws2_32)
else()
    set(UV_FLAGS -Wall -Wextra -Wno-unused-parameter -pedantic -march=native)

    if(CMAKE_BUILD_TYPE MATCHES "Debug")
        list(APPEND UV_FLAGS -O0 -ggdb3)
    else()
        list(APPEND UV_FLAGS -O3)
    endif()

    set(UV_LDFLAGS dl)

    set(UV_COMPILE_DEFS _LARGEFILE_SOURCE _FILE_OFFSET_BITS=64)

    set(UV_INCLUDE_DIRS ${UV_INCLUDE_DIRS} ${UV_SRC_DIR} ${UV_SRC_DIR}/unix)
    set(UV_SOURCES ${UV_SOURCES}
        ${UV_SRC_DIR}/unix/async.c
        ${UV_SRC_DIR}/unix/core.c
        ${UV_SRC_DIR}/unix/dl.c
        ${UV_SRC_DIR}/unix/fs.c
        ${UV_SRC_DIR}/unix/getaddrinfo.c
        ${UV_SRC_DIR}/unix/getnameinfo.c
        ${UV_SRC_DIR}/unix/loop-watcher.c
        ${UV_SRC_DIR}/unix/loop.c
        ${UV_SRC_DIR}/unix/pipe.c
        ${UV_SRC_DIR}/unix/poll.c
        ${UV_SRC_DIR}/unix/process.c
        ${UV_SRC_DIR}/unix/signal.c
        ${UV_SRC_DIR}/unix/stream.c
        ${UV_SRC_DIR}/unix/tcp.c
        ${UV_SRC_DIR}/unix/thread.c
        ${UV_SRC_DIR}/unix/timer.c
        ${UV_SRC_DIR}/unix/tty.c
        ${UV_SRC_DIR}/unix/udp.c
        ${UV_SRC_DIR}/unix/proctitle.c)
    if(${CMAKE_SYSTEM_NAME} STREQUAL "Linux")
        set(UV_COMPILE_DEFS ${UV_COMPILE_DEFS} _GNU_SOURCE)
        set(UV_SOURCES ${UV_SOURCES}
            ${UV_SRC_DIR}/unix/linux-core.c
            ${UV_SRC_DIR}/unix/linux-inotify.c
            ${UV_SRC_DIR}/unix/linux-syscalls.c
            ${UV_SRC_DIR}/unix/procfs-exepath.c
            ${UV_SRC_DIR}/unix/proctitle.c
            ${UV_SRC_DIR}/unix/sysinfo-loadavg.c
            ${UV_SRC_DIR}/unix/sysinfo-memory.c)
    elseif(${CMAKE_SYSTEM_NAME} STREQUAL "Darwin")
        set(UV_COMPILE_DEFS ${UV_COMPILE_DEFS} _DARWIN_USE_64_BIT_INODE=1 _DARWIN_UNLIMITED_SELECT=1)
        set(UV_SOURCES ${UV_SOURCES}
            ${UV_SRC_DIR}/unix/darwin.c
            ${UV_SRC_DIR}/unix/darwin-proctitle.c
            ${UV_SRC_DIR}/unix/fsevents.c
            ${UV_SRC_DIR}/unix/kqueue.c
            ${UV_SRC_DIR}/unix/proctitle.c)
    endif()
endif()

add_library(uvinternal STATIC ${UV_SOURCES})
target_compile_definitions(uvinternal PRIVATE ${UV_COMPILE_DEFS})
target_include_directories(uvinternal PRIVATE ${UV_INCLUDE_DIRS})
target_compile_options(uvinternal PRIVATE ${UV_FLAGS})
target_link_libraries(uvinternal PRIVATE ${UV_LDFLAGS})
