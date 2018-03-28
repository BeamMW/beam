#!/bin/sh
# The purpose of this file is to install libsodium in
# the Travis CI environment. Outside this environment,
# you would probably not want to install it like this.

set -e

# check if libsodium is already installed
if [ ! -d "$HOME/libsodium/lib" ]; then
  wget https://github.com/jedisct1/libsodium/releases/download/1.0.16/libsodium-1.0.16.tar.gz
  tar xvfz libsodium-1.0.16.tar.gz
  cd libsodium-1.0.16
  ./configure
  make
  sudo make install
else
  echo 'Using cached directory.'
fi
