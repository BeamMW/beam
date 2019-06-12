FROM ubuntu:18.04

# Define arguments
ARG beam=beam-node-testnet.tar.gz

# Install.
RUN \
  apt-get -y  update  && \
  mkdir -p  /home/beam/node/ && \
  apt-get -y install wget  && \
  wget -P /home/beam/node/  https://builds.beam.mw/testnet/latest/Release/linux/$beam  && \
  cd /home/beam/node/  && tar -xvf $beam && rm -rf $beam && \
  apt-get purge wget -y && \
  apt-get autoremove -y && \
  rm -rf /var/lib/apt/lists/*

# Define volume & working directory.
WORKDIR /home/beam/node/
VOLUME /home/beam/node/

# Define default command.
EXPOSE 10000
CMD ["./beam-node-testnet", "--peer=ap-nodes.testnet.beam.mw:8100,eu-nodes.testnet.beam.mw:8100,us-nodes.testnet.beam.mw:8100"]
