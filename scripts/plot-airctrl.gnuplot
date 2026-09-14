# AirControl CSV plotter (gnuplot 5.2 or newer)
# Usage:
#   gnuplot -c plot-airctrl.gnuplot [CSV [PNG]]

if (!exists("ARGC")) ARGC = 0
datafile = ARGC >= 1 ? ARG1 : "/var/log/airctrl.log"
outputfile = ARGC >= 2 ? ARG2 : "airctrl-status.png"

set datafile separator comma
set datafile columnheaders
set decimalsign locale "C"

# STATS_columns is the widest CSV row. A separate stats pass identifies every
# column containing at least one numeric value; text and _extra_json stay in the
# CSV but cannot be drawn on a numeric axis.
stats datafile using 0 nooutput
column_count = STATS_columns
array plot_columns[column_count]
plot_count = 0
do for [column_index = 2:column_count] {
    VALUE_records = -1
    stats datafile using (column(column_index)) name "VALUE" nooutput
    if (exists("VALUE_records")) {
        if (VALUE_records > 0) {
            plot_count = plot_count + 1
            plot_columns[plot_count] = column_index
        }
    }
}

if (plot_count < 1) {
    print sprintf("Keine numerischen Statuswerte in %s gefunden.", datafile)
    exit
}

panel_height = 230
set terminal pngcairo size 1600,(panel_height * plot_count) enhanced font "Sans,10"
set output outputfile
set xdata time
set timefmt "%Y-%m-%dT%H:%M:%S"
set format x "%d.%m.\n%H:%M"
set xlabel "Zeit (UTC)"
set grid xtics ytics back
set key top center horizontal
set border 3
set tics nomirror
set lmargin 12
set rmargin 3
set tmargin 2
set bmargin 4
panel_fraction = 1.0 / plot_count
set multiplot

do for [plot_index = 1:plot_count] {
    column_index = plot_columns[plot_index]
    set size 1.0,panel_fraction
    set origin 0.0,(1.0 - plot_index * panel_fraction)
    set ylabel "Wert"
    unset title
    plot datafile using (strptime("%Y-%m-%dT%H:%M:%S", substr(strcol(1), 1, 19))):(column(column_index)) \
        with linespoints pointtype 7 pointsize 0.25 linewidth 1.2 title columnhead(column_index)
}

unset multiplot
unset output
print sprintf("Plot geschrieben: %s", outputfile)
