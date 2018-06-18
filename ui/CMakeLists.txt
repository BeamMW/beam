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

set(UI_SRC
	ui.cpp
	test_model.h
	test_model.cpp
)

set( QML_FILES
   hw.qml
)

foreach( file_i ${QML_FILES})
    configure_file(${CMAKE_CURRENT_SOURCE_DIR}/qml/${file_i} ${CMAKE_CURRENT_BINARY_DIR}/qml/${file_i} COPYONLY)
endforeach( file_i )

add_executable(beam-ui ${SYSTEM_TYPE} ${UI_SRC})

target_link_libraries(beam-ui Qt5::Qml Qt5::Quick)
