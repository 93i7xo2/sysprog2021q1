#!/bin/bash
cores=`nproc --all`
round=100
testfiles=(threadpool_pi afn_threadpool_pi afn_threadpool_pi_v2)

for file in "${testfiles[@]}"; do
    rm -f "${file}.txt"
    for thread_count in `seq 1 1 $cores`; do
        creation=()
        execution=()
        deconstruction=()
        for r in `seq 1 1 $round`; do
            echo -ne "$file $thread_count/$cores $r/$round  \r"
            result=$(./$file $thread_count)
            creation+=($(echo "$result" | grep "Creation" | grep -oE "[0-9]+"))
            execution+=($(echo "$result" | grep "Execution" | grep -oE "[0-9]+"))
            deconstruction+=($(echo "$result" | grep "Deconstruction" | grep -oE "[0-9]+"))
        done

        # pipe capacity: 65,536 bytes
        cre=$(printf '%s\n' "${creation[@]}" | python3 filter.py)
        exe=$(printf '%s\n' "${execution[@]}" | python3 filter.py)
        des=$(printf '%s\n' "${deconstruction[@]}" | python3 filter.py)
        echo "$thread_count" "$cre">>"${file}_cre.txt"
        echo "$thread_count" "$exe">>"${file}_run.txt"
        echo "$thread_count" "$des">>"${file}_des.txt"
    done
    echo "" 
done
