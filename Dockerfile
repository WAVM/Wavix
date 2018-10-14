FROM ubuntu:18.04

RUN apt-get update
RUN apt-get install -y software-properties-common wget

# System deps
RUN apt-get update
RUN apt-get install -y build-essential \
    sudo \
    subversion \
    make \
    cmake \
    ninja-build \
    git \
    clang-6.0 \
    curl \
    autoconf \
    automake \
    libtool \
    unzip \
    python3-dev \
    python3-pip 

RUN apt-get clean autoclean
RUN apt-get autoremove -y

# Python tools
RUN pip3 install requests

# Clang
RUN ln -s /usr/bin/clang-6.0 /usr/bin/clang
RUN ln -s /usr/bin/clang-cpp-6.0 /usr/bin/clang-cpp
RUN ln -s /usr/bin/clang++-6.0 /usr/bin/clang++
RUN ln -s /usr/bin/llvm-6.0 /usr/bin/llvm

# Copy this code into place
COPY . /code

# Create a build directory
WORKDIR /build

# Run the build script
RUN python /code/bootstrap/build.py

CMD /bin/bash

