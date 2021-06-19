set ylabel 'Time (us)'
set xlabel 'Thread count'
set terminal png font " Times_New_Roman,12 "
set key left
set output "running.png"
set grid
# set logscale y 2
set xtics 1
set output "creation.png"
set title "Thread Pool Creation Time"
plot "threadpool_pi_cre.txt" u 1:($2/1000) w lines title "original", \
    'afn_threadpool_pi_cre.txt' u 1:($2/1000) w lines title "lock-free queue + affinity-based thread", \
    'afn_threadpool_pi_v2_cre.txt' u 1:($2/1000) w lines title "lock-free queue"


set output "running.png"
set title "Task Execution Time"
plot "threadpool_pi_run.txt" u 1:($2/1000) w lines title "original", \
    'afn_threadpool_pi_run.txt' u 1:($2/1000) w lines title "lock-free queue + affinity-based thread", \
    'afn_threadpool_pi_v2_run.txt' u 1:($2/1000) w lines title "lock-free queue"


set output "deconstruction.png"
set title "Thread Pool Deconstruction Time"
plot "threadpool_pi_des.txt" u 1:($2/1000) w lines title "original", \
    'afn_threadpool_pi_des.txt' u 1:($2/1000) w lines title "lock-free queue + affinity-based thread", \
    'afn_threadpool_pi_v2_des.txt' u 1:($2/1000) w lines title "lock-free queue"