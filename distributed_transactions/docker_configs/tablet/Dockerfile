# syntax=docker.io/docker/dockerfile:1.7-labs
# ^ for enabling --exclude flag

FROM ubuntu:22.04

WORKDIR /work

# install python to build ya
RUN apt-get update
RUN apt-get install -y python3

# needed for specifying network delays
RUN apt-get install iproute2 iputils-ping -y

# build ya
COPY --exclude=distributed_transactions . .
RUN ./ya make -r

# build tablet
COPY distributed_transactions/timestamp_provider distributed_transactions/timestamp_provider
COPY distributed_transactions/common distributed_transactions/common
COPY distributed_transactions/tablet distributed_transactions/tablet
RUN ./ya make -r distributed_transactions/tablet

RUN cp distributed_transactions/tablet/tablet ./
