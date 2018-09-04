[![Build Status](https://travis-ci.org/beam-mw/beam.svg?branch=master)](https://travis-ci.org/beam-mw/beam)
[![Build status](https://ci.appveyor.com/api/projects/status/i2vuy3bvjf31ocr2/branch/master?svg=true)](https://ci.appveyor.com/project/beam-mw/beam/branch/master)

# Beam. 
###Scalable Confidential Cryptocurrency
###A Mimblewimble Implementation

BEAM is a next generation confidential cryptocurrency based on an elegant and innovative [Mimblewimble protocol](https://www.scribd.com/document/382681522/The-Mimblewimble-white-papers).

[Read our position paper](https://www.scribd.com/document/382680718/BEAM-Position-Paper-V-0-1)

Things that make BEAM special include:

* Users have complete control over privacy - a user decides which information will be available and to which parties, having complete control over his personal data in accordance to his will and applicable laws.
* Confidentiality without penalty - in BEAM confidential transactions do not cause bloating of the blockchain, avoiding excessive computational overhead or penalty on performance or scalability while completely concealing the transaction value.
* No trusted setup required
* Blocks are mined using Equihash Proof-of-Work algorithm.
* Limited emission using periodic halving.
* No addresses are stored in the blockchain - no information whatsoever about either the sender or the receiver of a transaction is stored in the blockchain.
* Superior scalability through compact blockchain size - using the “cut-through” feature of
Mimblewimble makes the BEAM blockchain orders of magnitude smaller than any other
blockchain implementation.
* BEAM supports many transaction types such as escrow transactions, time locked
transactions, atomic swaps and more.
* No premine. No ICO. Backed by a treasury, emitted from every block during the first five
years.
* Implemented from scratch in C++ by a team of professional developers.

# Roadmap

- March 2018     : Project started
- Jun 2018       : Internal POC featuring fully functional node and CLI wallet
- September 2018 : Testnet 1 and Graphical Wallet
- December 2018  : Mainnet launch

# Current status

- Fully functional wallet with key generator and storage supporting secure and confidential online transactions.
- Full node with both transaction and block validation and full UTXO state management.
- Equihash miner with periodic mining difficulty adjustment.
- Batch Bulletproofs, the efficient non-interactive zero knowledge range proofs now in batch mode
- Graphical Wallet Application for Linux, Mac and Windows platforms
- Offline transactions using Secure BBS system
- ChainWork - sublinear blockchain validation, based on Benedikt Bünz FlyClient idea
- Compact history using cut through


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
