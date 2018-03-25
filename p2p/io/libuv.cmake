set(UV_SRC_DIR libuv/src)
set(UV_INCLUDE_DIR libuv/include)

set(UV_SOURCES
    ${UV_SRC_DIR}/fs-poll.c
    ${UV_SRC_DIR}/inet.c
    ${UV_SRC_DIR}/threadpool.c
    ${UV_SRC_DIR}/uv-common.c
    ${UV_SRC_DIR}/version.c
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
    ${UV_SRC_DIR}/unix/linux-core.c
    ${UV_SRC_DIR}/unix/linux-inotify.c
    ${UV_SRC_DIR}/unix/linux-syscalls.c
    ${UV_SRC_DIR}/unix/proctitle.c)

set(UV_FLAGS -Wall -Wextra -Wno-unused-parameter -pedantic -march=native)

if(CMAKE_BUILD_TYPE MATCHES "Debug")
    list(APPEND UV_FLAGS -O0 -ggdb3)
else()
    list(APPEND UV_FLAGS -O3)
endif()

add_library(uv STATIC ${UV_SOURCES})
target_compile_definitions(uv PRIVATE _GNU_SOURCE _LARGEFILE_SOURCE _FILE_OFFSET_BITS=64)
target_compile_options(uv PRIVATE ${UV_FLAGS})
target_include_directories(uv PRIVATE ${UV_INCLUDE_DIR} ${UV_SRC_DIR} ${UV_SRC_DIR}/unix)
target_link_libraries(uv PRIVATE dl)
