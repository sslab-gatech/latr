call "common.gnuplot" "3.2in,2in"

set terminal pdf
set output 'increasing-cores-1-page-min.pdf'

mp_startx=0.12
mp_starty=0.0
mp_height=0.90
mp_rowgap=0.15

eval mpSetup(1, 1)

eval mpNext
set key default

set yrange [0:6500]
set xrange [1:24]
set xlabel 'Cores'
set ylabel 'Latency (ns)'

plot '../analysis/min-500000-1.txt' using 1:2 notitle with lines ls 1
