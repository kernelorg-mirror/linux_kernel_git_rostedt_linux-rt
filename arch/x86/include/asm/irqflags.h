#ifndef _X86_IRQFLAGS_H_
#define _X86_IRQFLAGS_H_

#include <asm/processor-flags.h>

#undef CONFIG_LAZY_IRQ_DEBUG

#define LAZY_IRQ_DISABLED_BIT		0
#define LAZY_IRQ_TEMP_DISABLE_BIT	1
#define LAZY_IRQ_IDLE_BIT		2
#define LAZY_IRQ_REAL_DISABLE_BIT	3

#define LAZY_IRQ_FL_DISABLED		(1 << LAZY_IRQ_DISABLED_BIT)
#define LAZY_IRQ_FL_TEMP_DISABLE	(1 << LAZY_IRQ_TEMP_DISABLE_BIT)
#define LAZY_IRQ_FL_IDLE		(1 << LAZY_IRQ_IDLE_BIT)
#define LAZY_IRQ_FL_REAL_DISABLE	(1 << LAZY_IRQ_REAL_DISABLE_BIT)

#ifndef __ASSEMBLY__
#include <linux/kernel.h>

#ifdef CONFIG_LAZY_IRQ_DEBUG
void update_last_hard_enable(unsigned long addr);
void update_last_soft_enable(unsigned long addr);
void update_last_hard_disable(unsigned long addr);
void update_last_soft_disable(unsigned long addr);
void update_last_preempt_disable(unsigned long addr);
void update_last_preempt_enable(unsigned long addr);
#else
static inline void update_last_hard_enable(unsigned long addr) { }
static inline void update_last_soft_enable(unsigned long addr) { }
static inline void update_last_hard_disable(unsigned long addr) { }
static inline void update_last_soft_disable(unsigned long addr) { }
static inline void update_last_preempt_disable(unsigned long addr) { }
static inline void update_last_preempt_enable(unsigned long addr) { }
#endif

/*
 * Interrupt control:
 */

static inline unsigned long raw_native_save_fl(void)
{
	unsigned long flags;

	/*
	 * "=rm" is safe here, because "pop" adjusts the stack before
	 * it evaluates its effective address -- this is part of the
	 * documented behavior of the "pop" instruction.
	 */
	asm volatile("# __raw_save_flags\n\t"
		     "pushf ; pop %0"
		     : "=rm" (flags)
		     : /* no input */
		     : "memory");

	return flags;
}

static inline void raw_native_restore_fl(unsigned long flags)
{
#ifdef CONFIG_LAZY_IRQ_DEBUG
	if ((raw_native_save_fl() ^ flags) & X86_EFLAGS_IF) {
		if (flags & X86_EFLAGS_IF)
			update_last_hard_enable((long)__builtin_return_address(0));
		else
			update_last_hard_disable((long)__builtin_return_address(0));
	}
#endif

	asm volatile("push %0 ; popf"
		     : /* no output */
		     :"g" (flags)
		     :"memory", "cc");
}

static inline void raw_native_irq_disable(void)
{
	asm volatile("cli": : :"memory");
	update_last_hard_disable((long)__builtin_return_address(0));
}

static inline void raw_native_irq_enable(void)
{
	update_last_hard_enable((long)__builtin_return_address(0));
	asm volatile("sti": : :"memory");
}

static inline void native_safe_halt(void)
{
	asm volatile("sti; hlt": : :"memory");
}

static inline void native_halt(void)
{
	asm volatile("hlt": : :"memory");
}

#ifndef CONFIG_LAZY_IRQ_DISABLE
#define native_save_fl() raw_native_save_fl()
#define native_restore_fl(flags) raw_native_restore_fl(flags)
#define native_irq_disable() raw_native_irq_disable()
#define native_irq_enable() raw_native_irq_enable()
#else
#include <linux/bug.h>

void lazy_irq_bug(const char *file, int line, unsigned long flags, unsigned long raw);

#ifdef CONFIG_LAZY_IRQ_DEBUG
static inline void do_preempt_disable(void)
{
	unsigned long val;

	asm volatile ("addq $1,%%gs:lazy_irq_on\n"
		      "movq %%gs:lazy_irq_on,%0\n" : "=r"(val) : : "memory");
	update_last_preempt_disable((long)__builtin_return_address(0));
}

static inline void do_preempt_enable(void)
{
	unsigned long val;
	static int once;

	asm volatile ("movq %%gs:lazy_irq_on,%0\n"
		      "subq $1,%%gs:lazy_irq_on" : "=r"(val) : : "memory");
	if (!once && !val) {
		once++;
		lazy_irq_bug(__func__, __LINE__, val, val);
	}
	if (!once)
		update_last_preempt_enable((long)__builtin_return_address(0));
}
#else
static inline void do_preempt_disable(void)
{
	asm volatile ("addq $1,%%gs:lazy_irq_on\n" : : : "memory");
}

static inline void do_preempt_enable(void)
{
	asm volatile ("subq $1,%%gs:lazy_irq_on" : : : "memory");
}

static inline void print_lazy_debug(void) { }
static inline void print_lazy_irq(int line) { }

#endif /* CONFIG_LAZY_IRQ_DEBUG */

void lazy_irq_simulate(void *func);

static inline unsigned long get_lazy_irq_flags(void)
{
	unsigned long flags;

	asm volatile ("movq %%gs:lazy_irq_disabled_flags, %0" : "=r"(flags) :: );
	return flags;
}

static inline void * get_lazy_irq_func(void)
{
	void *func;

	asm volatile ("movq %%gs:lazy_irq_func, %0" : "=r"(func) :: );
	return func;
}

static inline unsigned long native_save_fl(void)
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

	if (flags >> LAZY_IRQ_TEMP_DISABLE_BIT)
		return raw_native_save_fl();

	return flags & LAZY_IRQ_FL_DISABLED ? 0 : X86_EFLAGS_IF;
}

static inline void lazy_irq_sub(unsigned long val)
{
#ifdef CONFIG_LAZY_IRQ_DEBUG
	if (val > get_lazy_irq_flags())
		lazy_irq_bug(__func__, __LINE__,
			     get_lazy_irq_flags(), raw_native_save_fl());
#endif

	asm volatile ("subq %0, %%gs:lazy_irq_disabled_flags" : : "r"(val) : "memory");
}

static inline void lazy_irq_add(unsigned long val)
{
	asm volatile ("addq %0, %%gs:lazy_irq_disabled_flags" : : "r"(val) : "memory");
}

static inline void lazy_irq_sub_temp(void)
{
	lazy_irq_sub(LAZY_IRQ_FL_TEMP_DISABLE);
}

static inline void lazy_irq_add_temp(void)
{
	lazy_irq_add(LAZY_IRQ_FL_TEMP_DISABLE);
}

static inline void lazy_irq_sub_disable(void)
{
	update_last_soft_enable((long)__builtin_return_address(0));
	lazy_irq_sub(LAZY_IRQ_FL_DISABLED);
}

static inline void lazy_irq_add_disable(void)
{
	update_last_soft_disable((long)__builtin_return_address(0));
	lazy_irq_add(LAZY_IRQ_FL_DISABLED);
}

static inline void native_irq_disable(void)
{
	unsigned long flags;
	unsigned long raw;

	do_preempt_disable();
	flags = get_lazy_irq_flags();
	raw = raw_native_save_fl();

	if (flags) {
		/* Always disable for real not in lazy mode */
		if (flags >> LAZY_IRQ_TEMP_DISABLE_BIT)
			raw_native_irq_disable();
		/* If native_flags are set, we already disabled preemption */
		do_preempt_enable();
		return;
	}

#ifdef CONFIG_LAZY_IRQ_DEBUG
	if (!(raw & X86_EFLAGS_IF))
		lazy_irq_bug(__func__, __LINE__, flags, raw);
#endif

	lazy_irq_add_disable();
	/* Leave with preemption disabled */
}

static inline void native_irq_enable(void)
{
	unsigned long flags;
	unsigned long raw;
	void *func = NULL;

	flags = get_lazy_irq_flags();
	raw = raw_native_save_fl();

	/* Do nothing if already enabled */
	if (!flags)
		goto out;

	if (flags >> LAZY_IRQ_TEMP_DISABLE_BIT) {
		WARN_ON((flags & LAZY_IRQ_FL_IDLE) && (flags & LAZY_IRQ_FL_DISABLED));
#ifdef CONFIG_LAZY_IRQ_DEBUG
		if ((flags & ~LAZY_IRQ_FL_IDLE) && raw_native_save_fl() & X86_EFLAGS_IF)
			lazy_irq_bug(__func__, __LINE__, flags, raw);
#endif
		if (flags & LAZY_IRQ_FL_TEMP_DISABLE) {
			lazy_irq_sub_temp();
			flags = get_lazy_irq_flags();
			if (flags == LAZY_IRQ_FL_DISABLED)
				lazy_irq_sub_disable();
		}
#ifdef CONFIG_LAZY_IRQ_DEBUG
		if (get_lazy_irq_flags() & LAZY_IRQ_FL_DISABLED)
			lazy_irq_bug(__func__, __LINE__, flags, raw);
#endif

		func = get_lazy_irq_func();
		if (func)
			lazy_irq_simulate(func); /* enables interrupts */
		else
			raw_native_irq_enable();
		goto out;
	}

#ifdef CONFIG_LAZY_IRQ_DEBUG
	if (LAZY_IRQ_FL_DISABLED > get_lazy_irq_flags())
		lazy_irq_bug(__func__, __LINE__, flags, raw);
#endif
	lazy_irq_sub_disable();
	/*
	 * Grab func *after* enabling lazy irqs, this prevents the race
	 * where we enable the lazy irq but a interrupt comes in when
	 * we do it and sets func.
	 */
	func = get_lazy_irq_func();

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
	if (func) {
#ifdef CONFIG_LAZY_IRQ_DEBUG
		if (raw_native_save_fl() & X86_EFLAGS_IF)
			lazy_irq_bug(__func__, __LINE__, flags, raw);
#endif
		lazy_irq_simulate(func);
	}

	do_preempt_enable();
out:
	return;
#ifdef CONFIG_LAZY_IRQ_DEBUG
	if (!(raw_native_save_fl() & X86_EFLAGS_IF)) {
		printk("func=%pS flags=%lx\n", func, get_lazy_irq_flags());
		lazy_irq_bug(__func__, __LINE__, flags, raw);
	}
#endif
}

static inline void native_restore_fl(unsigned long flags)
{
	if (flags & X86_EFLAGS_IF)
		native_irq_enable();
	else
		native_irq_disable();
}
#endif /* CONFIG_LAZY_IRQ_DISABLE */

#endif /* !__ASSEMBLY__ */

#ifdef CONFIG_PARAVIRT
#include <asm/paravirt.h>
#else
#ifndef __ASSEMBLY__
#include <linux/types.h>

static inline notrace unsigned long arch_local_save_flags(void)
{
	return native_save_fl();
}

static inline notrace void arch_local_irq_restore(unsigned long flags)
{
	native_restore_fl(flags);
}

static inline notrace void arch_local_irq_disable(void)
{
	native_irq_disable();
}

static inline notrace void arch_local_irq_enable(void)
{
	native_irq_enable();
}

/*
 * Used in the idle loop; sti takes one instruction cycle
 * to complete:
 */
static inline void arch_safe_halt(void)
{
	native_safe_halt();
}

/*
 * Used when interrupts are already enabled or to
 * shutdown the processor:
 */
static inline void halt(void)
{
	native_halt();
}

/*
 * For spinlocks, etc:
 */
static inline notrace unsigned long arch_local_irq_save(void)
{
	unsigned long flags = arch_local_save_flags();
	arch_local_irq_disable();
	return flags;
}
#else

#define ENABLE_INTERRUPTS(x)	sti
#define DISABLE_INTERRUPTS(x)	cli

#ifdef CONFIG_X86_64
#define SWAPGS	swapgs
/*
 * Currently paravirt can't handle swapgs nicely when we
 * don't have a stack we can rely on (such as a user space
 * stack).  So we either find a way around these or just fault
 * and emulate if a guest tries to call swapgs directly.
 *
 * Either way, this is a good way to document that we don't
 * have a reliable stack. x86_64 only.
 */
#define SWAPGS_UNSAFE_STACK	swapgs

#define PARAVIRT_ADJUST_EXCEPTION_FRAME	/*  */

#define INTERRUPT_RETURN	iretq
#define USERGS_SYSRET64				\
	swapgs;					\
	sysretq;
#define USERGS_SYSRET32				\
	swapgs;					\
	sysretl
#define ENABLE_INTERRUPTS_SYSEXIT32		\
	swapgs;					\
	sti;					\
	sysexit

#else
#define INTERRUPT_RETURN		iret
#define ENABLE_INTERRUPTS_SYSEXIT	sti; sysexit
#define GET_CR0_INTO_EAX		movl %cr0, %eax
#endif


#endif /* __ASSEMBLY__ */
#endif /* CONFIG_PARAVIRT */

#ifndef __ASSEMBLY__
static inline int arch_irqs_disabled_flags(unsigned long flags)
{
	return !(flags & X86_EFLAGS_IF);
}

static inline int arch_irqs_disabled(void)
{
	unsigned long flags = arch_local_save_flags();

	return arch_irqs_disabled_flags(flags);
}

#else

#ifdef CONFIG_X86_64
#define ARCH_LOCKDEP_SYS_EXIT		call lockdep_sys_exit_thunk
#define ARCH_LOCKDEP_SYS_EXIT_IRQ	\
	TRACE_IRQS_ON; \
	sti; \
	SAVE_REST; \
	LOCKDEP_SYS_EXIT; \
	RESTORE_REST; \
	cli; \
	TRACE_IRQS_OFF;

#else
#define ARCH_LOCKDEP_SYS_EXIT			\
	pushl %eax;				\
	pushl %ecx;				\
	pushl %edx;				\
	call lockdep_sys_exit;			\
	popl %edx;				\
	popl %ecx;				\
	popl %eax;

#define ARCH_LOCKDEP_SYS_EXIT_IRQ
#endif

#ifdef CONFIG_TRACE_IRQFLAGS
#  define TRACE_IRQS_ON		call trace_hardirqs_on_thunk;
#  define TRACE_IRQS_OFF	call trace_hardirqs_off_thunk;
#else
#  define TRACE_IRQS_ON
#  define TRACE_IRQS_OFF
#endif
#ifdef CONFIG_DEBUG_LOCK_ALLOC
#  define LOCKDEP_SYS_EXIT	ARCH_LOCKDEP_SYS_EXIT
#  define LOCKDEP_SYS_EXIT_IRQ	ARCH_LOCKDEP_SYS_EXIT_IRQ
# else
#  define LOCKDEP_SYS_EXIT
#  define LOCKDEP_SYS_EXIT_IRQ
# endif

#endif /* __ASSEMBLY__ */

#endif
