FROM ubuntu:18.04

RUN    apt-get update \
    && apt-get install -y --no-install-recommends cmake make gcc g++ gcc-multilib g++-multilib \
    && rm -rf /var/lib/apt/lists/*

ADD . /tool/
WORKDIR /tool
RUN ./build.sh

ENTRYPOINT ["./nitwit.sh"]
