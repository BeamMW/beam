if(MSVC)
	set (CMAKE_PREFIX_PATH $ENV{QT5_ROOT_DIR})
endif()

find_package(Qt5 COMPONENTS Qml Quick REQUIRED)

set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_AUTOMOC ON)

if(MSVC)
	set(SYSTEM_TYPE WIN32)
elseif(APPLE)
	set(SYSTEM_TYPE MACOSX_BUNDLE)
endif()

qt5_add_resources(QT_RESOURCES qml.qrc)

set(UI_SRC
	ui.cpp
	test_model.h
	test_model.cpp
)

add_executable(beam-ui ${SYSTEM_TYPE} ${UI_SRC} ${QT_RESOURCES})

target_link_libraries(beam-ui Qt5::Qml Qt5::Quick)
