/* SPDX-License-Identifier: GPL-2.0-only */
#include <linux/init.h>
/* latr */
#include <linux/kthread.h>
/*******/
#include <linux/mm.h>
#include <linux/spinlock.h>
#include <linux/smp.h>
#include <linux/interrupt.h>
#include <linux/export.h>
#include <linux/cpu.h>

#include <asm/tlbflush.h>
#include <asm/mmu_context.h>
#include <asm/cache.h>
#include <asm/apic.h>
#include <asm/uv/uv.h>
#include <linux/debugfs.h>

/*
 *	Smarter SMP flushing macros.
 *		c/o Linus Torvalds.
 *
 *	These mean you can really definitely utterly forget about
 *	writing to user space from interrupts. (Its not allowed anyway).
 *
 *	Optimizations Manfred Spraul <manfred@colorfullife.com>
 *
 *	More scalable flush, from Andi Kleen
 *
 *	Implement flush IPI by CALL_FUNCTION_VECTOR, Alex Shi
 */

/* latr */
DEFINE_PER_CPU(struct tlbinfo, tlbinfo);

#ifdef CONFIG_LAZY_TLB_SHOOTDOWN
shootdown_entries_t __percpu *lazy_tlb_entries;
static struct cpumask cpu_none = {CPU_BITS_NONE};
#define TLB_SHOOTDOWN_MAX 2
#endif
/*******/

#ifdef CONFIG_SMP

/* latr */
#ifndef CONFIG_LAZY_TLB_SHOOTDOWN
struct flush_tlb_info {
	struct mm_struct *flush_mm;
	unsigned long flush_start;
	unsigned long flush_end;
};
#endif
/*******/

/*
 * We cannot call mmdrop() because we are in interrupt context,
 * instead update mm->cpu_vm_mask.
 */
void leave_mm(int cpu)
{
	struct mm_struct *active_mm = this_cpu_read(cpu_tlbstate.active_mm);
	if (this_cpu_read(cpu_tlbstate.state) == TLBSTATE_OK)
		BUG();
	if (cpumask_test_cpu(cpu, mm_cpumask(active_mm))) {
		cpumask_clear_cpu(cpu, mm_cpumask(active_mm));
		load_cr3(swapper_pg_dir);
		/*
		 * This gets called in the idle path where RCU
		 * functions differently.  Tracing normally
		 * uses RCU, so we have to call the tracepoint
		 * specially here.
		 */
		trace_tlb_flush_rcuidle(TLB_FLUSH_ON_TASK_SWITCH, TLB_FLUSH_ALL);
	}
}
EXPORT_SYMBOL_GPL(leave_mm);

#endif /* CONFIG_SMP */

void switch_mm(struct mm_struct *prev, struct mm_struct *next,
	       struct task_struct *tsk)
{
	unsigned long flags;

	local_irq_save(flags);
	switch_mm_irqs_off(prev, next, tsk);
	local_irq_restore(flags);
}

void switch_mm_irqs_off(struct mm_struct *prev, struct mm_struct *next,
			struct task_struct *tsk)
{
	unsigned cpu = smp_processor_id();

	if (likely(prev != next)) {
		if (IS_ENABLED(CONFIG_VMAP_STACK)) {
			/*
			 * If our current stack is in vmalloc space and isn't
			 * mapped in the new pgd, we'll double-fault.  Forcibly
			 * map it.
			 */
			unsigned int stack_pgd_index = pgd_index(current_stack_pointer());

			pgd_t *pgd = next->pgd + stack_pgd_index;

			if (unlikely(pgd_none(*pgd)))
				set_pgd(pgd, init_mm.pgd[stack_pgd_index]);
		}

#ifdef CONFIG_SMP
		this_cpu_write(cpu_tlbstate.state, TLBSTATE_OK);
		this_cpu_write(cpu_tlbstate.active_mm, next);
#endif

		cpumask_set_cpu(cpu, mm_cpumask(next));

		/*
		 * Re-load page tables.
		 *
		 * This logic has an ordering constraint:
		 *
		 *  CPU 0: Write to a PTE for 'next'
		 *  CPU 0: load bit 1 in mm_cpumask.  if nonzero, send IPI.
		 *  CPU 1: set bit 1 in next's mm_cpumask
		 *  CPU 1: load from the PTE that CPU 0 writes (implicit)
		 *
		 * We need to prevent an outcome in which CPU 1 observes
		 * the new PTE value and CPU 0 observes bit 1 clear in
		 * mm_cpumask.  (If that occurs, then the IPI will never
		 * be sent, and CPU 0's TLB will contain a stale entry.)
		 *
		 * The bad outcome can occur if either CPU's load is
		 * reordered before that CPU's store, so both CPUs must
		 * execute full barriers to prevent this from happening.
		 *
		 * Thus, switch_mm needs a full barrier between the
		 * store to mm_cpumask and any operation that could load
		 * from next->pgd.  TLB fills are special and can happen
		 * due to instruction fetches or for no reason at all,
		 * and neither LOCK nor MFENCE orders them.
		 * Fortunately, load_cr3() is serializing and gives the
		 * ordering guarantee we need.
		 *
		 */
		load_cr3(next->pgd);

		trace_tlb_flush(TLB_FLUSH_ON_TASK_SWITCH, TLB_FLUSH_ALL);

		/* Stop flush ipis for the previous mm */
		cpumask_clear_cpu(cpu, mm_cpumask(prev));

		/* Load per-mm CR4 state */
		load_mm_cr4(next);

#ifdef CONFIG_MODIFY_LDT_SYSCALL
		/*
		 * Load the LDT, if the LDT is different.
		 *
		 * It's possible that prev->context.ldt doesn't match
		 * the LDT register.  This can happen if leave_mm(prev)
		 * was called and then modify_ldt changed
		 * prev->context.ldt but suppressed an IPI to this CPU.
		 * In this case, prev->context.ldt != NULL, because we
		 * never set context.ldt to NULL while the mm still
		 * exists.  That means that next->context.ldt !=
		 * prev->context.ldt, because mms never share an LDT.
		 */
		if (unlikely(prev->context.ldt != next->context.ldt))
			load_mm_ldt(next);
#endif
	}
#ifdef CONFIG_SMP
	  else {
		this_cpu_write(cpu_tlbstate.state, TLBSTATE_OK);
		BUG_ON(this_cpu_read(cpu_tlbstate.active_mm) != next);

		if (!cpumask_test_cpu(cpu, mm_cpumask(next))) {
			/*
			 * On established mms, the mm_cpumask is only changed
			 * from irq context, from ptep_clear_flush() while in
			 * lazy tlb mode, and here. Irqs are blocked during
			 * schedule, protecting us from simultaneous changes.
			 */
			cpumask_set_cpu(cpu, mm_cpumask(next));

			/*
			 * We were in lazy tlb mode and leave_mm disabled
			 * tlb flush IPI delivery. We must reload CR3
			 * to make sure to use no freed page tables.
			 *
			 * As above, load_cr3() is serializing and orders TLB
			 * fills with respect to the mm_cpumask write.
			 */
			load_cr3(next->pgd);
			trace_tlb_flush(TLB_FLUSH_ON_TASK_SWITCH, TLB_FLUSH_ALL);
			load_mm_cr4(next);
			load_mm_ldt(next);
		}
	}
#endif
}

#ifdef CONFIG_SMP

/*
 * The flush IPI assumes that a thread switch happens in this order:
 * [cpu0: the cpu that switches]
 * 1) switch_mm() either 1a) or 1b)
 * 1a) thread switch to a different mm
 * 1a1) set cpu_tlbstate to TLBSTATE_OK
 *	Now the tlb flush NMI handler flush_tlb_func won't call leave_mm
 *	if cpu0 was in lazy tlb mode.
 * 1a2) update cpu active_mm
 *	Now cpu0 accepts tlb flushes for the new mm.
 * 1a3) cpu_set(cpu, new_mm->cpu_vm_mask);
 *	Now the other cpus will send tlb flush ipis.
 * 1a4) change cr3.
 * 1a5) cpu_clear(cpu, old_mm->cpu_vm_mask);
 *	Stop ipi delivery for the old mm. This is not synchronized with
 *	the other cpus, but flush_tlb_func ignore flush ipis for the wrong
 *	mm, and in the worst case we perform a superfluous tlb flush.
 * 1b) thread switch without mm change
 *	cpu active_mm is correct, cpu0 already handles flush ipis.
 * 1b1) set cpu_tlbstate to TLBSTATE_OK
 * 1b2) test_and_set the cpu bit in cpu_vm_mask.
 *	Atomically set the bit [other cpus will start sending flush ipis],
 *	and test the bit.
 * 1b3) if the bit was 0: leave_mm was called, flush the tlb.
 * 2) switch %%esp, ie current
 *
 * The interrupt must handle 2 special cases:
 * - cr3 is changed before %%esp, ie. it cannot use current->{active_,}mm.
 * - the cpu performs speculative tlb reads, i.e. even if the cpu only
 *   runs in kernel space, the cpu could load tlb entries for user space
 *   pages.
 *
 * The good news is that cpu_tlbstate is local to each cpu, no
 * write/read ordering problems.
 */

/*
 * TLB flush funcation:
 * 1) Flush the tlb entries if the cpu uses the mm that's being flushed.
 * 2) Leave the mm if we are in the lazy tlb mode.
 */
static void flush_tlb_func(void *info)
{
	struct flush_tlb_info *f = info;

	inc_irq_stat(irq_tlb_count);

	if (f->flush_mm && f->flush_mm != this_cpu_read(cpu_tlbstate.active_mm))
		return;

	count_vm_tlb_event(NR_TLB_REMOTE_FLUSH_RECEIVED);
	if (this_cpu_read(cpu_tlbstate.state) == TLBSTATE_OK) {
		if (f->flush_end == TLB_FLUSH_ALL) {
			local_flush_tlb();
			trace_tlb_flush(TLB_REMOTE_SHOOTDOWN, TLB_FLUSH_ALL);
		} else {
			unsigned long addr;
			unsigned long nr_pages =
				(f->flush_end - f->flush_start) / PAGE_SIZE;
			addr = f->flush_start;
			while (addr < f->flush_end) {
				__flush_tlb_single(addr);
				addr += PAGE_SIZE;
			}
			trace_tlb_flush(TLB_REMOTE_SHOOTDOWN, nr_pages);
		}
	} else
		leave_mm(smp_processor_id());

}

/* latr */
#ifdef CONFIG_LAZY_TLB_SHOOTDOWN

void native_flush_tlb_fallback(const struct cpumask *cpumask,
			struct mm_struct *mm, unsigned long start,
			unsigned long end)
{
	struct flush_tlb_info info;

	if (end == 0)
		end = start + PAGE_SIZE;
	info.flush_mm = mm;
	info.flush_start = start;
	info.flush_end = end;

	count_vm_tlb_event(NR_TLB_REMOTE_FLUSH);
        this_cpu_inc(tlbinfo.remote_tlbs.num_tlbs);
	if (end == TLB_FLUSH_ALL)
		trace_tlb_flush(TLB_REMOTE_SEND_IPI, TLB_FLUSH_ALL);
	else
		trace_tlb_flush(TLB_REMOTE_SEND_IPI,
				(end - start) >> PAGE_SHIFT);

	if (is_uv_system()) {
		unsigned int cpu;

		cpu = smp_processor_id();
		cpumask = uv_flush_tlb_others(cpumask, mm, start, end, cpu);
		if (cpumask)
			smp_call_function_many(cpumask, flush_tlb_func,
					&info, 1);
		return;
	}

	smp_call_function_many(cpumask, flush_tlb_func, &info, 1);
}

// per cpu background thread handler - check cpu-mask and
// invalidate entries
void background_update_saved_states(int cpu)
{
	unsigned int i;
	shootdown_entries_t *cpu_entry = NULL;
	lazytlb_shootdown_t *state;

	cpu_entry = this_cpu_ptr(lazy_tlb_entries);

	for (i = 0; i < LAZY_TLB_NUM_ENTRIES; i++) {
		state = &cpu_entry->entries[i];
		if (atomic_read(&state->valid)) {
			if (cpumask_equal(&state->cpu_mask, &cpu_none)) {
				atomic_set(&state->valid, 0);
			}

			/* After MAX tries fallback to sending IPI */
			if (atomic_read(&state->valid)) {
				++state->refs;
				if (state->refs >= TLB_SHOOTDOWN_MAX) {
					/* reset valid to prevent flushes */
					atomic_set(&state->valid, 0);
					native_flush_tlb_fallback(&state->cpu_mask,
								state->flush_mm,
								state->flush_start,
								state->flush_end);
				}
			}
		}
	}
}

#ifdef CONFIG_LAZY_MEM_FREE
static int __process_lazy_vma(struct mm_struct *mm, int force)
{
	struct vm_area_struct *vma = mm->lazy_mmap, *next;
	int pending = 0;

        while (vma) {
		next = vma->lazy_vmnext;
		/* only free the entries after 2 HZ (1 extra buffer) */
		if (((get_jiffies_64() - vma->lazy_jiffy) > 2*HZ) || force) {
			/* lazy_detach_vmas_to_be_unmapped(mm, vma); */
			/* Now lets free the VMAs */
			lazy_remove_vma_list(mm, vma);
		} else {
			++pending;
		}
		vma = next;
	}
	return (!pending);
}

static int __process_lazy_pages(struct mm_struct *mm, int force)
{
	struct lazy_page_list *lpages, *tmp;
	int pending = 0;

	list_for_each_entry_safe(lpages, tmp, &mm->lazy_page_list_head,
				page_node) {
		if (((get_jiffies_64() - lpages->lazy_jiffy) > 2*HZ) || force) {
			/* free pages */
			list_del(&lpages->page_node);
			lazy_free_pages(mm, lpages);
#ifdef CONFIG_LAZY_MEM_FREE_DEBUG
			if (force) {
				lazy_free_pages(mm, lpages);
			} else {
				printk("Deleting %lld number of pages \n",lpages->nr);
				for (i=0; i<10; i++)
					printk("%d - %p \n",i,lpages->pages[i]);
			}
#endif
		} else {
			++pending;
		}
	}
	return (!pending);
}

int __process_lazy_mm(struct mm_struct *mm, int force)
{
	int vret=0, pret=0;

	/*
	 * Remove lazy VMA list
	 */
	vret = __process_lazy_vma(mm, force);

	/*
	 * Remove pages
	 */
	pret = __process_lazy_pages(mm, force);
	return !(vret|pret);
}
EXPORT_SYMBOL_GPL(__process_lazy_mm);

static int process_lazy_mm(struct mm_struct *mm, int force)
{
	int ret = 1;
	if (down_write_killable(&mm->mmap_sem)) {
		goto done;
	}
	if (!atomic_read(&mm->in_lazy_list)) {
		goto done;
	}
	ret = __process_lazy_mm(mm, force);
	up_write(&mm->mmap_sem);
done:
	return ret;
}
#endif

#ifdef CONFIG_LAZY_MEM_FREE
static int background_lazy_memory(void *__unused)
{
	int done=0;
	struct mm_struct *mm = NULL, *tmp = NULL;

	while (!kthread_should_stop()) {
		schedule_timeout_interruptible(HZ*1);
		// Process the VMAs and pages in each MM.
		spin_lock(&lazy_mm_lock);
		list_for_each_entry_safe(mm, tmp, &lazy_mm_list,
					lazy_mm_list) {
			spin_unlock(&lazy_mm_lock);
			done = process_lazy_mm(mm, 0);
			spin_lock(&lazy_mm_lock);
			if (done) {
				list_del(&mm->lazy_mm_list);
				/* Mark the entry as not present */
				atomic_set(&mm->in_lazy_list, 0);
			}
		}
		spin_unlock(&lazy_mm_lock);
		/* printk("background lazy memory thread running \n"); */
	}
	return 0;
}
#endif

/* High iter value, reduce if needed */
static int background_tlb_invalidate(void *__unused)
{
	int cpu;

	printk("TLB flush background thread started \n");
	while (!kthread_should_stop()) {
		schedule_timeout_interruptible(HZ*1);
		/* printk("TLB flush background thread running \n"); */
		for_each_online_cpu(cpu) {
			background_update_saved_states(cpu);
		}

	}
	return 0;
}

static uint32_t get_slot(const uint64_t counter) {
  return counter % LAZY_TLB_NUM_ENTRIES;
}

// Function to flush all the saved states

void native_flush_saved_states(int cpu_id, int tick)
{
	unsigned int cpu;
	shootdown_entries_t *cpu_entry;
	lazytlb_shootdown_t *state;
	struct flush_tlb_info info;

	/* TODO: This should be modified to cores in a socket */
	for_each_online_cpu(cpu) {
		cpu_entry = per_cpu_ptr(lazy_tlb_entries, cpu);

        // Store current entry, starting with the start, incrementing towards
        // end.
        uint64_t end = cpu_entry->end;

        uint64_t current_entry = cpu_entry->start;
        uint32_t local_counter = 0;

        while(current_entry != end && local_counter <= LAZY_TLB_NUM_ENTRIES) {
            smp_rmb();
            uint32_t current_slot = get_slot(current_entry);

            state = &cpu_entry->entries[current_slot];
	    if (!atomic_read(&state->valid)) {
                goto loop_end;
            }

			if (cpumask_test_cpu(cpu_id, &state->cpu_mask)) {
#ifdef CONFIG_LAZY_MIGRATION
				// check to use task_work_add
				if (atomic_read(&state->flags)) {
					if (!tick)
						continue;
					if (cpumask_equal(&state->cpu_mask,
							  cpu_online_mask)) {
						change_prot_numa(
							(struct vm_area_struct*)state->flush_mm,
							state->flush_start,
							state->flush_end, 2);
					}
					info.flush_mm = ((struct vm_area_struct*)state->flush_mm)->vm_mm;
				} else {
					info.flush_mm = state->flush_mm;
				}
#else
				info.flush_mm = state->flush_mm;
#endif
				info.flush_start = state->flush_start;
				info.flush_end = state->flush_end;

				// Flush only the local TLB
				flush_tlb_func(&info);

				// Clear the CPU mask
				cpumask_clear_cpu(cpu_id, &state->cpu_mask);

                // Invalidate current entry, potentially incrementing start
                // until the next valid entry (or end) is reached.
				if (cpumask_equal(&state->cpu_mask, &cpu_none)) {
                    // First, invalidate current entry.
#ifdef CONFIG_LAZY_MIGRATION
					atomic_set(&state->flags, 0);
#endif
					atomic_set(&state->valid, 0);
				}
			}
loop_end:
            ++current_entry;
            ++local_counter;
            if(local_counter > LAZY_TLB_NUM_ENTRIES) {
                printk("BUG BUG BUG, more than LAZY_TLB_BUM_ENTRIES traversed, %llu, %llu, %llu!\n", cpu_entry->start, current_entry, end);
            }
        }
	}

    // Skip over all invalid entries until reaching a valid one or the end
    // poiner for the local list.
    // NOTE: This is currently not synchronized with other CPUs, an invalid
    // entry might only get cleared after two rounds.

	cpu_entry = this_cpu_ptr(lazy_tlb_entries);
    // Store current entry, starting with the start, incrementing towards
    // end.
    uint64_t end = cpu_entry->end;
    uint64_t current_entry = cpu_entry->start;
    uint32_t local_counter = 0;

    state = &cpu_entry->entries[get_slot(current_entry)];

    // Iterate invalid entries until either reaching the end or a still valid
    // entry.
    while(!atomic_read(&state->valid) && current_entry != end) {
        // Advance start by one and advance current_entry
        ++cpu_entry->start;
        ++current_entry;
        ++local_counter;
        state = &cpu_entry->entries[get_slot(current_entry)];
        if(local_counter > LAZY_TLB_NUM_ENTRIES) {
            printk("BUG BUG BUG, more than LAZY_TLB_BUM_ENTRIES traversed while reclaiming, %llu, %llu, %llu!\n", cpu_entry->start, current_entry, end);
            smp_wmb();
            return;
        }
    }
    // Publish changes to start.
    smp_wmb();
}

// Function to save the states

int native_flush_save_state(struct flush_tlb_info *info,
			const struct cpumask *cpumask,
			int migration)
{
	shootdown_entries_t *cpu_entry;
	lazytlb_shootdown_t *state;
	uint32_t entry;
    int valid;

    // Be sure to get up-to-date information.
    smp_rmb();
	cpu_entry = this_cpu_ptr(lazy_tlb_entries);

    // Check if there are any free entries.
    // This means that start and end + 1 would fall on the same slot.
    if(get_slot(cpu_entry->start) == get_slot(cpu_entry->end + 1)) {
        return 0;
    }

	// identify free entry
	entry = get_slot(cpu_entry->end);
	state = &cpu_entry->entries[entry];
	valid = atomic_read(&state->valid);
    // In case the next entry is still valid, i.e. it is not flushed yet,
    // return false and fall back.
	if (valid) {
        printk("BUG BUG BUG, entry should have been invalid but it is not, %llu, %llu!\n", cpu_entry->start, cpu_entry->end);
        return 0;
	}

	// Update local TLB state
	// atomic_set(&state->valid, 0);
	state->flush_mm = info->flush_mm;
	state->flush_start = info->flush_start;
	state->flush_end = info->flush_end;
	state->cpu_mask = *cpumask;
	state->refs = 0;
#ifdef CONFIG_LAZY_MIGRATION
	if (migration) {
		state->flags.counter = 1;
	} else {
		state->flags.counter = 0;
	}
#endif

	// write barrier. increment count only after the
	// data values are updated, to avoid instruction
	// reordering.
	smp_wmb();
	atomic_set(&state->valid, 1);
    ++cpu_entry->end;
	smp_wmb();

	return 1;
}
EXPORT_SYMBOL_GPL(native_flush_save_state);

#endif
/*******/

void native_flush_tlb_others(const struct cpumask *cpumask,
				 struct mm_struct *mm, unsigned long start,
				 unsigned long end)
{
	struct flush_tlb_info info;
/* latr */
        u64 time;
#ifdef CONFIG_LAZY_TLB_SHOOTDOWN
	int ret=0;
#endif

        time = rdtsc();
/*******/
	if (end == 0)
		end = start + PAGE_SIZE;
	info.flush_mm = mm;
	info.flush_start = start;
	info.flush_end = end;

	count_vm_tlb_event(NR_TLB_REMOTE_FLUSH);
/* latr */
        this_cpu_inc(tlbinfo.remote_tlbs.num_tlbs);
/*******/
	if (end == TLB_FLUSH_ALL)
		trace_tlb_flush(TLB_REMOTE_SEND_IPI, TLB_FLUSH_ALL);
	else
		trace_tlb_flush(TLB_REMOTE_SEND_IPI,
				(end - start) >> PAGE_SHIFT);

	if (is_uv_system()) {
		unsigned int cpu;

		cpu = smp_processor_id();
		cpumask = uv_flush_tlb_others(cpumask, mm, start, end, cpu);
		if (cpumask)
			smp_call_function_many(cpumask, flush_tlb_func,
								&info, 1);
		return;
	}

/* latr */
#ifdef CONFIG_LAZY_TLB_SHOOTDOWN
	// Flush the local TLB and save state only for unmap operation
	if ((mm) && (atomic_read(&mm->munmap_inprogress))) {
		flush_tlb_func(&info);
		ret = native_flush_save_state(&info, cpumask, 0);
		if(!ret) {
			this_cpu_inc(tlbinfo.remote_tlbs.fallback_ipi);
		}
	}

	if (!ret) {
		/* No free entries available, fall back */
		smp_call_function_many(cpumask, flush_tlb_func, &info, 1);
		this_cpu_inc(tlbinfo.remote_tlbs.count_ipi);
        }
                update_tlbinfo_stats(&this_cpu_ptr(&tlbinfo)->remote_tlbs,
                                     rdtsc_ordered() - time);
#else
	smp_call_function_many(cpumask, flush_tlb_func, &info, 1);
        update_tlbinfo_stats(&this_cpu_ptr(&tlbinfo)->remote_tlbs,
                             rdtsc_ordered() - time);
    this_cpu_inc(tlbinfo.remote_tlbs.count_ipi);
#endif
/*******/
}

void flush_tlb_current_task(void)
{
	struct mm_struct *mm = current->mm;

	preempt_disable();

	count_vm_tlb_event(NR_TLB_LOCAL_FLUSH_ALL);

	/* This is an implicit full barrier that synchronizes with switch_mm. */
	local_flush_tlb();

	trace_tlb_flush(TLB_LOCAL_SHOOTDOWN, TLB_FLUSH_ALL);
	if (cpumask_any_but(mm_cpumask(mm), smp_processor_id()) < nr_cpu_ids)
		flush_tlb_others(mm_cpumask(mm), mm, 0UL, TLB_FLUSH_ALL);
	preempt_enable();
}

/*
 * See Documentation/x86/tlb.txt for details.  We choose 33
 * because it is large enough to cover the vast majority (at
 * least 95%) of allocations, and is small enough that we are
 * confident it will not cause too much overhead.  Each single
 * flush is about 100 ns, so this caps the maximum overhead at
 * _about_ 3,000 ns.
 *
 * This is in units of pages.
 */
static unsigned long tlb_single_page_flush_ceiling __read_mostly = 33;

void flush_tlb_mm_range(struct mm_struct *mm, unsigned long start,
				unsigned long end, unsigned long vmflag)
{
	unsigned long addr;
/* latr */
	u64 time;
/*******/
	/* do a global flush by default */
	unsigned long base_pages_to_flush = TLB_FLUSH_ALL;

	preempt_disable();
/* latr */
	time = rdtsc();
/*******/
	if (current->active_mm != mm) {
		/* Synchronize with switch_mm. */
		smp_mb();

		goto out;
	}

	if (!current->mm) {
		leave_mm(smp_processor_id());

		/* Synchronize with switch_mm. */
		smp_mb();

		goto out;
	}

	if ((end != TLB_FLUSH_ALL) && !(vmflag & VM_HUGETLB))
		base_pages_to_flush = (end - start) >> PAGE_SHIFT;

	/*
	 * Both branches below are implicit full barriers (MOV to CR or
	 * INVLPG) that synchronize with switch_mm.
	 */
	if (base_pages_to_flush > tlb_single_page_flush_ceiling) {
		base_pages_to_flush = TLB_FLUSH_ALL;
		count_vm_tlb_event(NR_TLB_LOCAL_FLUSH_ALL);
		local_flush_tlb();
	} else {
		/* flush range by one by one 'invlpg' */
		for (addr = start; addr < end;	addr += PAGE_SIZE) {
			count_vm_tlb_event(NR_TLB_LOCAL_FLUSH_ONE);
			__flush_tlb_single(addr);
		}
	}
	trace_tlb_flush(TLB_LOCAL_MM_SHOOTDOWN, base_pages_to_flush);
/* latr */
	this_cpu_inc(tlbinfo.inv_tlbs.num_tlbs);
	update_tlbinfo_stats(&this_cpu_ptr(&tlbinfo)->inv_tlbs,
			rdtsc_ordered() - time);
/*******/
out:
	if (base_pages_to_flush == TLB_FLUSH_ALL) {
		start = 0UL;
		end = TLB_FLUSH_ALL;
	}
	if (cpumask_any_but(mm_cpumask(mm), smp_processor_id()) < nr_cpu_ids)
		flush_tlb_others(mm_cpumask(mm), mm, start, end);
	preempt_enable();
}

void flush_tlb_page(struct vm_area_struct *vma, unsigned long start)
{
	struct mm_struct *mm = vma->vm_mm;

	preempt_disable();

	if (current->active_mm == mm) {
		if (current->mm) {
			/*
			 * Implicit full barrier (INVLPG) that synchronizes
			 * with switch_mm.
			 */
			__flush_tlb_one(start);
		} else {
			leave_mm(smp_processor_id());

			/* Synchronize with switch_mm. */
			smp_mb();
		}
	}

	if (cpumask_any_but(mm_cpumask(mm), smp_processor_id()) < nr_cpu_ids)
		flush_tlb_others(mm_cpumask(mm), mm, start, 0UL);

	preempt_enable();
}

static void do_flush_tlb_all(void *info)
{
	count_vm_tlb_event(NR_TLB_REMOTE_FLUSH_RECEIVED);
	__flush_tlb_all();
	if (this_cpu_read(cpu_tlbstate.state) == TLBSTATE_LAZY)
		leave_mm(smp_processor_id());
}

void flush_tlb_all(void)
{
	count_vm_tlb_event(NR_TLB_REMOTE_FLUSH);
	on_each_cpu(do_flush_tlb_all, NULL, 1);
}

static void do_kernel_range_flush(void *info)
{
	struct flush_tlb_info *f = info;
	unsigned long addr;

	/* flush range by one by one 'invlpg' */
	for (addr = f->flush_start; addr < f->flush_end; addr += PAGE_SIZE)
		__flush_tlb_single(addr);
}

void flush_tlb_kernel_range(unsigned long start, unsigned long end)
{

	/* Balance as user space task's flush, a bit conservative */
	if (end == TLB_FLUSH_ALL ||
	    (end - start) > tlb_single_page_flush_ceiling * PAGE_SIZE) {
		on_each_cpu(do_flush_tlb_all, NULL, 1);
	} else {
		struct flush_tlb_info info;
		info.flush_start = start;
		info.flush_end = end;
		on_each_cpu(do_kernel_range_flush, &info, 1);
	}
}

static ssize_t tlbflush_read_file(struct file *file, char __user *user_buf,
			     size_t count, loff_t *ppos)
{
	char buf[32];
	unsigned int len;

	len = sprintf(buf, "%ld\n", tlb_single_page_flush_ceiling);
	return simple_read_from_buffer(user_buf, count, ppos, buf, len);
}

static ssize_t tlbflush_write_file(struct file *file,
		 const char __user *user_buf, size_t count, loff_t *ppos)
{
	char buf[32];
	ssize_t len;
	int ceiling;

	len = min(count, sizeof(buf) - 1);
	if (copy_from_user(buf, user_buf, len))
		return -EFAULT;

	buf[len] = '\0';
	if (kstrtoint(buf, 0, &ceiling))
		return -EINVAL;

	if (ceiling < 0)
		return -EINVAL;

	tlb_single_page_flush_ceiling = ceiling;
	return count;
}

static const struct file_operations fops_tlbflush = {
	.read = tlbflush_read_file,
	.write = tlbflush_write_file,
	.llseek = default_llseek,
};

static int __init create_tlb_single_page_flush_ceiling(void)
{
/* latr */
#ifdef CONFIG_LAZY_TLB_SHOOTDOWN
	int align = 32;
	struct task_struct *tlb_task;

	 /* create per-core TLB memory to store state */
	lazy_tlb_entries = __alloc_percpu(sizeof(shootdown_entries_t),
					align);

	shootdown_entries_t *cpu_entry;
	lazytlb_shootdown_t *state;
    int i;
	unsigned int cpu;
	for_each_possible_cpu(cpu) {
		cpu_entry = per_cpu_ptr(lazy_tlb_entries, cpu);
        cpu_entry->start = 0;
        cpu_entry->end = 0;
        for (i = 0; i < LAZY_TLB_NUM_ENTRIES; ++i) {
            state = &cpu_entry->entries[i];
            atomic_set(&state->valid, 0);
        }
    }

	 /* Current one kthread. TODO: per-core or per-node kthread */
	tlb_task = kthread_create(background_tlb_invalidate,
			NULL, "TLB flush");
	if (IS_ERR(tlb_task)) {
		printk("TLB flush kernel thread not created\n");
	}
	wake_up_process(tlb_task);
#endif

#ifdef CONFIG_LAZY_MEM_FREE
	struct task_struct *mem_task;

	/* kthread to handle memory reclamation*/
	mem_task = kthread_create(background_lazy_memory,
			NULL, "Memory reclam");
	if (IS_ERR(mem_task)) {
		printk("Memory reclamation kernel thread not created\n");
	}
	wake_up_process(mem_task);
#endif

	// create the per-core TLB flush scratchpad memory needed
/*******/

	debugfs_create_file("tlb_single_page_flush_ceiling", S_IRUSR | S_IWUSR,
			    arch_debugfs_dir, NULL, &fops_tlbflush);
	return 0;
}
late_initcall(create_tlb_single_page_flush_ceiling);

#endif /* CONFIG_SMP */
