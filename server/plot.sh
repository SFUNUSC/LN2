#! /bin/sh
# LN2 System - plots and uploads data from a locally saved text file
# argument 1 contains name of file containing plottable data, argument 2 contains network location to upload to,
# argument 3 contains the number of overflow sensors
# argument 4 contains the number of temperature sensors

echo Plotting file...
gnuplot <<**
set term png size 768,1024
set output '$1.png'

set multiplot layout 2,1 scale 0.95,1

set key autotitle columnhead
set xdata time
set timefmt "%d-%m-%Y,%H:%M:%S"
set format x "%d %b\n %H:%M"
set xlabel "Time (Hr:Min)"
set ylabel "Sensor Voltage (V)"
#plot voltage sensor readings
set nokey 

set yrange [1:7]
if ( $3 == 1 ) \
plot "$1" u 1:4 w p t 'Sensor Voltage'

#set ylabel " Sensor temperature (K)"
#plot temperature sensor readings
#if ( $4 == 1 ) \
#plot "$1" u 1:5 w p t 'Sensor Temperature'

set autoscale y
set ylabel "Scale weight (kg)"
plot "$1" u 1:3 w p  t 'Scale Reading'

set xtics 120


replot
unset multiplot 
quit
**
echo File plotted!
echo Uploading data...
# uploads/moves file, preserving permissions
# NOTE: if accessing a network location, you will need to set up automated SSH login to avoid a password prompt
cp $1 $2$1.txt
cp $1.png $2$1.png
cp $1.png $2LATEST.png
echo Data uploaded!
# clean up remaining files
rm $1
rm $1.png
