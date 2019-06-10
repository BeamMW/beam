FROM ubuntu:19.10

# Define arguments
ARG beam=beam-node.tar.gz

# Install.
RUN \
  apt-get -y  update  && \
  mkdir -p  /home/beam/node/ && \
  apt -y install wget  && \
  wget -P /home/beam/node/  https://builds.beam.mw/mainnet/latest/Release/linux/$beam  && \
  cd /home/beam/node/  && tar -xvf $beam && rm -rf $beam

# Define volume & working directory.
WORKDIR /home/beam/node/
VOLUME /home/beam/node/

# Define default command.
EXPOSE 8100
CMD ["/home/beam/node/beam-node", "--peer=eu-nodes.mainnet.beam.mw:8100,us-nodes.mainnet.beam.mw:8100,ap-nodes.mainnet.beam.mw:8100"]
