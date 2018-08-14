[![Build Status](https://travis-ci.com/beam-mw/beam.svg?token=9GsAxqTGpjsBMMHVvgzz&branch=master)](https://travis-ci.com/beam-mw/beam)
[![Build Status](https://ci.appveyor.com/api/projects/status/voh9d2q7i5oj20rr/branch/master?svg=true)](https://ci.appveyor.com/project/beam-mw/beam/branch/master)

# Beam

# How to build

## Windows
1. Install Visual Studio >= 2017 with CMake support.
1. Download and install Boost prebuilt binaries https://sourceforge.net/projects/boost/files/boost-binaries/, also add `BOOST_ROOT` to the _Environment Variables_.
1. Download and install OpenSSL prebuilt binaries https://slproweb.com/products/Win32OpenSSL.html (`Win64 OpenSSL v1.1.0h` for example) and add `OPENSSL_ROOT_DIR` to the _Environment Variables_.
1. Download and install QT 5.11 https://download.qt.io/official_releases/qt/5.11/5.11.0/qt-opensource-windows-x86-5.11.0.exe.mirrorlist and add `QT5_ROOT_DIR` to the _Environment Variables_ (usually it looks like `.../5.11.0/msvc2017_64`), also add `QML_IMPORT_PATH` (it should look like `%QT5_ROOT_DIR%\qml`). BTW disabling system antivirus on Windows makes QT installing process much faster.
1. Open project folder in Visual Studio, select your target (`Release-x64` for example, if you downloaded 64bit Boost and OpenSSL) and select `CMake -> Build All`.
1. Go to `CMake -> Cache -> Open Cache Folder -> beam` (you'll find `beam.exe` in the `beam` subfolder, `beam-wallet.exe` in `ui` subfolder).

## Linux
1. Make sure you have installed `g++-7 libboost-all-dev libssl-dev` packages.
1. Install latest CMake `wget "https://cmake.org/files/v3.12/cmake-3.12.0-Linux-x86_64.sh"` and `sudo sh cmake-3.12.0-Linux-x86_64.sh --skip-license --prefix=/usr`.
1. Add proper QT 5.11 repository depending on your system https://launchpad.net/~beineri (for example, choose `Qt 5.10.1 for /opt Trusty` if you have Ubuntu 14.04), install `sudo apt-get install qt510declarative qt510svg` packages and add `export PATH=/opt/qt511/bin:$PATH`.
1. Go to Beam project folder and call `cmake -DCMAKE_BUILD_TYPE=Release . && make -j4`.
1. You'll find _Beam_ binary in `bin` folder, `beam-wallet` in `ui` subfolder.

## Mac
1. Install Brew Package Manager.
1. Installed necessary packages using `brew install openssl boost cmake qt5` command.
1. Add `OPENSSL_ROOT_DIR="/usr/local/opt/openssl"` and `export PATH=/usr/local/opt/qt/bin:$PATH` to the _Environment Variables_.
1. Go to Beam project folder and call `cmake -DCMAKE_BUILD_TYPE=Release . && make -j4`.
1. You'll find _Beam_ binary in `bin` folder, `beam-wallet` in `ui` subfolder.

If you don't want to build UI don't install QT5 and comment `CMakeLists.txt:130 # add_subdirectory(ui)` line.
