# Create an intermediate build image.
FROM ubuntu:18.04 AS build

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

# Create a final image that just includes the host and sys directories from the build image.
FROM ubuntu:18.04
COPY --from=build /build/host /build/host
COPY --from=build /build/sys /build/sys
WORKDIR /build

CMD /bin/bash

