cmake_minimum_required(VERSION 3.13)

set(WEBSOCKET_TEST_LIBS websocket Boost::date_time Boost::context Boost::thread utility)
# boost::asio::spawn used Boost.Coroutine until Boost 1.86;
# since Boost 1.87 it works only with Boost.Context fibers.
if (Boost_VERSION VERSION_LESS 108700)
    list(APPEND WEBSOCKET_TEST_LIBS Boost::coroutine)
endif()
add_test_snippet(websocket_test ${WEBSOCKET_TEST_LIBS})

if(MSVC)
    # to avoid 4702 warning here, until another workaround will be found
    # const_buffer(const mutable_buffer& b) BOOST_ASIO_NOEXCEPT
    #    : data_(b.data()),
    target_compile_options(websocket_test PRIVATE "/wd4702")
    target_compile_options(websocket_test PRIVATE "/bigobj")
endif()