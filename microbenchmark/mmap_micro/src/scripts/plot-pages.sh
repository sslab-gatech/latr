#!/bin/bash

mkdir -p analysis

iterations=500000

# Handle first core separately
cores=6

for pages in 1 2 4 8 16 32 64
do
  filename_input=log-cores-$cores-$iterations-$pages

  filename_tlb_after=tlbinfo_after$filename_input

  filename_output_tlb_time=tlbinfo-time-$iterations-pages.txt
  filename_output_tlb_min=tlbinfo-min-$iterations-pages.txt
  filename_output_tlb_max=tlbinfo-max-$iterations-pages.txt
  filename_output_avg=avg-$iterations-pages.txt
  filename_output_min=min-$iterations-pages.txt

  `cat ../log/$filename_input | awk '/^time per iteration:/ {print cores, $4}' cores=$pages >> analysis/$filename_output_avg`
  `cat ../log/$filename_input | awk '/^minimum time per iteration:/ {print cores, $5}' cores=$pages >> analysis/$filename_output_min`

  after_time=`cat ../log/$filename_tlb_after | awk 'NR==2 {print $11}'`
  after_min=`cat ../log/$filename_tlb_after | awk 'NR==2 {print $12}'`
  after_max=`cat ../log/$filename_tlb_after | awk 'NR==2 {print $13}'`
  echo "$pages $after_time" >> analysis/$filename_output_tlb_time
  echo "$pages $after_min" >> analysis/$filename_output_tlb_min
  echo "$pages $after_max" >> analysis/$filename_output_tlb_max
done
