### Install header files and library for cURL
```
sudo apt-get install libcurl4-openssl-dev
```

### Build protobuf v3.7.0
To build protobuf from source, install the following tools:
```
$ sudo apt-get install autoconf automake libtool curl make g++ unzip
```
You can also get the source by "git clone" our git repository. Make sure you
have also cloned the submodules and generated the configure script (skip this
if you are using a release .tar.gz or .zip package):
```
$ git clone https://github.com/protocolbuffers/protobuf.git
$ cd protobuf
$ git checkout v3.7.0
$ git submodule update --init --recursive
$ ./autogen.sh
```
To build and install the C++ Protocol Buffer runtime and the Protocol
Buffer compiler (protoc) execute the following:
```
$ ./configure
$ make
$ make check
$ sudo make install
$ sudo ldconfig # refresh shared library cache.
```
If "make check" fails, you can still install, but it is likely that
some features of this library will not work correctly on your system.
Proceed at your own risk.
