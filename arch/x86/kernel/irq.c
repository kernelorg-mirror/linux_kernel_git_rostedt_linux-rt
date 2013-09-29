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

/* Start out enabling and disabling for real */
DEFINE_PER_CPU(local_t, lazy_irq_disabled_flags) = LOCAL_INIT(LAZY_IRQ_FL_TEMP_DISABLED);
DEFINE_PER_CPU(void *, lazy_irq_func);
DEFINE_PER_CPU(unsigned long, lazy_irq_vector);

#define BUG_ON_IRQS_ENABLED()					\
	do {							\
		BUG_ON(raw_native_save_fl() & X86_EFLAGS_IF);	\
	} while (0)

static DEFINE_PER_CPU(local_t, debug_count);
static DEFINE_PER_CPU(void *, last_irq_disabled);
static DEFINE_PER_CPU(void *, last_irq_enabled);
static DEFINE_PER_CPU(void *, last_trigger);
static DEFINE_PER_CPU(long, last_irq_disabled_cnt);
static DEFINE_PER_CPU(long, last_irq_enabled_cnt);
static DEFINE_PER_CPU(long, last_trigger_cnt);

#define UPDATE_LAZY(ptr, val)						\
	do {								\
		this_cpu_write(ptr, val);				\
		this_cpu_write(ptr##_cnt,				\
			local_inc_return(&__get_cpu_var(debug_count)));	\
	} while (0)

#define PRINT_LAZY_FUNC(ptr)			\
	printk(" %s: [%ld] %pS (%p)\n",		\
		#ptr,				\
		this_cpu_read(ptr##_cnt),	\
		this_cpu_read(ptr),		\
	       this_cpu_read(ptr))

void lazy_irq_soft_disable_debug(void)
{
	preempt_disable();
	PRINT_LAZY_FUNC(last_irq_disabled);
	PRINT_LAZY_FUNC(last_irq_enabled);
	PRINT_LAZY_FUNC(last_trigger);
	preempt_enable();
}

__init static int init_lazy_irqs(void)
{
	int cpu;

	return 0;
	/* Only boot CPU needs irqs disabled */
	for_each_possible_cpu(cpu) {
		if (cpu == smp_processor_id())
			continue;
		local_set(&per_cpu(lazy_irq_disabled_flags, cpu), 0);
	}
	return 0;
}
early_initcall(init_lazy_irqs);

static inline unsigned long get_lazy_irq_flags(void)
{
	return local_read(&__raw_get_cpu_var(lazy_irq_disabled_flags));
}

unsigned long lazy_irq_flags(void)
{
	return get_lazy_irq_flags();
}

DEFINE_PER_CPU(unsigned long, sdr_last);
DEFINE_PER_CPU(void *, sdr_func1);
DEFINE_PER_CPU(void *, sdr_func2);
DEFINE_PER_CPU(void *, sdr_func3);
DEFINE_PER_CPU(void *, sdr_func4);
DEFINE_PER_CPU(void *, sdr_func5);
DEFINE_PER_CPU(void *, sdr_func6);
DEFINE_PER_CPU(void *, sdr_func7);
DEFINE_PER_CPU(struct task_struct *, sdr_task);
DEFINE_PER_CPU(unsigned long, sdr_flags1);
DEFINE_PER_CPU(unsigned long, sdr_flags2);
DEFINE_PER_CPU(unsigned long, sdr_flags3);
DEFINE_PER_CPU(unsigned long, sdr_flags4);
DEFINE_PER_CPU(unsigned long, sdr_flags5);
DEFINE_PER_CPU(unsigned long, sdr_flags6);
DEFINE_PER_CPU(unsigned long, sdr_raw_flags1);
DEFINE_PER_CPU(unsigned long, sdr_raw_flags2);
void show_lazy_irq_flags(void)
{
	printk(KERN_DEFAULT "LAZY DISABLE FLAGS: %lx\n", get_lazy_irq_flags());
	printk("last switch %pS\n", this_cpu_read(sdr_last));
	printk("last flags1 %lx\n", this_cpu_read(sdr_flags1));
	printk("last flags2 %lx\n", this_cpu_read(sdr_flags2));
	printk("last flags3 %lx\n", this_cpu_read(sdr_flags3));
	printk("last flags4 %lx\n", this_cpu_read(sdr_flags4));
	printk("last flags5 %lx\n", this_cpu_read(sdr_flags5));
	printk("last flags6 %lx\n", this_cpu_read(sdr_flags6));
	printk("last raw flags1 %lx\n", this_cpu_read(sdr_raw_flags1));
	printk("last raw flags2 %lx\n", this_cpu_read(sdr_raw_flags2));
	printk("last func1 %pS\n", this_cpu_read(sdr_func1));
	printk("last func2 %pS\n", this_cpu_read(sdr_func2));
	printk("last func3 %pS\n", this_cpu_read(sdr_func3));
	printk("last func4 %pS\n", this_cpu_read(sdr_func4));
	printk("last func5 %pS\n", this_cpu_read(sdr_func5));
	printk("last func6 %pS\n", this_cpu_read(sdr_func6));
	printk("last func7 %pS\n", this_cpu_read(sdr_func7));
	if (this_cpu_read(sdr_task)) {
		printk("last task %s:%d\n",
		       this_cpu_read(sdr_task)->comm,
		       this_cpu_read(sdr_task)->pid);
	} else
		printk("last task NULL\n");
}

void native_irq_soft_disable_debug(void)
{
	preempt_disable();
	printk("lazy_flags=%lx\n", get_lazy_irq_flags());
	preempt_enable();
}

int sdr_print;
unsigned long native_save_fl(void)
{
	unsigned long flags;

	/*
	 * It might be possible that if irqs are fully enabled
	 * we could migrate. But the result of this operation
	 * will be the same regardless if we move from one
	 * CPU to another.
	 *
	 * Inverse the result, as the test checks if
	 * NATIVE_IRQ_DISABLED is clear, not set.
	 */
	flags = get_lazy_irq_flags();

	if (sdr_print) {
		void *le = this_cpu_read(last_irq_enabled);
		void *ld = this_cpu_read(last_irq_disabled);
		sdr_print = 0;
		printk("last enabled: %pS\n", le);
		printk("last disabled %pS\n", ld);
		printk("irq=%lx ret=%lx fl=%lx\n",
		       flags, (~flags) & X86_EFLAGS_IF, raw_native_save_fl());
	}
	if (flags >> LAZY_IRQ_TEMP_DISABLED_BIT)
		return raw_native_save_fl();
	return flags & LAZY_IRQ_FL_IRQ_DISABLED ? 0 : X86_EFLAGS_IF;
}
EXPORT_SYMBOL(native_save_fl);

static inline void lazy_irq_sub_temp(void)
{
	local_sub(LAZY_IRQ_FL_TEMP_DISABLED,
		  &__get_cpu_var(lazy_irq_disabled_flags));
}

static inline void lazy_irq_add_temp(void)
{
	local_add(LAZY_IRQ_FL_TEMP_DISABLED,
		  &__get_cpu_var(lazy_irq_disabled_flags));
}

static inline void lazy_irq_sub_disable(void)
{
	local_sub(LAZY_IRQ_FL_IRQ_DISABLED,
		  &__get_cpu_var(lazy_irq_disabled_flags));
}

static inline void lazy_irq_add_disable(void)
{
	local_add(LAZY_IRQ_FL_IRQ_DISABLED,
		  &__get_cpu_var(lazy_irq_disabled_flags));
}

static void __native_irq_disable(void *ip)
{
	unsigned long flags;

	preempt_disable();
	flags = get_lazy_irq_flags();

	if (flags) {
		/* Always disable for real not in lazy mode */
		if (flags >> LAZY_IRQ_TEMP_DISABLED_BIT)
			raw_native_irq_disable();
		/* If native_flags are set, we already disabled preemption */
		preempt_enable();
		return;
	}

	UPDATE_LAZY(last_irq_disabled, ip);
	lazy_irq_add_disable();
	/* Leave with preemption disabled */
}

void native_irq_disable(void)
{
	__native_irq_disable(__builtin_return_address(0));
}
EXPORT_SYMBOL(native_irq_disable);

/**
 * native_simulate_irq - simulate an interrupt that triggered during soft disable
 * @func: The interrupt function to call.
 * @orig_ax: The saved interrupt vector
 *
 * Defined in assembly, this function is used to simulate an interrupt
 * that happened while the irq soft disabling was in effect.
 *
 * Basically this will simulate the 
 */
extern void native_simulate_irq(void *func, unsigned long orig_ax);

static void lazy_irq_simulate(void *func)
{
	this_cpu_write(lazy_irq_func, NULL);

	BUG_ON_IRQS_ENABLED();

	native_simulate_irq(func, this_cpu_read(lazy_irq_vector));
}

static void __native_irq_enable(void *ip)
{
	unsigned long flags;
	unsigned long raw;
	void *func;
	static int once;

	flags = get_lazy_irq_flags();
	raw = raw_native_save_fl();

	/* Do nothing if already enabled */
	if (!flags)
		return;

	func = this_cpu_read(lazy_irq_func);

	if (flags >> LAZY_IRQ_TEMP_DISABLED_BIT) {
		BUG_ON_IRQS_ENABLED();
		if (flags & LAZY_IRQ_FL_TEMP_DISABLED) {
			lazy_irq_sub_temp();
			if (flags & LAZY_IRQ_FL_IRQ_DISABLED)
				lazy_irq_sub_disable();
		}

		if (func)
			lazy_irq_simulate(func); /* enables interrupts */
		else
			raw_native_irq_enable();
		return;
	}

	lazy_irq_sub_disable();

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
	if (unlikely(func)) {
		if (!once && raw_native_save_fl() & X86_EFLAGS_IF) {
			once = 1;
			raw_native_irq_disable();
			lazy_irq_add_temp();
			show_lazy_irq_flags();
			printk("flags=%lx init_raw=%lx func=%pS\n", flags, raw, func);
			printk("raw=%lx\n", raw_native_save_fl());
			BUG();
		}
		lazy_irq_simulate(func);
	}

	if (!once && !(raw_native_save_fl() & X86_EFLAGS_IF)) {
		once = 1;
		raw_native_irq_enable();
		show_lazy_irq_flags();
		printk("flags=%lx init_raw=%lx func=%pS\n", flags, raw, func);
		printk("raw=%lx\n", raw_native_save_fl());
		BUG();
	}
	preempt_enable();
}

int lazy_irq_idle_enter(void)
{
	/*
	 * Note, if there's a pending interrupt, then on real hardware
	 * when the x86_idle() is called, it would trigger immediately.
	 * We need to imitate that.
	 *
	 * Disable interrupts for real, need this anyway, as interrupts
	 * would be enabled by the cpu idle.
	 */
	if (this_cpu_read(lazy_irq_func))
		BUG_ON_IRQS_ENABLED();

	raw_native_irq_disable();
	if (this_cpu_read(lazy_irq_func)) {
		/* Process the interrupt and do not go idle */
		local_irq_enable();
		return 0;
	}

	/* Interrupts will be enabled exiting x86_idle() */
	BUG_ON(!(get_lazy_irq_flags() & LAZY_IRQ_FL_IRQ_DISABLED));
	lazy_irq_sub_disable();
	return 1;
}

asmlinkage void lazy_irq_debug(long id, long err, void *func)
{
	printk("(%ld err=%lx f=%pS) flags=%lx vect=%lx func=%pS\n", id, ~err, func,
	       get_lazy_irq_flags(),
	       this_cpu_read(lazy_irq_vector),
	       this_cpu_read(lazy_irq_func));
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

#endif /* CONFIG_IRQ_SOFT_DISABLE */

