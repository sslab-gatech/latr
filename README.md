Latr—lazy TLB coherence—is a software-based TLB shootdown mechanism that can
alleviate the overhead of the synchronous TLB shootdown mechanism in Linux
Kernel. This repository has the modified Linux kernel and micro benchmarks
used in the evaluation.

Build and install the kernel:

      - LATR was developed on Linux 4.10.
      - The Linux kernel source is available in ./src/linux/.
      - Enable LAZY_TLB_SHOOTDOWN in arch/Kconfig.
      - Build and install the kernel.

Running the micro-benchmark:

      - The mmap micro benchmark is availabe in ./microbenchmark/mmap_micro/src/.
      - build the mmap microbenchmark by:
        - make cmake
        - make
      - use run.py in the scripts directory
        (./microbenchmark/mmap_micro/src/scripts/) to run the micro benchmark.

@inproceedings{LATR:ASPLOS18,
 author = {Kumar, Mohan Kumar and Maass, Steffen and Kashyap, Sanidhya and Vesel\'{y}, J\'{a}n and Yan, Zi and Kim, Taesoo and Bhattacharjee, Abhishek and Krishna, Tushar},
 title = {{LATR: Lazy Translation Coherence}},
 booktitle = {Proceedings of the Twenty-Third International Conference on Architectural Support for Programming Languages and Operating Systems},
 series = {ASPLOS '18},
 month = mar,
 year = {2018},
 address = {Williamsburg, VA, USA},
 pages = {651--664},
}