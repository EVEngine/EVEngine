FROM ubuntu:20.04

RUN apt-get update && \
    DEBIAN_FRONTEND=noninteractive TZ=Etc/UTC apt-get install -y \
    build-essential \
    cmake \
    ninja-build \
    git openssh-server\
    clang-12 \
    && apt-get clean \
    && rm -rf /var/lib/apt/lists/*

RUN useradd -ms /bin/bash evengine

# configure SSH for communication with Visual Studio Code
RUN mkdir -p /var/run/sshd

RUN echo 'evengine:evengine' | chpasswd \
    && sed -i 's/#PermitRootLogin prohibit-password/PermitRootLogin yes/' /etc/ssh/sshd_config \
    && sed 's@session\s*required\s*pam_loginuid.so@session optional pam_loginuid.so@g' -i /etc/pam.d/sshd

USER evengine
WORKDIR /home/evengine
EXPOSE 22
