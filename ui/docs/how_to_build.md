# How to build on Windows

download and install QT from here https://download.qt.io/official_releases/qt/5.11/5.11.0/qt-opensource-windows-x86-5.11.0.exe.mirrorlist
add QT5_ROOT_DIR system var

# How to build with static QT on Windows

some info here http://amin-ahmadi.com/2016/09/22/how-to-build-qt-5-7-statically-using-msvc14-microsoft-visual-studio-2015/

change `msvc-desktop.conf`: `-MD` to `-MT`

then call `configure -release -prefix "path/to/install/dir/qt-static" -opensource -confirm-license -static -platform win32-msvc2017 -ltcg -mp -no-opengl -no-openssl -make libs -nomake tools -nomake examples -nomake tests`

add `Q_IMPORT_PLUGIN(QWindowsIntegrationPlugin)` to the `main.cpp`

should look like this
```
#include <QGuiApplication>
#include <QtCore/QtPlugin>
#include <QQuickView>
#include <QtQml>

Q_IMPORT_PLUGIN(QWindowsIntegrationPlugin)

int main (int argc, char* argv[])
{
    QGuiApplication q_app (argc, argv);
    
    QQuickView view;
    view.setSource(QUrl::fromLocalFile("hw.qml"));
    view.show();
    
    return q_app.exec ();
}
```

change CMake to
`find_package(Qt5 COMPONENTS Widgets Gui Core Qml Quick REQUIRED)`
and
`target_link_libraries(beam-ui Qt5::Qml Qt5::Quick Qt5::Widgets Qt5::Gui Qt5::Core Qt5FontDatabaseSupport Qt5WindowsUIAutomationSupport Qt5EventDispatcherSupport Qt5ThemeSupport qtmain qwindows qtfreetype qtpcre2 qtlibpng qtharfbuzz wsock32 ws2_32 iphlpapi winmm userenv version netapi32 opengl32 imm32 dwmapi)`
