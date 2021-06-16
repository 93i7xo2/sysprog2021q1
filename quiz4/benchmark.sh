#!/bin/bash
cores=`nproc --all`
round=100
testfiles=(threadpool_pi afn_threadpool_pi afn_threadpool_pi_v2)

for file in "${testfiles[@]}"; do
    rm -f "${file}.txt"
    for thread_count in `seq 1 1 $cores`; do
        tmp=()
        for r in `seq 1 1 $round`; do
            echo -ne "$file $thread_count/$cores $r/$round  \r"
            tmp+=($(./$file $thread_count | grep "time" | grep -oE "[0-9]+"))
        done

        # pipe capacity: 65,536 bytes
        avg=$(printf '%s\n' "${tmp[@]}" | python3 filter.py)
        echo "$thread_count" "$avg">>"${file}.txt"
    done
    echo "" 
done