[![Build Status](https://travis-ci.com/beam-mw/beam.svg?token=9GsAxqTGpjsBMMHVvgzz&branch=master)](https://travis-ci.com/beam-mw/beam)
[![Build Status](https://ci.appveyor.com/api/projects/status/voh9d2q7i5oj20rr/branch/master?svg=true)](https://ci.appveyor.com/project/beam-mw/beam/branch/master)

# Beam

# How to build
## Windows
1. Install Visual Studio >= 2017
1. Download and install Boost prebuilt binaries https://sourceforge.net/projects/boost/files/boost-binaries/, also add `BOOST_ROOT` to the _Environment Variables_
1. Download and install OpenSSL prebuilt binaries (`Win64 OpenSSL v1.1.0h` for example) and add `OPENSSL_ROOT_DIR` to the _Environment Variables_
1. Download and install QT 5.11 https://download.qt.io/official_releases/qt/5.11/5.11.0/qt-opensource-windows-x86-5.11.0.exe.mirrorlist and add `QT5_ROOT_DIR` to the _Environment Variables_ (usually it looks like `.../5.11.0/msvc2017_64`), also add `QML_IMPORT_PATH` (it should look like `%QT5_ROOT_DIR%\qml`). BTW disabling system antivirus on Windows makes QT installing process much faster.
1. Open project folder in Visual Studio, select your target (`Release-x64` for example, if you downloaded 64bit Boost and OpenSSL) and select `CMake -> Build All`
1. Go to `CMake -> Cache -> Open Cache Folder -> beam` (you'll find `beam.exe` in the `beam` subfolder, `beam-ui.exe` in `ui` subfolder)

## Linux
1. Make sure you have installed `g++-7 cmake libboost-all-dev libssl-dev qtdeclarative5-dev qtdeclarative5-qtquick2-plugin` packages
1. Go to Beam project folder and call `cmake -DCMAKE_BUILD_TYPE=Release . && make -j 4`
1. You'll find _Beam_ binary in `bin` folder, `beam-ui.exe` in `ui` subfolder

## Mac
1. Install Brew Package Manager
1. Installed necessary packages using `brew install openssl boost cmake qt5` command
1. Add `OPENSSL_ROOT_DIR="/usr/local/opt/openssl"` and `export PATH=/usr/local/opt/qt/bin:$PATH` to the _Environment Variables_
1. Go to Beam project folder and call `cmake -DCMAKE_BUILD_TYPE=Release . && make -j 4`
1. You'll find _Beam_ binary in `bin` folder, `beam-ui.exe` in `ui` subfolder
