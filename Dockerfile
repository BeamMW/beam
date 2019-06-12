FROM ubuntu:19.10

# Define arguments
ARG beam=beam-node-masternet.tar.gz 

# Install.
RUN \
  apt-get -y  update  && \
  mkdir -p  /home/beam/node/ && \
  apt -y install wget  && \
  wget -P /home/beam/node/  https://builds.beam.mw/master/latest/Release/linux/$beam  && \
  cd /home/beam/node/  && tar -xvf $beam && rm -rf $beam

# Define volume & working directory.
WORKDIR /home/beam/node/
VOLUME /home/beam/node/

# Define default command.
EXPOSE 8100
CMD ["/home/beam/node/beam-node-masternet", "--peer=eu-node01.masternet.beam.mw:8100,eu-node02.masternet.beam.mw:8100,eu-node04.masternet.beam.mw:8100,eu-node04.masternet.beam.mw:8100"]
