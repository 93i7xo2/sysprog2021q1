set ylabel 'time (us)'
set terminal png font " Times_New_Roman,12 "
set output "output.png"
set grid
set boxwidth 0.5
set style fill solid
set yrange [0:]
plot "data.txt" using 1:($3/1000):xtic(2) with boxes title ""