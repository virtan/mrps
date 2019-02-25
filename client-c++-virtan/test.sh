#!/bin/sh

set -e

if [[ "${#}" -lt 2 ]]; then
  echo "Usage: ${0} host port [number-of-threads = 24 [number-of-iteration = 16 [client-docker-image]]]" >&2
  exit 1
fi

target_host="${1}"
target_port=${2}

if [[ "${#}" -ge 3 ]]; then
  work_threads=${3}
else
  work_threads=24
fi

if [[ "${#}" -ge 4 ]]; then
  run_iterations=${4}
else
  run_iterations=16
fi

if [[ "${#}" -ge 5 ]]; then
  docker_image="${5}"
else
  docker_image="abrarov/client-cpp-virtan"
fi

max_msgs=0
best_stats=""

echo "Running container from ${docker_image} image ${run_iterations} times with command: ${target_host} ${target_port} ${work_threads}"

i=0
while [[ ${i} -lt ${run_iterations} ]]
do
  #echo "Running iteration #${i}..."
  stats=$(docker run --rm ${docker_image} ${target_host} ${target_port} ${work_threads} 2>&1 | grep -A 1 "Final statistics" | grep "Chld:")
  #echo "Stats: ${stats}"
  msgs=$(echo "${stats}" | sed -r 's/.*Msgs: ([0-9]+)/\1/')
  if [[ "${msgs}" -gt ${max_msgs} ]]; then
    max_msgs=${msgs}
    best_stats="${stats}"
  fi
  i=$(( ${i} + 1 ))
done

echo "Best stats: ${best_stats}"
