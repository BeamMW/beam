set (CMAKE_PREFIX_PATH $ENV{QT5_ROOT_DIR})

find_package(Qt5 COMPONENTS Qml Quick Svg REQUIRED)

set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_AUTOMOC ON)

if(WIN32)
	if(CMAKE_BUILD_TYPE STREQUAL "Release")
		set(SYSTEM_TYPE WIN32)
	endif()
elseif(APPLE)
	set(SYSTEM_TYPE MACOSX_BUNDLE)
endif()

find_package(Qt5QuickCompiler)
qtquick_compiler_add_resources(QT_RESOURCES view/qml.qrc)

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

if(WIN32)
	add_definitions(-DWIN32_LEAN_AND_MEAN)
endif()

add_executable(beam-ui ${SYSTEM_TYPE} ${UI_SRC} ${QT_RESOURCES})

add_dependencies(beam-ui wallet)

target_link_libraries(beam-ui wallet Qt5::Qml Qt5::Quick Qt5::Svg)

if(BEAM_USE_STATIC)

	if(WIN32)

		foreach(lib lib/clip2tri lib/Qt5Concurrent lib/Qt5Help lib/Qt5PrintSupport lib/Qt5RemoteObjects lib/Qt5TextToSpeech 
			lib/Qt5XmlPatterns lib/clipper lib/Qt5Core lib/Qt5Location lib/Qt5Purchasing lib/Qt5Script lib/Qt5ThemeSupport lib/qtfreetype 
			lib/poly2tri lib/Qt5DBus lib/Qt5Multimedia lib/Qt5Qml lib/Qt5ScriptTools lib/Qt5UiTools lib/qtharfbuzz lib/Qt5AccessibilitySupport 
			lib/Qt5DeviceDiscoverySupport lib/Qt5MultimediaWidgets lib/Qt5QmlDebug lib/Qt5Scxml lib/Qt5WebChannel lib/qtlibpng lib/Qt5AxBase 
			lib/Qt5EdidSupport lib/Qt5Network lib/Qt5QmlDevTools lib/Qt5Sensors lib/Qt5WebSockets lib/qtmain lib/Qt5AxContainer 
			lib/Qt5EventDispatcherSupport lib/Qt5NetworkAuth lib/Qt5Quick lib/Qt5SerialBus lib/Qt5WebView lib/qtopenwnn lib/Qt5AxServer 
			lib/Qt5FbSupport lib/Qt5Nfc lib/Qt5QuickControls2 lib/Qt5SerialPort lib/Qt5Widgets lib/qtpcre2 lib/Qt5Bluetooth 
			lib/Qt5FontDatabaseSupport lib/Qt5PacketProtocol lib/Qt5QuickTemplates2 lib/Qt5Sql lib/Qt5WindowsUIAutomationSupport 
			lib/qtpinyin lib/Qt5Bootstrap lib/Qt5Gamepad lib/Qt5Positioning lib/Qt5QuickTest lib/Qt5Svg lib/Qt5WinExtras lib/qttcime 
			lib/Qt5Charts lib/Qt5Gui lib/Qt5PositioningQuick lib/Qt5QuickWidgets lib/Qt5Test lib/Qt5Xml qml/QtQuick.2/qtquick2plugin 
			qml/QtQuick/Window.2/windowplugin qml/QtQuick/Controls/qtquickcontrolsplugin qml/QtGraphicalEffects/qtgraphicaleffectsplugin 
			qml/QtGraphicalEffects/private/qtgraphicaleffectsprivate plugins/platforms/qwindows plugins/imageformats/qsvg)

			target_link_libraries(beam-ui ${CMAKE_PREFIX_PATH}/${lib}.lib)
		endforeach(flag_var)

		target_link_libraries(beam-ui Netapi32.lib version.lib winmm.lib Imm32.lib Dwmapi.lib)
		
	endif()


	if(APPLE)

		foreach(lib lib/libQt53DAnimation lib/libQt5Concurrent lib/libQt5MultimediaWidgets lib/libQt5QuickParticles lib/libQt5WebChannel 
			lib/libQt53DCore lib/libQt5Core lib/libQt5Network lib/libQt5QuickTemplates2 lib/libQt5WebSockets lib/libQt53DExtras lib/libQt5DBus 
			lib/libQt5NetworkAuth lib/libQt5QuickTest lib/libQt5WebView lib/libQt53DInput lib/libQt5DataVisualization lib/libQt5Nfc 
			lib/libQt5QuickWidgets lib/libQt5Widgets lib/libQt53DLogic lib/libQt5DeviceDiscoverySupport lib/libQt5OpenGL lib/libQt5RemoteObjects 
			lib/libQt5Xml lib/libQt53DQuick lib/libQt5EdidSupport lib/libQt5OpenGLExtensions lib/libQt5Script lib/libQt5XmlPatterns 
			lib/libQt53DQuickAnimation lib/libQt5EventDispatcherSupport lib/libQt5PacketProtocol lib/libQt5ScriptTools lib/libclip2tri 
			lib/libQt53DQuickExtras lib/libQt5FbSupport lib/libQt5PlatformCompositorSupport lib/libQt5Scxml lib/libclipper lib/libQt53DQuickInput 
			lib/libQt5FontDatabaseSupport lib/libQt5Positioning lib/libQt5Sensors lib/libpoly2tri lib/libQt53DQuickRender lib/libQt5Gamepad 
			lib/libQt5PositioningQuick lib/libQt5SerialBus lib/libqmapboxgl lib/libQt53DQuickScene2D lib/libQt5GraphicsSupport 
			lib/libQt5PrintSupport lib/libQt5SerialPort lib/libqtfreetype lib/libQt53DRender lib/libQt5Gui lib/libQt5Purchasing lib/libQt5Sql 
			lib/libqtharfbuzz lib/libQt5AccessibilitySupport lib/libQt5Help lib/libQt5Qml lib/libQt5Svg lib/libqtlibpng lib/libQt5Bluetooth 
			lib/libQt5Location lib/libQt5QmlDebug lib/libQt5Test lib/libqtopenwnn lib/libQt5Bootstrap lib/libQt5MacExtras lib/libQt5QmlDevTools 
			lib/libQt5TextToSpeech lib/libqtpcre2 lib/libQt5Charts lib/libQt5Multimedia lib/libQt5Quick lib/libQt5ThemeSupport lib/libqtpinyin 
			lib/libQt5ClipboardSupport lib/libQt5MultimediaQuick lib/libQt5QuickControls2 lib/libQt5UiTools lib/libqttcime 
			qml/QtQuick.2/libqtquick2plugin qml/QtQuick/Window.2/libwindowplugin qml/QtQuick/Controls/libqtquickcontrolsplugin 
			qml/QtGraphicalEffects/libqtgraphicaleffectsplugin qml/QtGraphicalEffects/private/libqtgraphicaleffectsprivate 
			plugins/platforms/libqcocoa plugins/platforms/libqminimal plugins/imageformats/libqsvg)

			target_link_libraries(beam-ui ${CMAKE_PREFIX_PATH}/${lib}.a)
		endforeach(flag_var)

		target_link_libraries(beam-ui
			"-framework CoreFoundation"
			"-framework Foundation"
			"-framework DiskArbitration"
			"-framework ApplicationServices"
			"-framework CFNetwork"
			"-framework CoreGraphics"
			"-framework CoreText"
			"-framework CoreServices"
			"-framework AppKit"
			"-framework OpenGL"
			"-framework AGL"
			"-framework Carbon"
			"-framework Cocoa"
			"-framework IOKit"
			"-framework QuartzCore"
			"-framework Security"
			"-framework SystemConfiguration"
			cups
		)
	endif()

endif()
