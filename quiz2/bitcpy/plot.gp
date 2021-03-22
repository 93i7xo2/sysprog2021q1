set ylabel 'time (ms)'
set xlabel 'size (KB)'
set terminal png font " Times_New_Roman,12 "
set key left
set output "output.png"
set grid
set logscale y 2
set ytics 2
plot "output" u ($1/1024/8):($2*1000) w lines title "8-bit bitcpy", \
    '' u ($1/1024/8):($3*1000) w lines title "64-bit bitcpy"