/*
 * Common interrupt code for 32 and 64 bit
 */
#include <linux/cpu.h>
#include <linux/interrupt.h>
#include <linux/kernel_stat.h>
#include <linux/of.h>
#include <linux/seq_file.h>
#include <linux/smp.h>
#include <linux/ftrace.h>
#include <linux/delay.h>
#include <linux/export.h>

#include <asm/apic.h>
#include <asm/io_apic.h>
#include <asm/irq.h>
#include <asm/idle.h>
#include <asm/mce.h>
#include <asm/hw_irq.h>

#define CREATE_TRACE_POINTS
#include <asm/trace/irq_vectors.h>

atomic_t irq_err_count;

/* Function pointer for generic interrupt vector handling */
void (*x86_platform_ipi_callback)(void) = NULL;

/*
 * 'what should we do if we get a hw irq event on an illegal vector'.
 * each architecture has to answer this themselves.
 */
void ack_bad_irq(unsigned int irq)
{
	if (printk_ratelimit())
		pr_err("unexpected IRQ trap at vector %02x\n", irq);

	/*
	 * Currently unexpected vectors happen only on SMP and APIC.
	 * We _must_ ack these because every local APIC has only N
	 * irq slots per priority level, and a 'hanging, unacked' IRQ
	 * holds up an irq slot - in excessive cases (when multiple
	 * unexpected vectors occur) that might lock up the APIC
	 * completely.
	 * But only ack when the APIC is enabled -AK
	 */
	ack_APIC_irq();
}

#define irq_stats(x)		(&per_cpu(irq_stat, x))
/*
 * /proc/interrupts printing for arch specific interrupts
 */
int arch_show_interrupts(struct seq_file *p, int prec)
{
	int j;

	seq_printf(p, "%*s: ", prec, "NMI");
	for_each_online_cpu(j)
		seq_printf(p, "%10u ", irq_stats(j)->__nmi_count);
	seq_printf(p, "  Non-maskable interrupts\n");
#ifdef CONFIG_X86_LOCAL_APIC
	seq_printf(p, "%*s: ", prec, "LOC");
	for_each_online_cpu(j)
		seq_printf(p, "%10u ", irq_stats(j)->apic_timer_irqs);
	seq_printf(p, "  Local timer interrupts\n");

	seq_printf(p, "%*s: ", prec, "SPU");
	for_each_online_cpu(j)
		seq_printf(p, "%10u ", irq_stats(j)->irq_spurious_count);
	seq_printf(p, "  Spurious interrupts\n");
	seq_printf(p, "%*s: ", prec, "PMI");
	for_each_online_cpu(j)
		seq_printf(p, "%10u ", irq_stats(j)->apic_perf_irqs);
	seq_printf(p, "  Performance monitoring interrupts\n");
	seq_printf(p, "%*s: ", prec, "IWI");
	for_each_online_cpu(j)
		seq_printf(p, "%10u ", irq_stats(j)->apic_irq_work_irqs);
	seq_printf(p, "  IRQ work interrupts\n");
	seq_printf(p, "%*s: ", prec, "RTR");
	for_each_online_cpu(j)
		seq_printf(p, "%10u ", irq_stats(j)->icr_read_retry_count);
	seq_printf(p, "  APIC ICR read retries\n");
#endif
	if (x86_platform_ipi_callback) {
		seq_printf(p, "%*s: ", prec, "PLT");
		for_each_online_cpu(j)
			seq_printf(p, "%10u ", irq_stats(j)->x86_platform_ipis);
		seq_printf(p, "  Platform interrupts\n");
	}
#ifdef CONFIG_SMP
	seq_printf(p, "%*s: ", prec, "RES");
	for_each_online_cpu(j)
		seq_printf(p, "%10u ", irq_stats(j)->irq_resched_count);
	seq_printf(p, "  Rescheduling interrupts\n");
	seq_printf(p, "%*s: ", prec, "CAL");
	for_each_online_cpu(j)
		seq_printf(p, "%10u ", irq_stats(j)->irq_call_count -
					irq_stats(j)->irq_tlb_count);
	seq_printf(p, "  Function call interrupts\n");
	seq_printf(p, "%*s: ", prec, "TLB");
	for_each_online_cpu(j)
		seq_printf(p, "%10u ", irq_stats(j)->irq_tlb_count);
	seq_printf(p, "  TLB shootdowns\n");
#endif
#ifdef CONFIG_X86_THERMAL_VECTOR
	seq_printf(p, "%*s: ", prec, "TRM");
	for_each_online_cpu(j)
		seq_printf(p, "%10u ", irq_stats(j)->irq_thermal_count);
	seq_printf(p, "  Thermal event interrupts\n");
#endif
#ifdef CONFIG_X86_MCE_THRESHOLD
	seq_printf(p, "%*s: ", prec, "THR");
	for_each_online_cpu(j)
		seq_printf(p, "%10u ", irq_stats(j)->irq_threshold_count);
	seq_printf(p, "  Threshold APIC interrupts\n");
#endif
#ifdef CONFIG_X86_MCE
	seq_printf(p, "%*s: ", prec, "MCE");
	for_each_online_cpu(j)
		seq_printf(p, "%10u ", per_cpu(mce_exception_count, j));
	seq_printf(p, "  Machine check exceptions\n");
	seq_printf(p, "%*s: ", prec, "MCP");
	for_each_online_cpu(j)
		seq_printf(p, "%10u ", per_cpu(mce_poll_count, j));
	seq_printf(p, "  Machine check polls\n");
#endif
	seq_printf(p, "%*s: %10u\n", prec, "ERR", atomic_read(&irq_err_count));
#if defined(CONFIG_X86_IO_APIC)
	seq_printf(p, "%*s: %10u\n", prec, "MIS", atomic_read(&irq_mis_count));
#endif
	return 0;
}

/*
 * /proc/stat helpers
 */
u64 arch_irq_stat_cpu(unsigned int cpu)
{
	u64 sum = irq_stats(cpu)->__nmi_count;

#ifdef CONFIG_X86_LOCAL_APIC
	sum += irq_stats(cpu)->apic_timer_irqs;
	sum += irq_stats(cpu)->irq_spurious_count;
	sum += irq_stats(cpu)->apic_perf_irqs;
	sum += irq_stats(cpu)->apic_irq_work_irqs;
	sum += irq_stats(cpu)->icr_read_retry_count;
#endif
	if (x86_platform_ipi_callback)
		sum += irq_stats(cpu)->x86_platform_ipis;
#ifdef CONFIG_SMP
	sum += irq_stats(cpu)->irq_resched_count;
	sum += irq_stats(cpu)->irq_call_count;
#endif
#ifdef CONFIG_X86_THERMAL_VECTOR
	sum += irq_stats(cpu)->irq_thermal_count;
#endif
#ifdef CONFIG_X86_MCE_THRESHOLD
	sum += irq_stats(cpu)->irq_threshold_count;
#endif
#ifdef CONFIG_X86_MCE
	sum += per_cpu(mce_exception_count, cpu);
	sum += per_cpu(mce_poll_count, cpu);
#endif
	return sum;
}

u64 arch_irq_stat(void)
{
	u64 sum = atomic_read(&irq_err_count);
	return sum;
}


/*
 * do_IRQ handles all normal device IRQ's (the special
 * SMP cross-CPU interrupts have their own specific
 * handlers).
 */
unsigned int __irq_entry do_IRQ(struct pt_regs *regs)
{
	struct pt_regs *old_regs = set_irq_regs(regs);

	/* high bit used in ret_from_ code  */
	unsigned vector = ~regs->orig_ax;
	unsigned irq;

	if (test_irq_softdisable(regs, do_IRQ))
		return 1;

	irq_enter();
	exit_idle();

	irq = __this_cpu_read(vector_irq[vector]);

	if (!handle_irq(irq, regs)) {
		ack_APIC_irq();

		if (printk_ratelimit())
			pr_emerg("%s: %d.%d No irq handler for vector (irq %d)\n",
				__func__, smp_processor_id(), vector, irq);
	}

	irq_exit();

	set_irq_regs(old_regs);
	return 1;
}

/*
 * Handler for X86_PLATFORM_IPI_VECTOR.
 */
void __smp_x86_platform_ipi(void)
{
	inc_irq_stat(x86_platform_ipis);

	if (x86_platform_ipi_callback)
		x86_platform_ipi_callback();
}

void smp_x86_platform_ipi(struct pt_regs *regs)
{
	struct pt_regs *old_regs = set_irq_regs(regs);

	entering_ack_irq();
	__smp_x86_platform_ipi();
	exiting_irq();
	set_irq_regs(old_regs);
}

#ifdef CONFIG_HAVE_KVM
/*
 * Handler for POSTED_INTERRUPT_VECTOR.
 */
void smp_kvm_posted_intr_ipi(struct pt_regs *regs)
{
	struct pt_regs *old_regs = set_irq_regs(regs);

	ack_APIC_irq();

	irq_enter();

	exit_idle();

	inc_irq_stat(kvm_posted_intr_ipis);

	irq_exit();

	set_irq_regs(old_regs);
}
#endif

void smp_trace_x86_platform_ipi(struct pt_regs *regs)
{
	struct pt_regs *old_regs = set_irq_regs(regs);

	entering_ack_irq();
	trace_x86_platform_ipi_entry(X86_PLATFORM_IPI_VECTOR);
	__smp_x86_platform_ipi();
	trace_x86_platform_ipi_exit(X86_PLATFORM_IPI_VECTOR);
	exiting_irq();
	set_irq_regs(old_regs);
}

EXPORT_SYMBOL_GPL(vector_used_by_percpu_irq);

#ifdef CONFIG_HOTPLUG_CPU
/* A cpu has been removed from cpu_online_mask.  Reset irq affinities. */
void fixup_irqs(void)
{
	unsigned int irq, vector;
	static int warned;
	struct irq_desc *desc;
	struct irq_data *data;
	struct irq_chip *chip;

	for_each_irq_desc(irq, desc) {
		int break_affinity = 0;
		int set_affinity = 1;
		const struct cpumask *affinity;

		if (!desc)
			continue;
		if (irq == 2)
			continue;

		/* interrupt's are disabled at this point */
		raw_spin_lock(&desc->lock);

		data = irq_desc_get_irq_data(desc);
		affinity = data->affinity;
		if (!irq_has_action(irq) || irqd_is_per_cpu(data) ||
		    cpumask_subset(affinity, cpu_online_mask)) {
			raw_spin_unlock(&desc->lock);
			continue;
		}

		/*
		 * Complete the irq move. This cpu is going down and for
		 * non intr-remapping case, we can't wait till this interrupt
		 * arrives at this cpu before completing the irq move.
		 */
		irq_force_complete_move(irq);

		if (cpumask_any_and(affinity, cpu_online_mask) >= nr_cpu_ids) {
			break_affinity = 1;
			affinity = cpu_online_mask;
		}

		chip = irq_data_get_irq_chip(data);
		if (!irqd_can_move_in_process_context(data) && chip->irq_mask)
			chip->irq_mask(data);

		if (chip->irq_set_affinity)
			chip->irq_set_affinity(data, affinity, true);
		else if (!(warned++))
			set_affinity = 0;

		/*
		 * We unmask if the irq was not marked masked by the
		 * core code. That respects the lazy irq disable
		 * behaviour.
		 */
		if (!irqd_can_move_in_process_context(data) &&
		    !irqd_irq_masked(data) && chip->irq_unmask)
			chip->irq_unmask(data);

		raw_spin_unlock(&desc->lock);

		if (break_affinity && set_affinity)
			pr_notice("Broke affinity for irq %i\n", irq);
		else if (!set_affinity)
			pr_notice("Cannot set affinity for irq %i\n", irq);
	}

	/*
	 * We can remove mdelay() and then send spuriuous interrupts to
	 * new cpu targets for all the irqs that were handled previously by
	 * this cpu. While it works, I have seen spurious interrupt messages
	 * (nothing wrong but still...).
	 *
	 * So for now, retain mdelay(1) and check the IRR and then send those
	 * interrupts to new targets as this cpu is already offlined...
	 */
	mdelay(1);

	for (vector = FIRST_EXTERNAL_VECTOR; vector < NR_VECTORS; vector++) {
		unsigned int irr;

		if (__this_cpu_read(vector_irq[vector]) < 0)
			continue;

		irr = apic_read(APIC_IRR + (vector / 32 * 0x10));
		if (irr  & (1 << (vector % 32))) {
			irq = __this_cpu_read(vector_irq[vector]);

			desc = irq_to_desc(irq);
			data = irq_desc_get_irq_data(desc);
			chip = irq_data_get_irq_chip(data);
			raw_spin_lock(&desc->lock);
			if (chip->irq_retrigger)
				chip->irq_retrigger(data);
			raw_spin_unlock(&desc->lock);
		}
		__this_cpu_write(vector_irq[vector], -1);
	}
}
#endif

#ifdef CONFIG_IRQ_SOFT_DISABLE
#include <linux/percpu.h>
#include <asm/local.h>

/* Start out with interrupts disabled */
static DEFINE_PER_CPU(local_t, is_native_irq_disabled) = LOCAL_INIT(X86_EFLAGS_IF);
static DEFINE_PER_CPU(void *, native_trigger_func);
static DEFINE_PER_CPU(unsigned long, native_orig_ax);

static DEFINE_PER_CPU(local_t, debug_count);
static DEFINE_PER_CPU(void *, last_irq_disabled);
static DEFINE_PER_CPU(void *, last_irq_enabled);
static DEFINE_PER_CPU(void *, last_trigger);
static DEFINE_PER_CPU(long, last_irq_disabled_cnt);
static DEFINE_PER_CPU(long, last_irq_enabled_cnt);
static DEFINE_PER_CPU(long, last_trigger_cnt);

#define UPDATE_NATIVE(ptr, val)						\
	do {								\
		this_cpu_write(ptr, val);				\
		this_cpu_write(ptr##_cnt,				\
			local_inc_return(&__get_cpu_var(debug_count)));	\
	} while (0)

#define PRINT_NATIVE_FUNC(ptr)			\
	printk(" %s: [%ld] %pS (%p)\n",		\
		#ptr,				\
		this_cpu_read(ptr##_cnt),	\
		this_cpu_read(ptr),		\
	       this_cpu_read(ptr))

void native_irq_soft_disable_debug(void)
{
	preempt_disable();
	PRINT_NATIVE_FUNC(last_irq_disabled);
	PRINT_NATIVE_FUNC(last_irq_enabled);
	PRINT_NATIVE_FUNC(last_trigger);
	preempt_enable();
}

enum {
	NATIVE_IRQ_DO_ENABLE		= 1,
	NATIVE_IRQ_DISABLED		= X86_EFLAGS_IF,
	NATIVE_IRQ_TRIGGERED		= (1 << 31),
};

static inline unsigned long get_irq_flags(void)
{
	return local_read(&__raw_get_cpu_var(is_native_irq_disabled));
}

int sdr_print;
unsigned long native_save_fl(void)
{
	unsigned long ret;
	/*
	 * It might be possible that if irqs are fully enabled
	 * we could migrate. But the result of this operation
	 * will be the same regardless if we move from one
	 * CPU to another.
	 *
	 * Inverse the result, as the test checks if
	 * NATIVE_IRQ_DISABLED is clear, not set.
	 */
	ret = get_irq_flags();
	if (sdr_print) {
		void *le = this_cpu_read(last_irq_enabled);
		void *ld = this_cpu_read(last_irq_disabled);
		sdr_print = 0;
		printk("last enabled: %pS\n", le);
		printk("last disabled %pS\n", ld);
		printk("irq=%lx ret=%lx fl=%lx\n",
		       ret, (~ret) & X86_EFLAGS_IF, raw_native_save_fl());
	}
	return (~get_irq_flags()) & X86_EFLAGS_IF;
}
EXPORT_SYMBOL(native_save_fl);

static void __native_irq_disable(void *ip)
{
	unsigned long native_flags;

	preempt_disable();
	native_flags = get_irq_flags();

	if (native_flags) {
		/* If native_flags are set, we already disabled preemption */
		preempt_enable();
		return;
	}

	UPDATE_NATIVE(last_irq_disabled, ip);
	local_add(NATIVE_IRQ_DISABLED, &__get_cpu_var(is_native_irq_disabled));
	/* Leave with preemption disabled */
}

void native_irq_disable(void)
{
	__native_irq_disable(__builtin_return_address(0));
}
EXPORT_SYMBOL(native_irq_disable);

/**
 * native_simulate_irq - simulate an interrupt that triggered during soft disable
 * @stack: The per_cpu interrupt stack to use.
 * @func: The interrupt function to call.
 * @orig_ax: The saved interrupt vector
 *
 * Defined in assembly, this function is used to simulate an interrupt
 * that happened while the irq soft disabling was in effect.
 *
 * Basically this will simulate the 
 */
extern void native_simulate_irq(void *stack, void *func, unsigned long orig_ax);

static void __native_irq_enable(void *ip)
{
	unsigned long flags;

	/* Do nothing if already enabled */
	if (!get_irq_flags())
		return;

	local_sub(NATIVE_IRQ_DISABLED, &__get_cpu_var(is_native_irq_disabled));

	flags = get_irq_flags();

	/*
	 * At this moment we can be in one of two states.
	 * Either native_flags == 0 or native_flags == triggered
	 * If zero, and an interrupt comes in, then it will simply
	 *  process the interrupt.
	 * If it is triggered, then the interrupt returned with
	 *  real interrupts disabled, and we do not need to worry
	 *  about interrupts coming in now. Call native_simulate_irq()
	 *  to do the nasty work.
	 */
	if (unlikely(flags & NATIVE_IRQ_TRIGGERED)) {
		unsigned long *irq_stack;
		static int once;

		local_sub(NATIVE_IRQ_TRIGGERED,
			  &__get_cpu_var(is_native_irq_disabled));

		/*
		 * If an interrupt was postponed, then no other
		 * flags should be set here.
		 */
		BUG_ON(get_irq_flags());

		irq_stack = (unsigned long *)
			(this_cpu_read(irq_stack_ptr) - IRQ_STACK_SIZE);

		UPDATE_NATIVE(last_trigger, this_cpu_read(native_trigger_func));

		if (once < 2) {
			once++;
		local_add(NATIVE_IRQ_DISABLED, &__get_cpu_var(is_native_irq_disabled));
		printk("simulate IRQ! irq=%x cpu=%d stack=%p func=%pS\n",
		       raw_native_save_fl(), smp_processor_id(),
		       irq_stack, this_cpu_read(native_trigger_func));
		local_sub(NATIVE_IRQ_DISABLED, &__get_cpu_var(is_native_irq_disabled));
		}

		native_simulate_irq(irq_stack, this_cpu_read(native_trigger_func),
				    this_cpu_read(native_orig_ax));
	} else if (flags & NATIVE_IRQ_DO_ENABLE) {
		/* Enable for real too */

	UPDATE_NATIVE(last_irq_enabled, ip);
	if (local_read(&__get_cpu_var(is_native_irq_disabled) & NATIVE_IRQ_DO_ENABLE)) {
		local_sub(NATIVE_IRQ_DO_ENABLE, &__get_cpu_var(is_native_irq_disabled));
		BUG_ON(raw_native_save_fl() & X86_EFLAGS_IF);
		raw_native_irq_enable();
	}

	preempt_enable();
}

void native_irq_enable(void)
{
	__native_irq_enable(__builtin_return_address(0));
}
EXPORT_SYMBOL(native_irq_enable);

void native_restore_fl(unsigned long flags)
{
	if (flags & X86_EFLAGS_IF)
		__native_irq_enable(__builtin_return_address(0));
	else
		__native_irq_disable(__builtin_return_address(0));
}
EXPORT_SYMBOL(native_restore_fl);

typedef void (*irq_func_t)(struct pt_regs *regs);

asmlinkage long
native_check_irq_disable(struct pt_regs *regs, irq_func_t func)
{
	unsigned long native_flags;
	static int once;

	native_flags = get_irq_flags();

	if (native_flags) {
		static int once;
		if (once < 2) {
			once++;
			printk("CHECK IRQ DISABLE %pF %lx native=%lx loc=%pF\n",
			       func, regs->flags, native_flags,
			       (void *)regs->ip);
		}
		BUG_ON(native_flags & NATIVE_IRQ_TRIGGERED);
		local_add(NATIVE_IRQ_TRIGGERED,
			  &__get_cpu_var(is_native_irq_disabled));
		this_cpu_write(native_trigger_func, func);
		this_cpu_write(native_orig_ax, regs->orig_ax);
		/* Keep interrupts disabled */
		regs->flags &= ~X86_EFLAGS_IF;
		return 1;
	}

	/* Interrupts are disabled, let the irq know that too */
	local_add(NATIVE_IRQ_DISABLED, &__get_cpu_var(is_native_irq_disabled));

	if (once < 2) {
		once++;
		printk("CHECK IRQ REAL %pF %lx native=%lx\n",
		       func, regs->flags, native_flags);
	}

	func(regs);

	local_sub(NATIVE_IRQ_DISABLED, &__get_cpu_var(is_native_irq_disabled));

	return 0;
}

#endif /* CONFIG_IRQ_SOFT_DISABLE */

