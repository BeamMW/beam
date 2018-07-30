set (CMAKE_PREFIX_PATH $ENV{QT5_ROOT_DIR})

find_package(Qt5 COMPONENTS Qml Quick Svg REQUIRED)

set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_AUTOMOC ON)

foreach(plugin ${Qt5Quick_PLUGINS})
  get_target_property(_loc ${plugin} LOCATION)
  message("Plugin ${plugin} is at location ${_loc}")
endforeach()

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

		target_link_libraries(beam-ui
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
		)

		target_link_libraries(beam-ui
			Netapi32.lib 
			version.lib 
			winmm.lib 
			Imm32.lib
			Dwmapi.lib
		)
		
	endif()


	if(APPLE)
		target_link_libraries(beam-ui
			${CMAKE_PREFIX_PATH}/lib/libQt53DAnimation.a
			${CMAKE_PREFIX_PATH}/lib/libQt5Concurrent.a
			${CMAKE_PREFIX_PATH}/lib/libQt5MultimediaWidgets.a
			${CMAKE_PREFIX_PATH}/lib/libQt5QuickParticles.a
			${CMAKE_PREFIX_PATH}/lib/libQt5WebChannel.a
			${CMAKE_PREFIX_PATH}/lib/libQt53DCore.a
			${CMAKE_PREFIX_PATH}/lib/libQt5Core.a
			${CMAKE_PREFIX_PATH}/lib/libQt5Network.a
			${CMAKE_PREFIX_PATH}/lib/libQt5QuickTemplates2.a
			${CMAKE_PREFIX_PATH}/lib/libQt5WebSockets.a
			${CMAKE_PREFIX_PATH}/lib/libQt53DExtras.a
			${CMAKE_PREFIX_PATH}/lib/libQt5DBus.a
			${CMAKE_PREFIX_PATH}/lib/libQt5NetworkAuth.a
			${CMAKE_PREFIX_PATH}/lib/libQt5QuickTest.a
			${CMAKE_PREFIX_PATH}/lib/libQt5WebView.a
			${CMAKE_PREFIX_PATH}/lib/libQt53DInput.a
			${CMAKE_PREFIX_PATH}/lib/libQt5DataVisualization.a
			${CMAKE_PREFIX_PATH}/lib/libQt5Nfc.a
			${CMAKE_PREFIX_PATH}/lib/libQt5QuickWidgets.a
			${CMAKE_PREFIX_PATH}/lib/libQt5Widgets.a
			${CMAKE_PREFIX_PATH}/lib/libQt53DLogic.a
			${CMAKE_PREFIX_PATH}/lib/libQt5DeviceDiscoverySupport.a
			${CMAKE_PREFIX_PATH}/lib/libQt5OpenGL.a
			${CMAKE_PREFIX_PATH}/lib/libQt5RemoteObjects.a
			${CMAKE_PREFIX_PATH}/lib/libQt5Xml.a
			${CMAKE_PREFIX_PATH}/lib/libQt53DQuick.a
			${CMAKE_PREFIX_PATH}/lib/libQt5EdidSupport.a
			${CMAKE_PREFIX_PATH}/lib/libQt5OpenGLExtensions.a
			${CMAKE_PREFIX_PATH}/lib/libQt5Script.a
			${CMAKE_PREFIX_PATH}/lib/libQt5XmlPatterns.a
			${CMAKE_PREFIX_PATH}/lib/libQt53DQuickAnimation.a
			${CMAKE_PREFIX_PATH}/lib/libQt5EventDispatcherSupport.a
			${CMAKE_PREFIX_PATH}/lib/libQt5PacketProtocol.a
			${CMAKE_PREFIX_PATH}/lib/libQt5ScriptTools.a
			${CMAKE_PREFIX_PATH}/lib/libclip2tri.a
			${CMAKE_PREFIX_PATH}/lib/libQt53DQuickExtras.a
			${CMAKE_PREFIX_PATH}/lib/libQt5FbSupport.a
			${CMAKE_PREFIX_PATH}/lib/libQt5PlatformCompositorSupport.a
			${CMAKE_PREFIX_PATH}/lib/libQt5Scxml.a
			${CMAKE_PREFIX_PATH}/lib/libclipper.a
			${CMAKE_PREFIX_PATH}/lib/libQt53DQuickInput.a
			${CMAKE_PREFIX_PATH}/lib/libQt5FontDatabaseSupport.a
			${CMAKE_PREFIX_PATH}/lib/libQt5Positioning.a
			${CMAKE_PREFIX_PATH}/lib/libQt5Sensors.a
			${CMAKE_PREFIX_PATH}/lib/libpoly2tri.a
			${CMAKE_PREFIX_PATH}/lib/libQt53DQuickRender.a
			${CMAKE_PREFIX_PATH}/lib/libQt5Gamepad.a
			${CMAKE_PREFIX_PATH}/lib/libQt5PositioningQuick.a
			${CMAKE_PREFIX_PATH}/lib/libQt5SerialBus.a
			${CMAKE_PREFIX_PATH}/lib/libqmapboxgl.a
			${CMAKE_PREFIX_PATH}/lib/libQt53DQuickScene2D.a
			${CMAKE_PREFIX_PATH}/lib/libQt5GraphicsSupport.a
			${CMAKE_PREFIX_PATH}/lib/libQt5PrintSupport.a
			${CMAKE_PREFIX_PATH}/lib/libQt5SerialPort.a
			${CMAKE_PREFIX_PATH}/lib/libqtfreetype.a
			${CMAKE_PREFIX_PATH}/lib/libQt53DRender.a
			${CMAKE_PREFIX_PATH}/lib/libQt5Gui.a
			${CMAKE_PREFIX_PATH}/lib/libQt5Purchasing.a
			${CMAKE_PREFIX_PATH}/lib/libQt5Sql.a
			${CMAKE_PREFIX_PATH}/lib/libqtharfbuzz.a
			${CMAKE_PREFIX_PATH}/lib/libQt5AccessibilitySupport.a
			${CMAKE_PREFIX_PATH}/lib/libQt5Help.a
			${CMAKE_PREFIX_PATH}/lib/libQt5Qml.a
			${CMAKE_PREFIX_PATH}/lib/libQt5Svg.a
			${CMAKE_PREFIX_PATH}/lib/libqtlibpng.a
			${CMAKE_PREFIX_PATH}/lib/libQt5Bluetooth.a
			${CMAKE_PREFIX_PATH}/lib/libQt5Location.a
			${CMAKE_PREFIX_PATH}/lib/libQt5QmlDebug.a
			${CMAKE_PREFIX_PATH}/lib/libQt5Test.a
			${CMAKE_PREFIX_PATH}/lib/libqtopenwnn.a
			${CMAKE_PREFIX_PATH}/lib/libQt5Bootstrap.a
			${CMAKE_PREFIX_PATH}/lib/libQt5MacExtras.a
			${CMAKE_PREFIX_PATH}/lib/libQt5QmlDevTools.a
			${CMAKE_PREFIX_PATH}/lib/libQt5TextToSpeech.a
			${CMAKE_PREFIX_PATH}/lib/libqtpcre2.a
			${CMAKE_PREFIX_PATH}/lib/libQt5Charts.a
			${CMAKE_PREFIX_PATH}/lib/libQt5Multimedia.a
			${CMAKE_PREFIX_PATH}/lib/libQt5Quick.a
			${CMAKE_PREFIX_PATH}/lib/libQt5ThemeSupport.a
			${CMAKE_PREFIX_PATH}/lib/libqtpinyin.a
			${CMAKE_PREFIX_PATH}/lib/libQt5ClipboardSupport.a
			${CMAKE_PREFIX_PATH}/lib/libQt5MultimediaQuick.a
			${CMAKE_PREFIX_PATH}/lib/libQt5QuickControls2.a
			${CMAKE_PREFIX_PATH}/lib/libQt5UiTools.a
			${CMAKE_PREFIX_PATH}/lib/libqttcime.a

			${CMAKE_PREFIX_PATH}/qml/QtQuick.2/libqtquick2plugin.a
			${CMAKE_PREFIX_PATH}/qml/QtQuick/Window.2/libwindowplugin.a
			${CMAKE_PREFIX_PATH}/qml/QtQuick/Controls/libqtquickcontrolsplugin.a
			${CMAKE_PREFIX_PATH}/qml/QtGraphicalEffects/libqtgraphicaleffectsplugin.a
			${CMAKE_PREFIX_PATH}/qml/QtGraphicalEffects/private/libqtgraphicaleffectsprivate.a

			${CMAKE_PREFIX_PATH}/plugins/platforms/libqcocoa.a
			${CMAKE_PREFIX_PATH}/plugins/platforms/libqminimal.a
			${CMAKE_PREFIX_PATH}/plugins/imageformats/libqsvg.a
		)

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
