if(MSVC)
	set (CMAKE_PREFIX_PATH $ENV{QT5_ROOT_DIR})
endif()

find_package(Qt5 COMPONENTS Qml Quick Svg REQUIRED)

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

qt5_add_resources(QT_RESOURCES view/qml.qrc)

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

add_dependencies(beam-ui wallet)

target_link_libraries(beam-ui Qt5::Qml Qt5::Quick Qt5::Svg 
	${CMAKE_PREFIX_PATH}/lib/clip2tri.lib
	${CMAKE_PREFIX_PATH}/lib/Qt5Concurrent.lib
	${CMAKE_PREFIX_PATH}/lib/Qt5Help.lib
	${CMAKE_PREFIX_PATH}/lib/Qt5PrintSupport.lib
	${CMAKE_PREFIX_PATH}/lib/Qt5RemoteObjects.lib
	${CMAKE_PREFIX_PATH}/lib/Qt5TextToSpeech.lib
	${CMAKE_PREFIX_PATH}/lib/Qt5XmlPatterns.lib
	${CMAKE_PREFIX_PATH}/lib/clipper.lib
	${CMAKE_PREFIX_PATH}/lib/Qt5Core.lib
	${CMAKE_PREFIX_PATH}/lib/Qt5Location.lib
	${CMAKE_PREFIX_PATH}/lib/Qt5Purchasing.lib
	${CMAKE_PREFIX_PATH}/lib/Qt5Script.lib
	${CMAKE_PREFIX_PATH}/lib/Qt5ThemeSupport.lib
	${CMAKE_PREFIX_PATH}/lib/qtfreetype.lib
	${CMAKE_PREFIX_PATH}/lib/poly2tri.lib
	${CMAKE_PREFIX_PATH}/lib/Qt5DBus.lib
	${CMAKE_PREFIX_PATH}/lib/Qt5Multimedia.lib
	${CMAKE_PREFIX_PATH}/lib/Qt5Qml.lib
	${CMAKE_PREFIX_PATH}/lib/Qt5ScriptTools.lib
	${CMAKE_PREFIX_PATH}/lib/Qt5UiTools.lib
	${CMAKE_PREFIX_PATH}/lib/qtharfbuzz.lib
	${CMAKE_PREFIX_PATH}/lib/Qt5AccessibilitySupport.lib
	${CMAKE_PREFIX_PATH}/lib/Qt5DeviceDiscoverySupport.lib
	${CMAKE_PREFIX_PATH}/lib/Qt5MultimediaWidgets.lib
	${CMAKE_PREFIX_PATH}/lib/Qt5QmlDebug.lib
	${CMAKE_PREFIX_PATH}/lib/Qt5Scxml.lib
	${CMAKE_PREFIX_PATH}/lib/Qt5WebChannel.lib
	${CMAKE_PREFIX_PATH}/lib/qtlibpng.lib
	${CMAKE_PREFIX_PATH}/lib/Qt5AxBase.lib
	${CMAKE_PREFIX_PATH}/lib/Qt5EdidSupport.lib
	${CMAKE_PREFIX_PATH}/lib/Qt5Network.lib
	${CMAKE_PREFIX_PATH}/lib/Qt5QmlDevTools.lib
	${CMAKE_PREFIX_PATH}/lib/Qt5Sensors.lib
	${CMAKE_PREFIX_PATH}/lib/Qt5WebSockets.lib
	${CMAKE_PREFIX_PATH}/lib/qtmain.lib
	${CMAKE_PREFIX_PATH}/lib/Qt5AxContainer.lib
	${CMAKE_PREFIX_PATH}/lib/Qt5EventDispatcherSupport.lib
	${CMAKE_PREFIX_PATH}/lib/Qt5NetworkAuth.lib
	${CMAKE_PREFIX_PATH}/lib/Qt5Quick.lib
	${CMAKE_PREFIX_PATH}/lib/Qt5SerialBus.lib
	${CMAKE_PREFIX_PATH}/lib/Qt5WebView.lib
	${CMAKE_PREFIX_PATH}/lib/qtopenwnn.lib
	${CMAKE_PREFIX_PATH}/lib/Qt5AxServer.lib
	${CMAKE_PREFIX_PATH}/lib/Qt5FbSupport.lib
	${CMAKE_PREFIX_PATH}/lib/Qt5Nfc.lib
	${CMAKE_PREFIX_PATH}/lib/Qt5QuickControls2.lib
	${CMAKE_PREFIX_PATH}/lib/Qt5SerialPort.lib
	${CMAKE_PREFIX_PATH}/lib/Qt5Widgets.lib
	${CMAKE_PREFIX_PATH}/lib/qtpcre2.lib
	${CMAKE_PREFIX_PATH}/lib/Qt5Bluetooth.lib
	${CMAKE_PREFIX_PATH}/lib/Qt5FontDatabaseSupport.lib
	${CMAKE_PREFIX_PATH}/lib/Qt5PacketProtocol.lib
	${CMAKE_PREFIX_PATH}/lib/Qt5QuickTemplates2.lib
	${CMAKE_PREFIX_PATH}/lib/Qt5Sql.lib
	${CMAKE_PREFIX_PATH}/lib/Qt5WindowsUIAutomationSupport.lib
	${CMAKE_PREFIX_PATH}/lib/qtpinyin.lib
	${CMAKE_PREFIX_PATH}/lib/Qt5Bootstrap.lib
	${CMAKE_PREFIX_PATH}/lib/Qt5Gamepad.lib
	${CMAKE_PREFIX_PATH}/lib/Qt5Positioning.lib
	${CMAKE_PREFIX_PATH}/lib/Qt5QuickTest.lib
	${CMAKE_PREFIX_PATH}/lib/Qt5Svg.lib
	${CMAKE_PREFIX_PATH}/lib/Qt5WinExtras.lib
	${CMAKE_PREFIX_PATH}/lib/qttcime.lib
	${CMAKE_PREFIX_PATH}/lib/Qt5Charts.lib
	${CMAKE_PREFIX_PATH}/lib/Qt5Gui.lib
	${CMAKE_PREFIX_PATH}/lib/Qt5PositioningQuick.lib
	${CMAKE_PREFIX_PATH}/lib/Qt5QuickWidgets.lib
	${CMAKE_PREFIX_PATH}/lib/Qt5Test.lib
	${CMAKE_PREFIX_PATH}/lib/Qt5Xml.lib

	${CMAKE_PREFIX_PATH}/qml/QtQuick.2/qtquick2plugin.lib
	${CMAKE_PREFIX_PATH}/qml/QtQuick/Window.2/windowplugin.lib
	${CMAKE_PREFIX_PATH}/qml/QtQuick/Controls/qtquickcontrolsplugin.lib
	${CMAKE_PREFIX_PATH}/qml/QtGraphicalEffects/qtgraphicaleffectsplugin.lib
	${CMAKE_PREFIX_PATH}/qml/QtGraphicalEffects/private/qtgraphicaleffectsprivate.lib

	${CMAKE_PREFIX_PATH}/plugins/platforms/qwindows.lib
	${CMAKE_PREFIX_PATH}/plugins/imageformats/qsvg.lib

	Netapi32.lib 
	version.lib 
	winmm.lib 
	Imm32.lib
	Dwmapi.lib
	wallet
)
