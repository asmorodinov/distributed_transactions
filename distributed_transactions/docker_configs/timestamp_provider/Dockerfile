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

# build timestamp_provider
COPY distributed_transactions/timestamp_provider distributed_transactions/timestamp_provider
RUN ./ya make -r distributed_transactions/timestamp_provider

RUN cp distributed_transactions/timestamp_provider/timestamp_provider ./
