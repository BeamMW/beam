function(add_snippet EXE_NAME)
    add_executable(${EXE_NAME} ${EXE_NAME}.cpp)
    add_dependencies(${EXE_NAME} io)
    target_link_libraries(${EXE_NAME} io)
endfunction()

add_snippet(reactor_test)
add_snippet(asyncevent_test)
add_snippet(tcpserver_test)

# ~ etc
