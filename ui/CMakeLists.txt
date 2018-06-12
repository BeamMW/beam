if(MSVC)
	set (CMAKE_PREFIX_PATH $ENV{QT5_ROOT_DIR})
endif()

find_package(Qt5 COMPONENTS Qml Quick REQUIRED)

if(MSVC)
	set(SYSTEM_TYPE WIN32)
elseif(APPLE)
	set(SYSTEM_TYPE MACOSX_BUNDLE)
endif()

set(UI_SRC
	ui.cpp
)

add_executable(beam-ui ${SYSTEM_TYPE} ${UI_SRC})

target_link_libraries(beam-ui Qt5::Qml Qt5::Quick)
