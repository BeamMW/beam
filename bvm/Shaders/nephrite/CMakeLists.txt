set(SRC
	app.cpp
	app-admin.cpp
	contract.cpp
)

add_library(lib-nephrite STATIC ${SRC})
target_link_libraries(lib-nephrite PUBLIC Boost::boost)

