TITLE = 'Custom Fan Curve'

set grid
set xlabel 'Temp'
set ylabel 'Percent'
set yrange [0:100]

plot 'curve.dat' using 1:2 with lp

pause -1
