TARGET="`echo $TARGET`"

gplt_ver=`gnuplot --version |awk '{print $2}'`

set macros

if( gplt_ver >= 5.0){
    #command line argument '$0~$9' has been deprecated in 5.0
   if (ARG1 eq "") {
     # Sized for one column of a two column, 7.5" wide body
     # SIZE="3.05in,1.8in"

     # Sized for one column 6" wide body
     SIZE="3in,2.2in"
   } else {
     if (ARG1 eq "2col") {
       # Sized for 6" wide body
       #SIZE="2.95in,2.2in"
       # 5.5" wide body
       SIZE="2.7in,2.2in"
     } else {
       if (ARG1 eq "3col") {
         SIZE="2.25in,1.6in"
       } else {
         if (ARG1 eq "2x2") {
           # Sized for a 2x2 multiplot on a 6" wide body
           #SIZE="6in,4in"
           # 5.5" wide body
           SIZE="5.5in,3.7in"
         } else {
           SIZE=ARG1
         }
       }
     }
   }
 }else{
   if ("$0" eq "" || "$0"[0:1] eq "$$"[0:1]) {
     # Sized for one column of a two column, 7.5" wide body
     # SIZE="3.05in,1.8in"

     # Sized for one column 6" wide body
     SIZE="3in,2.2in"
   } else {
     if ("$0" eq "2col") {
       # Sized for 6" wide body
       #SIZE="2.95in,2.2in"
       # 5.5" wide body
       SIZE="2.7in,2.2in"
     } else {
       if ("$0" eq "3col") {
         SIZE="2.25in,1.6in"
       } else {
         if ("$0" eq "2x2") {
           # Sized for a 2x2 multiplot on a 6" wide body
           #SIZE="6in,4in"
           # 5.5" wide body
           SIZE="5.5in,3.7in"
         } else {
           SIZE="$0"
         }
       }
     }
   }
 }

if (!exists("SLIDES_SIZE")) {
  SLIDES_SIZE="720,500"
}

# Note: If you change the default font size, change \gpcode
TIKZ_FONT=exists("TIKZ_FONT") ? TIKZ_FONT : "'\\figureversion{tab},10'"
if (TARGET eq "paper-tikz") {
  set term tikz size @SIZE font @TIKZ_FONT
  set output
  set pointsize 1.5
  set key spacing 1.35
} else {
  if (TARGET eq "pdf") {
    set term pdfcairo size @SIZE linewidth 2 rounded font ',10'
    set output
  } else {
    if (TARGET eq "slides") {
      set term svg size @SLIDES_SIZE font "Open Sans,20" dashed linewidth 2 enhanced
#      set output
      set output "|sed 's/<svg/& style=\"font-weight:300\"/'"
    } else {
      if (!(TARGET eq "")) {
        if (TARGET eq "paper-epslatex") {
          set term epslatex color size @SIZE input font 6 header "\\scriptsize"
          set output
          set pointsize 1.5
          set key spacing 1.35
        } else {
          print sprintf("Unknown target %s!", TARGET)
        }
      }
    }
  }
}

set ytics nomirror
set xtics nomirror
set grid back lt 0 lt rgb '#999999'
set border 3 back

xaxis_optimus = 'set xrange [1:80]; \
          set xtics 10 add (1); \
          set xlabel "\\# cores"'
xaxis_goldbug = 'set xrange [1:32]; \
          set xtics 8 add (1); \
          set xlabel "\\# cores"'
xaxis_bumblebee_mic = 'set xrange [1:57]; \
          set xtics 8 add (1); \
          set xlabel "\\# cores"'

xaxis_host = 'set xrange [32*1024:128*1024*1024];\
	      unset xtics; \
	      set for [pow = 15:30:4] xtics add (fmtbinary(2**pow) 2**pow)'

xaxis_mic = 'set xrange [16*1024:4*1024*1024];\
	     unset xtics; \
	     set for [pow = 14:30:2] xtics add (fmtbinary(2**pow) 2**pow)'

fmtbinary(x) = x < 2**10 ? sprintf("%d", x) \
             : x < 2**20 ? sprintf("%d~KB", x/2**10) \
             : x < 2**30 ? sprintf("%d~MB", x/2**20) \
             : x < 2**40 ? sprintf("%d~GB", x/2**30) \
             : x < 2**50 ? sprintf("%d~TB", x/2**40) \
             : "too big"

set linetype 1 lw 2 lc rgb '#00dd00'
set linetype 2 lw 2 lc rgb '#0000ff'
set linetype 3 lw 2 lc rgb '#ff0000'

#btrfs
set style line 1 lt rgb "#A00000" lw 3 pt 1

#ext4
set style line 2 lt rgb "#00A000" lw 3 pt 2

#ext4_no_jnl
set style line 3 lt rgb "#5060D0" lw 3 pt 3

#f2fs
set style line 4 lt rgb "#F25900" lw 3 pt 4

#xfs
set style line 5 lt rgb "#008AB8" lw 3 pt 6

#tmpfs
set style line 6 lt rgb "#002E00" lw 3 pt 8

set style line 7 lt rgb "#00A000" lw 5 pt 0
set style line 8 lc rgb "#A00000" lw 4
set style line 9 lt rgb "#002E00" lw 5 pt 0
set style line 10 lt rgb "#F25900" lw 5 pt 0
set style line 11 lt rgb "#005490" lw 3 pt 9

# Since most of our graphs are scalability graphs, put key in top left
#set key at graph 0,1 left top Left reverse
# Bottom right for per-core scalability graphs
set key at graph 1,0.85

#
# RadixVM stuff
#

radix='with lp lt 1'
linux='with lp lt 2'
bonsai='with lp lt 3'

vmusing="using 'iv.cores':(column(2)/column('iv.cores'))"

#
# Multiplot stuff
#

mp_startx=0.090                 # Left edge of col 0 plot area
mp_starty=0.120                 # Top of row 0 plot area
mp_width=0.825                  # Total width of plot area
mp_height=0.780                 # Total height of plot area
mp_colgap=0.07                  # Gap between columns
mp_rowgap=0.15                  # Gap between rows
# The screen coordinate of the left edge of column col
mp_left(col)=mp_startx + col*((mp_width+mp_colgap)/real(mp_ncols))
# The screen coordinate of the top edge of row row
mp_top(row)=1 - (mp_starty + row*((mp_height+mp_rowgap)/real(mp_nrows)))

# Set up a multiplot with w columns and h rows
mpSetup(w,h) = sprintf('\
    mp_nplot=-1; \
    mp_ncols=%d; \
    mp_nrows=%d; \
    set multiplot', w, h)
# Start the next graph in the multiplot
mpNext = '\
    mp_nplot=mp_nplot+1; \
    set lmargin at screen mp_left(mp_nplot%mp_ncols); \
    set rmargin at screen mp_left(mp_nplot%mp_ncols+1)-mp_colgap; \
    set tmargin at screen mp_top(mp_nplot/mp_ncols); \
    set bmargin at screen mp_top(mp_nplot/mp_ncols+1)+mp_rowgap; \
    unset label 1'

# Set Y axis row label such that it aligns regardless of tic width
mpRowLabel(lbl) = \
    sprintf('set label 1 "%s" at graph -0.25,0.5 center rotate',lbl)

#
# Slides stuff
#

if (TARGET eq "slides") {
  set style line 1 lt 1 lc rgb "#8ae234" lw 3
  set style line 2 lt 1 lc rgb "#000000" lw 3

  # Based on
  # http://youinfinitesnake.blogspot.com/2011/02/attractive-scientific-plots-with.html

  # Line style for axes
  set style line 80 lt 1
  set style line 80 lt rgb "#808080"

  # Line style for grid
  set style line 81 lt 3  # Dotted
  set style line 81 lt rgb "#808080" lw 0.5

  set grid back linestyle 81
  set border 3 back linestyle 80
}
