if(MSVC)
	set (CMAKE_PREFIX_PATH $ENV{QT5_ROOT_DIR})
endif()

find_package(Qt5QuickCompiler)
find_package(Qt5 COMPONENTS Qml Quick Widgets REQUIRED)

set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_AUTOMOC ON)

if(MSVC)
	if(CMAKE_BUILD_TYPE STREQUAL "Release")
		set(SYSTEM_TYPE WIN32)
	endif()	
elseif(APPLE)
# TODO: uncomment this later
#	set(SYSTEM_TYPE MACOSX_BUNDLE)
endif()

qtquick_compiler_add_resources(QT_RESOURCES view/qml.qrc)

if(!MSVC)
	find_package(Qt5 COMPONENTS LinguistTools REQUIRED)

	qt5_add_resources(QT_RESOURCES lang/translations.qrc)

	file (GLOB_RECURSE FILES_TO_TRANSLATE "${CMAKE_CURRENT_SOURCE_DIR}/view/*.qml")

	file (GLOB_RECURSE TRANSLATION_FILES "${CMAKE_CURRENT_SOURCE_DIR}/lang/*.ts")

	qt5_create_translation(QM_FILES ${FILES_TO_TRANSLATE} ${TRANSLATION_FILES})
endif()

set(UI_SRC
	ui.cpp
	translator.h
	translator.cpp
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

	model/wallet.h
	model/wallet.cpp
)

if(MSVC)
	add_definitions(-DWIN32_LEAN_AND_MEAN)
endif()

add_executable(beam-ui ${SYSTEM_TYPE} ${UI_SRC} ${QT_RESOURCES})

if (Boost_FOUND)
	find_package(Boost COMPONENTS program_options)
    target_link_libraries(beam-ui Boost::program_options)
else()
    target_link_libraries(beam-ui boost_program_options)
endif()

add_dependencies(beam-ui wallet)

target_link_libraries(beam-ui Qt5::Qml Qt5::Quick Qt5::Widgets wallet)
