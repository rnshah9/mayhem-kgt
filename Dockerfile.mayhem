# Build Stage
FROM --platform=linux/amd64 ubuntu:20.04 as builder
RUN apt-get update
RUN DEBIAN_FRONTEND=noninteractive apt-get install -y bmake gcc

ADD . /kgt
WORKDIR /kgt
RUN bmake -r install

RUN mkdir -p /deps
RUN ldd /kgt/build/bin/kgt | tr -s '[:blank:]' '\n' | grep '^/' | xargs -I % sh -c 'cp % /deps;'

FROM ubuntu:20.04 as package

COPY --from=builder /deps /deps
COPY --from=builder /kgt/build/bin/kgt /kgt/build/bin/kgt
ENV LD_LIBRARY_PATH=/deps
