#pragma once

namespace beam { namespace io {

struct Config {
    int handle_pool_size=256;
    int tcp_listen_backlog=32;
    int stream_read_buffer_size=1024*256;
    int stream_write_buffer_alarm=100000000;

    // ~etc
};

}} //namespaces
