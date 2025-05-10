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

# build tablet_stress_test
COPY distributed_transactions/common distributed_transactions/common
COPY distributed_transactions/client distributed_transactions/client
COPY distributed_transactions/tablet/proxy distributed_transactions/tablet/proxy
COPY distributed_transactions/tablet/proto distributed_transactions/tablet/proto
COPY distributed_transactions/tablet/common distributed_transactions/tablet/common
COPY distributed_transactions/tablet_stress_test distributed_transactions/tablet_stress_test
RUN ./ya make -r distributed_transactions/tablet_stress_test

RUN cp distributed_transactions/tablet_stress_test/tablet_stress_test ./
