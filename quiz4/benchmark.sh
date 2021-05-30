#!/bin/bash
for thread_count in `seq 1 5 1000`; do
    tmp=()
    for round in `seq 1 1 30`; do
        tmp+=($(./pi $thread_count | grep "time" | grep -oE "[0-9]+"))
    done

    # pipe capacity: 65,536 bytes
    avg=$(printf '%s\n' "${tmp[@]}" | python3 filter.py)
    echo "$thread_count" "$avg"
done