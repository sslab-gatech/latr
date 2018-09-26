call "common.gnuplot" "3.2in,2in"

set terminal pdf
set output 'tlb-basic-base-diff-cores-1-page.pdf'

mp_startx=0.12
mp_starty=0.0
mp_height=0.90
mp_rowgap=0.15

eval mpSetup(1, 1)

eval mpNext
set key default

set yrange [0:*]
set xrange [1:16]
set xlabel 'Cores'
set ylabel 'Latency (ns)'

plot '../analysis/tlb-basic-avg-500000-1.txt' using 1:2 title "tlb-basic" with lines ls 1, \
     '../analysis/base-diff-500000-1.txt' using 1:2 title "baseline diff" with lines ls 2
