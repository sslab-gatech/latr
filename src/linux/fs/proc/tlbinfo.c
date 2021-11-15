// SPDX-License-Identifier: GPL-2.0-or-later
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/proc_fs.h>
#include <linux/sched.h>
#include <linux/seq_file.h>
#include <linux/time.h>
#include <linux/kernel_stat.h>
#include <linux/cputime.h>
#include <linux/mm.h>

#define get_min_time(t1, t2) do { \
        if ((t1) > (t2)) \
                (t1) = (t2); \
} while (0)

#define get_max_time(t1, t2) do { \
        if ((t1) < (t2)) \
                (t1) = (t2); \
} while (0)


DEFINE_PER_CPU(struct tlbinfo, copy_tlbinfo);

static void percore_tlbinfo_func(void *d)
{
        struct tlbinfo *t = this_cpu_ptr(&copy_tlbinfo);
        memcpy(t, this_cpu_ptr(&tlbinfo), sizeof(struct tlbinfo));
}

static void percore_tlbinforeset_func(void *d)
{
        memset(this_cpu_ptr(&tlbinfo), 0, sizeof(struct tlbinfo));
}

static int tlbinfo_proc_show(struct seq_file *m, void *v)
{
	int i;
        u64 total_time = 0, total_intlb_time = 0;
        u64 total_gtlb_time = 0, total_rtlb_time = 0;
        u64 total_rtlbs = 0, total_gtlbs = 0, total_intlbs = 0;
        u32 min_rtlb = 1UL << 31, min_gtlb = 1UL << 31, min_intlb = 1UL << 31;
        u32 max_rtlb = 0, max_gtlb = 0, max_intlb = 0;
        u32 total_fallback_ipi =0;
        u32 total_count_ipi =0;

        smp_call_function_many(cpu_online_mask, percore_tlbinfo_func, NULL, 1);

        seq_printf(m, "CPU\tStlb\tTime\tMin\tMax\tGtlb\tTime\tMin\tMax\tRtlb\tTime\tMin\tMax (cycles)\tCount IPI\tFallback IPI\n");
	for_each_possible_cpu(i) {
                struct tlbinfo *t = per_cpu_ptr(&copy_tlbinfo, i);
		seq_printf(m, "%d\t%Lu\t%Lu\t%u\t%u\t%Lu\t%Lu\t%u\t%u\t%Lu"
                           "\t%Lu\t%u\t%u\t%u\t%u\n",
                           i, t->inv_tlbs.num_tlbs, t->inv_tlbs.total_time,
                           t->inv_tlbs.min_time, t->inv_tlbs.max_time,
                           t->global_tlbs.num_tlbs, t->global_tlbs.total_time,
                           t->global_tlbs.min_time, t->global_tlbs.max_time,
                           t->remote_tlbs.num_tlbs, t->remote_tlbs.total_time,
                           t->remote_tlbs.min_time, t->remote_tlbs.max_time,
                           t->remote_tlbs.count_ipi, t->remote_tlbs.fallback_ipi);
                total_time += t->remote_tlbs.total_time +
                        t->inv_tlbs.total_time + t->global_tlbs.total_time;
                total_intlb_time += t->inv_tlbs.total_time;
                total_gtlb_time += t->global_tlbs.total_time;
                total_rtlb_time += t->remote_tlbs.total_time;
                total_intlbs += t->inv_tlbs.num_tlbs;
                total_gtlbs += t->global_tlbs.num_tlbs;
                total_rtlbs += t->remote_tlbs.num_tlbs;
                total_count_ipi += t->remote_tlbs.count_ipi;
                total_fallback_ipi += t->remote_tlbs.fallback_ipi;
                get_min_time(min_intlb, t->inv_tlbs.min_time);
                get_max_time(max_intlb, t->inv_tlbs.max_time);
                get_min_time(min_gtlb, t->global_tlbs.min_time);
                get_max_time(max_gtlb, t->global_tlbs.max_time);
                get_min_time(min_rtlb, t->remote_tlbs.min_time);
                get_max_time(max_rtlb, t->remote_tlbs.max_time);
	}
        seq_printf(m, "Total\t%Lu\t%Lu\t%Lu\t%u\t%u\t%Lu\t%Lu\t%u\t%u\t%Lu"
                   "\t%Lu\t%u\t%u\t%u\t%u\n",
                   total_time, total_intlbs, total_intlb_time,
                   min_intlb, max_intlb,
                   total_gtlbs, total_gtlb_time, min_gtlb, max_gtlb,
                   total_rtlbs, total_rtlb_time, min_rtlb, max_rtlb,
                   total_count_ipi, total_fallback_ipi);

        smp_call_function_many(cpu_online_mask, percore_tlbinforeset_func,
                        NULL, 1);

	return 0;
}

static int tlbinfo_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, tlbinfo_proc_show, NULL);
}

static const struct file_operations tlbinfo_proc_fops = {
	.open		= tlbinfo_proc_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static int __init proc_tlbinfo_init(void)
{
	proc_create("tlbinfo", 0, NULL, &tlbinfo_proc_fops);
	return 0;
}
fs_initcall(proc_tlbinfo_init);
