#!/usr/bin/env python3
import os
import sys
import optparse

def runSingleConfiguration(binary, core, sockets, iterations, pages, log_name, log_dir, opts):
    if opts.prefix != "":
        log_name = opts.prefix + "_" + log_name
    log_tlb_info_name_before = "tlbinfo_before_" + log_name
    log_tlb_info_name_after = "tlbinfo_after_" + log_name

    logfile = os.path.join(log_dir, log_name)
    tlb_logfile_before = os.path.join(log_dir, log_tlb_info_name_before)
    tlb_logfile_after = os.path.join(log_dir, log_tlb_info_name_after)

    use_madvise = 1 if opts.use_madvise else 0

    # cmd = "sudo cat /proc/tlbinfo | tee %s" % (tlb_logfile_before)
    cmd = "sudo cat /proc/tlbinfo > %s" % (tlb_logfile_before)
    print(cmd)
    os.system(cmd)

    cmd = "./%s --cores %d --sockets %d --iterations %d --pages %d --use-madvise %s 2>&1 | tee %s" % (binary, core, sockets, iterations, pages, use_madvise, logfile)
    print(cmd)
    os.system(cmd)

    # cmd = "sudo cat /proc/tlbinfo | tee %s" % (tlb_logfile_after)
    cmd = "sudo cat /proc/tlbinfo > %s" % (tlb_logfile_after)
    print(cmd)
    os.system(cmd)

def runAll(opts):
    log_dir = "../log"
    os.system("mkdir -p %s" % (log_dir))

    binary = "../../build/Release/lib/mmap_mb"
    iterations = 500000
    pages = 1
    core = opts.pages_cores
    sockets = 1

    if opts.enable_pages:
        while pages <= opts.max_pages:
            log_name = "log-pages-%d-%d-%d" % (core, iterations, pages)
            runSingleConfiguration(binary, core, sockets, iterations, pages, log_name, log_dir, opts)
            pages = pages * 2

    if opts.enable_cores:
        pages = 1
        sockets = 1
        for core in range(1, opts.max_cores + 1):
            log_name = "log-cores-%d-%d-%d" % (core, iterations, pages)
            runSingleConfiguration(binary, core, sockets, iterations, pages, log_name, log_dir, opts)

    if opts.enable_sockets:
        pages = 1
        core = opts.sockets_cores
        for socket in range(1, opts.max_sockets + 1):
            log_name = "log-sockets-%d-%d-%d-%d" % (socket, core, iterations, pages)
            runSingleConfiguration(binary, core, socket, iterations, pages, log_name, log_dir, opts)

if __name__ == "__main__":
    parser = optparse.OptionParser()
    parser.add_option("--disable-pages", action="store_false", dest="enable_pages", default = True)
    parser.add_option("--disable-cores", action="store_false", dest="enable_cores", default = True)
    parser.add_option("--disable-sockets", action="store_false", dest="enable_sockets", default = True)
    parser.add_option("--use-madvise", action="store_true", dest="use_madvise", default = False)
    parser.add_option("--max-pages", default = 1, type="int")
    parser.add_option("--pages-cores", default = 2, type="int")
    parser.add_option("--max-cores", default = 1, type="int")
    parser.add_option("--max-sockets", default = 1, type="int")
    parser.add_option("--sockets-cores", default = 1, type="int")
    parser.add_option("--prefix", default = "")
    (opts, args) = parser.parse_args()

    runAll(opts)
