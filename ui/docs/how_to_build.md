# Build static QT5

## Windows

- go to https://download.qt.io/official_releases/qt/5.11/5.11.1/single/ and download `qt-everywhere-src-...`
- unpack, go to `.../qtbase/mkspecs/common/msvc-desktop.conf` and change all `MD` to `MT`
- add `QMAKE_CXXFLAGS += /MP` option
- run Visual Studio command line tools, cd to the QT dir, and call `configure -opensource -confirm-license -static -nomake examples -nomake tests -nomake tools -no-sql-sqlite -platform win32-msvc -qt-zlib -qt-libpng -qt-libjpeg -opengl dynamic`
- call `nmake`
- call `nmake install`
