cmake_minimum_required(VERSION 3.13)

add_test_snippet(websocket_test websocket Boost::date_time Boost::coroutine utility)

if(MSVC)
    # to avoid 4702 warning here, until another workaround will be found
    # const_buffer(const mutable_buffer& b) BOOST_ASIO_NOEXCEPT
    #    : data_(b.data()),
    target_compile_options(websocket_test PRIVATE "/wd4702")
    target_compile_options(websocket_test PRIVATE "/bigobj")
endif()