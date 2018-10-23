FROM ubuntu:18.04

# System deps
RUN apt-get update && apt-get install -y \
    autoconf \
    automake \
    build-essential \
    cmake \
    git \
    libtool \
    make \
    ninja-build \
    python-dev \
    software-properties-common \
    sudo \
    unzip \
    wget

RUN apt-get clean autoclean
RUN apt-get autoremove -y

# Copy this code into place
COPY . /code

# Create a build directory
WORKDIR /build

# Run the build script
RUN python /code/bootstrap/build.py

CMD /bin/bash

