cmake_minimum_required(VERSION 3.11)
project(qmllang3)

if(MSVC)
	set (CMAKE_PREFIX_PATH $ENV{QT5_ROOT_DIR})
endif()

find_package(Qt5 COMPONENTS Qml Quick QuickControls2 Widgets REQUIRED LinguistTools)
set(CMAKE_AUTOMOC ON)

qt5_add_resources(QT_RESOURCES qml.qrc)
qt5_add_resources(QT_RESOURCES translations.qrc)

set(FILES_TO_TRANSLATE
    "${CMAKE_CURRENT_SOURCE_DIR}/main.qml"
)

set(TS_FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/QmlLanguage_ru.ts"
    "${CMAKE_CURRENT_SOURCE_DIR}/QmlLanguage_by.ts"
	"${CMAKE_CURRENT_SOURCE_DIR}/QmlLanguage_de.ts"
)    

qt5_create_translation(QM_FILES ${FILES_TO_TRANSLATE} ${TS_FILES})

if(MSVC)
	set(SYSTEM_TYPE WIN32)
elseif(APPLE)
	set(SYSTEM_TYPE MACOSX_BUNDLE)
endif()

set(UI_SRC
	main.cpp
	qmltranslator.cpp
)

set(UI_HEADERS
    qmltranslator.h	
)

add_executable(${PROJECT_NAME} ${SYSTEM_TYPE} ${UI_SRC} ${QT_RESOURCES})

target_link_libraries(${PROJECT_NAME} Qt5::Widgets Qt5::Qml Qt5::Quick Qt5::QuickControls2)

qt5_create_translation(QM_FILES ${FILES_TO_TRANSLATE} ${TS_FILES})
