# Multistage Dockerfile for the Aleth tools.
# It depends on sources being available in the docker context,
# so build it from the project root dir as
#     docker build -f scripts/docker/cpp-ethereum.dockerfile .


# Build stage

FROM alpine:latest AS builder
RUN apk add --no-cache \
        linux-headers \
        g++ \
        cmake \
        make \
        git
ADD . /source
WORKDIR /build
RUN cmake /source -DVMTRACE=ON -DCMAKE_INSTALL_PREFIX=/usr -DCMAKE_BUILD_TYPE=Release -DHUNTER_JOBS_NUMBER=$(nproc)
RUN make -j $(nproc) && make install

# Target: testeth
# This is not the last stage so build it as
#     docker build --target testeth -f scripts/docker/cpp-ethereum.dockerfile .

FROM alpine:latest AS testeth
RUN apk add --no-cache libstdc++
COPY --from=builder /build/test/testeth /usr/bin/
ENTRYPOINT ["/usr/bin/testeth"]


# Target: cpp-ethereum

FROM alpine:latest AS cpp-ethereum
RUN apk add --no-cache python3 libstdc++
COPY --from=builder /usr/bin/cpp-ethereum /source/scripts/cpp-ethereum.py /source/scripts/dopple.py /usr/bin/
COPY --from=builder /usr/share/cpp-ethereum/ /usr/share/cpp-ethereum/
EXPOSE 8545
ENTRYPOINT ["/usr/bin/cpp-ethereum.py"]
