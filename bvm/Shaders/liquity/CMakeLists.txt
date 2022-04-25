set(SRC
	app.cpp
	app-admin.cpp
	contract.cpp
)

add_library(lib-liquity STATIC ${SRC})
target_link_libraries(lib-liquity PUBLIC Boost::boost)

