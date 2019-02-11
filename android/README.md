# Build for Android

## Install NDK 
https://developer.android.com/ndk/downloads/
set `ANDROID_NDK` system variable (C:\Users\grigo\projects\libs\android-ndk-r17b)


## Build Boost for Android 
https://github.com/moritz-wundke/Boost-for-Android
or clone https://github.com/nesbox/boost_1_68-android
set `BOOST_ROOT_ANDROID` variable (C:\Users\grigo\projects\libs\boost_1_68-android\armeabi-v7a)


## clone OpenSSL prebuilt binaries 
https://github.com/Sharm/Prebuilt-OpenSSL-Android or here https://github.com/nesbox/Prebuilt-OpenSSL-Android
set `OPENSSL_ROOT_DIR_ANDROID` (C:\Users\grigo\projects\libs\OpenSSL-Android\Prebuilt\armv7-shared)


## Build with MinGW 
armv7 - `cmake -G "MinGW Makefiles" -DCMAKE_SH=CMAKE_SH-NOTFOUND -DANDROID_NATIVE_API_LEVEL=23 -DCMAKE_TOOLCHAIN_FILE=$ANDROID_NDK/build/cmake/android.toolchain.cmake -DANDROID_ABI=armeabi-v7a .`
x86 - `cmake -G "MinGW Makefiles" -DCMAKE_SH=CMAKE_SH-NOTFOUND -DANDROID_NATIVE_API_LEVEL=23 -DCMAKE_TOOLCHAIN_FILE=$ANDROID_NDK/build/cmake/android.toolchain.cmake -DANDROID_ABI=x86 .`

