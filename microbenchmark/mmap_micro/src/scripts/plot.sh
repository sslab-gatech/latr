#!/bin/bash

mkdir -p analysis

iterations=500000
pages=1
prefix=$1
# Needed to convert from rdtsc to seconds
frequency=$2

filename_output_tlb_time=${prefix}-tlbinfo-time-$iterations-$pages.txt
filename_output_tlb_avg=${prefix}-tlbinfo-avg-$iterations-$pages.txt
filename_output_tlb_min=${prefix}-tlbinfo-min-$iterations-$pages.txt
filename_output_tlb_max=${prefix}-tlbinfo-max-$iterations-$pages.txt
filename_output_avg=${prefix}-avg-$iterations-$pages.txt
filename_output_diff=${prefix}-diff-$iterations-$pages.txt
filename_output_min=${prefix}-min-$iterations-$pages.txt

for cores in $(seq 1 16)
do
  filename_input=${prefix}_log-cores-$cores-$iterations-$pages

  filename_tlb_after=tlbinfo_after_${prefix}_log-cores-$cores-$iterations-$pages

  iter_avg_time=`cat ../log/$filename_input | awk '/^time per iteration:/ {print $4}'`
  iter_min_time=`cat ../log/$filename_input | awk '/^minimum time per iteration:/ {print $5}'`

  after_count=`cat ../log/$filename_tlb_after | awk 'NR==2 {print $10}'`
  after_time=`cat ../log/$filename_tlb_after | awk 'NR==2 {print $11}'`
  after_min=`cat ../log/$filename_tlb_after | awk 'NR==2 {print $12}'`
  after_max=`cat ../log/$filename_tlb_after | awk 'NR==2 {print $13}'`
  tlbinfo_avg=0
  if [ "$after_count" -gt 0 ]; then
    tlbinfo_avg=`bc <<< "scale=2; ${after_time} / ${after_count} / ${frequency}"`
  fi
  diff=`bc <<< "scale=2; ${iter_avg_time} - ${tlbinfo_avg}"`

  echo "$cores $iter_avg_time" >> analysis/$filename_output_avg
  echo "$cores $iter_min_time" >> analysis/$filename_output_min
  echo "$cores $after_time" >> analysis/$filename_output_tlb_time
  echo "$cores $after_min" >> analysis/$filename_output_tlb_min
  echo "$cores $after_max" >> analysis/$filename_output_tlb_max
  echo "$cores $tlbinfo_avg" >> analysis/$filename_output_tlb_avg
  echo "$cores $diff" >> analysis/$filename_output_diff
done
