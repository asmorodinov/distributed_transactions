#!/bin/bash

set -eux

if [ "$#" -ne 5 ]; then
    echo "Illegal number of parameters"
    exit 1
fi

is_hlc=$1
is_read_blocking=$2
test_type=$3
network_type=$4
delay=$5

echo $is_hlc $test_type $network_type $delay

command="docker compose -f distributed_transactions/docker_configs/docker-compose"

if [ "$is_hlc" = "hlc" ]; then
    command+="-hlc.yml"
else
    command+=".yml"
fi

$command down
$command up --build -d

if [[ "$network_type" = "1" && "$delay" != "" ]]; then
    docker exec tablet_1 tc qdisc add dev eth0 root netem delay $delay
    docker exec tablet_2 tc qdisc add dev eth0 root netem delay $delay
    docker exec tablet_3 tc qdisc add dev eth0 root netem delay $delay

    if [ "$is_hlc" != "hlc" ]; then
        docker exec timestamp_provider tc qdisc add dev eth0 root netem delay $delay
    fi

    docker exec tablet_stress_test tc qdisc add dev eth0 root netem delay $delay
fi

if [[ "$network_type" = "2" && "$delay" != "" ]]; then
    if [ "$is_hlc" != "hlc" ]; then
        docker exec timestamp_provider tc qdisc add dev eth0 root netem delay $delay
    fi
fi

if [[ "$network_type" = "3" && "$delay" != "" ]]; then
    docker exec tablet_stress_test tc qdisc add dev eth0 root netem delay $delay
fi

test_command=""

if [[ "$test_type" = "additions" ]]; then
    test_command="docker exec tablet_stress_test ./tablet_stress_test --tablet tablet_1:8080 --tablet tablet_2:8080 --tablet tablet_3:8080 --writes 100 --keys 10 --threads 4 --type additions"
else
    test_command="docker exec tablet_stress_test ./tablet_stress_test --tablet tablet_1:8080 --tablet tablet_2:8080 --tablet tablet_3:8080 --writes 1000 --keys 30 --threads 4 --type transfers"
fi

name=""

if [[ "$is_read_blocking" = "blocking" ]]; then
    test_command+=" --blocking-read 1"
    name="blocking"
else
    name="nonblocking"
fi

name+=",$delay,$network_type"

test_command+=" --name $name"

$test_command

$command down
