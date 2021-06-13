set ylabel 'Time (us)'
set xlabel 'Thread count'
set terminal png font " Times_New_Roman,12 "
set key left
set output "output.png"
set grid
# set logscale y 2
set xtics 1
# set ytics 1
plot "threadpool_pi.txt" u 1:($2/1000) w lines title "original", \
    'afn_threadpool_pi.txt' u 1:($2/1000) w lines title "lock-free queue + affinity-based thread", \
    'afn_threadpool_pi_v2.txt' u 1:($2/1000) w lines title "lock-free queue", \