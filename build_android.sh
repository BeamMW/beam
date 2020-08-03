#!/bin/sh

#  build_ios.sh
#  wallet_test
#
#  Created by Denis on 2/27/19.
#  Copyright © 2019 Denis. All rights reserved.
#
######## Boost Framework
#
#   1) download https://github.com/faithfracture/Apple-Boost-BuildScript
#   2) change boost.sh - add arm64e architecture to IOS_ARCHS=("armv7 arm64")
#   3) build
#   4) copy boost fraemwork to /Applications/Xcode.app/Contents/Developer/Platforms/iPhoneOS.platform/Developer/SDKs/iPhoneOS12.0.sdk/System/Library/Frameworks
#
######## Open SSL
#
#   1) download https://github.com/levigroker/GRKOpenSSLFramework
#   2) change build.sh - add build "arm64e" "${IPHONEOS_SDK}" "ios" below build "arm64" "${IPHONEOS_SDK}" "ios"
#   3) build



export BOOST_ROOT_ANDROID="/Users/denis/Documents/Projects/Xcode/beam_libs_android/boost_1_68-android"
export OPENSSL_ROOT_DIR_ANDROID="/Users/denis/Documents/Projects/Xcode/beam_libs_android/OpenSSL/Prebuilt/x86-shared"
export ANDROID_NDK_HOME="/Users/denis/Library/Android/sdk/ndk/21.3.6528147"
export OPENSSL_SSL_LIBRARY="/Users/Denis/Documents/Projects/Xcode/beam/openssl/lib/libssl.a"
export OPENSSL_LIBRARIES="/Users/Denis/Documents/Projects/Xcode/beam/openssl/lib/"
export BOOST_ROOT_IOS="/Users/Denis/Documents/Projects/Xcode/beam/boost"
export ANDROID_ABI=x86
export ANDROID_NATIVE_API_LEVEL=23

cmake . -DCMAKE_TOOLCHAIN_FILE=$ANDROID_NDK_HOME/build/cmake/android.toolchain.cmake -DANDROID_NATIVE_API_LEVEL=$ANDROID_NATIVE_API_LEVEL -DANDROID_ABI=$ANDROID_ABI -DANDROID=YES -DCMAKE_CXX_FLAGS=-stdlib=libc++ -Wno-error=deprecated -Wno-error=deprecated-declarations -DOPENSSL_ROOT_DIR=$OPENSSL_ROOT_DIR -DBOOST_ROOT_ANDROID=$BOOST_ROOT_ANDROID -DBUILD_TYPE=Release -DBEAM_BUILD_JNI=ON -DBEAM_TESTS_ENABLED=OFF


#-DCMAKE_TOOLCHAIN_FILE=ios.toolchain.cmake -DBEAM_NO_QT_UI_WALLET=ON -DIOS_PLATFORM=OS64 -DCMAKE_CXX_FLAGS=-stdlib=libc++ -DIOS_DEPLOYMENT_TARGET=11.0 -DENABLE_BITCODE=0 -DOPENSSL_ROOT_DIR=$OPENSSL_ROOT_DIR -DOPENSSL_CRYPTO_LIBRARY=$OPENSSL_CRYPTO_LIBRARY -DOPENSSL_INCLUDE_DIR=$OPENSSL_INCLUDE_DIR -DOPENSSL_SSL_LIBRARY=$OPENSSL_SSL_LIBRARY -DOPENSSL_LIBRARIES=$OPENSSL_LIBRARIES -DIOS=YES -DBOOST_ROOT_IOS=$BOOST_ROOT_IOS -Wno-error=deprecated-declarations -Wno-error=deprecated -DCMAKE_TRY_COMPILE_PLATFORM_VARIABLES=CMAKE_WARN_DEPRECATED -DBEAM_ATOMIC_SWAP_SUPPORT=OFF
