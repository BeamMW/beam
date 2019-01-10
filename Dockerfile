FROM ubuntu:18.04 as builder

# builds without UI elements by default
# remove `-DBEAM_NO_QT_UI_WALLET=On` to build with UI

ARG DEBIAN_FRONTEND=noninteractive

RUN apt-get -y update && apt-get -y upgrade
RUN apt-get -y install software-properties-common
RUN add-apt-repository ppa:ubuntu-toolchain-r/test && apt-get update
RUN apt-get install -y apt-utils ca-certificates git cmake build-essential \
    libboost-all-dev libssl-dev libdrm-dev

COPY . /beam/
WORKDIR /beam
RUN cmake -DCMAKE_BUILD_TYPE=RELEASE -DBEAM_NO_QT_UI_WALLET=On .
RUN make -j8

FROM ubuntu:18.04

RUN apt-get -y update && apt-get -y upgrade && \
    apt-get -y install libboost-all-dev libssl1.1
#    apt-get -y install libboost-all libssl libdrm

COPY --from=builder \
    /beam/beam/beam-node \
    /beam/beam/beam-node.cfg \
    /beam/wallet/beam-wallet \
    /beam/wallet/beam-wallet.cfg \
    /usr/bin/

VOLUME /root/.beam
WORKDIR /root/.beam
CMD ["/usr/bin/beam-node", "--peer=eu-node01.mainnet.beam.mw:8100"]
