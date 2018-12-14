FROM ubuntu:18.04 as builder

# builds without UI elements by default
# remove `-DBEAM_NO_QT_UI_WALLET=On` to build with UI

ARG DEBIAN_FRONTEND=noninteractive

RUN apt-get -y update && apt-get -y upgrade
RUN apt-get -y install software-properties-common
RUN add-apt-repository ppa:ubuntu-toolchain-r/test && apt-get update
RUN apt-get install -y apt-utils ca-certificates curl git-all cmake build-essential \
    vim inetutils-tools wget libboost-all-dev libssl-dev libdrm-dev g++ gcc

RUN git clone https://github.com/Whiteblock/beam
WORKDIR /beam
RUN git checkout testnet3_cpu_mining && cmake -DCMAKE_BUILD_TYPE=RELEASE -DBEAM_NO_QT_UI_WALLET=On . && make -j8

ENTRYPOINT ["/bin/bash"]
