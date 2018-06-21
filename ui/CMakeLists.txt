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

qt5_add_resources(QT_RESOURCES view/qml.qrc)

set(UI_SRC
	ui.cpp
	viewmodel/main.h
	viewmodel/main.cpp
	viewmodel/dashboard.h
	viewmodel/dashboard.cpp
	viewmodel/wallet.h
	viewmodel/wallet.cpp
	viewmodel/notifications.h
	viewmodel/notifications.cpp
	viewmodel/help.h
	viewmodel/help.cpp
	viewmodel/settings.h
	viewmodel/settings.cpp
)

add_executable(beam-ui ${SYSTEM_TYPE} ${UI_SRC} ${QT_RESOURCES})

target_link_libraries(beam-ui Qt5::Qml Qt5::Quick)
