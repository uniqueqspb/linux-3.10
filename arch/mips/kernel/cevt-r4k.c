/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2007 MIPS Technologies, Inc.
 * Copyright (C) 2007 Ralf Baechle <ralf@linux-mips.org>
 */
#include <linux/clockchips.h>
#include <linux/interrupt.h>
#include <linux/percpu.h>
#include <linux/smp.h>
#include <linux/irq.h>

#include <asm/smtc_ipi.h>
#include <asm/time.h>
#include <asm/cevt-r4k.h>
#include <asm/gic.h>

#if defined(CONFIG_LOONGSON3_ENHANCEMENT) && !defined(CONFIG_KVM_GUEST_LS3A3000)

#include <loongson.h>

#define CPU_TO_CONF(x)	(0x900000003ff00000 | (((unsigned long)x & 0x3) << 8) \
		| (((unsigned long)x & 0xc) << 42))
#define OFF_TIMER	0x1060

static int loongson3_next_event(unsigned long delta,
				struct clock_event_device *evt)
{
	unsigned long addr;
	unsigned long cnt;
	unsigned int raw_cpuid;

	raw_cpuid = (read_c0_ebase() & 0x3ff);
	addr = CPU_TO_CONF(raw_cpuid) + OFF_TIMER;

	cnt = (delta | (0x5UL << 61));
	ls64_conf_write64(cnt, (void *)addr);
	return 0;
}
#endif

/*
 * The SMTC Kernel for the 34K, 1004K, et. al. replaces several
 * of these routines with SMTC-specific variants.
 */

#ifndef CONFIG_MIPS_MT_SMTC
static int mips_next_event(unsigned long delta,
			   struct clock_event_device *evt)
{
	unsigned int cnt;
	int res;

	cnt = read_c0_count();
	cnt += delta;
	write_c0_compare(cnt);
	res = ((int)(read_c0_count() - cnt) >= 0) ? -ETIME : 0;
	return res;
}

#endif /* CONFIG_MIPS_MT_SMTC */

void mips_set_clock_mode(enum clock_event_mode mode,
				struct clock_event_device *evt)
{
	/* Nothing to do ...  */
}

DEFINE_PER_CPU(struct clock_event_device, mips_clockevent_device);
int cp0_timer_irq_installed;

extern void loongson3_cache_stall_unlock(int cpu, int irq);
#ifndef CONFIG_MIPS_MT_SMTC
irqreturn_t c0_compare_interrupt(int irq, void *data)
{
	const int r2 = cpu_has_mips_r2;
	struct clock_event_device *cd;
	int cpu = smp_processor_id();
#if defined(CONFIG_CPU_LOONGSON3)
#if defined(CONFIG_OPROFILE)
	if(!(read_c0_cause() & (1<<30)))  return IRQ_NONE;
#endif
	loongson3_cache_stall_unlock(cpu, irq);
#endif
	/*
	 * Suckage alert:
	 * Before R2 of the architecture there was no way to see if a
	 * performance counter interrupt was pending, so we have to run
	 * the performance counter interrupt handler anyway.
	 */
	if (handle_perf_irq(r2))
		goto out;

	/*
	 * The same applies to performance counter interrupts.	But with the
	 * above we now know that the reason we got here must be a timer
	 * interrupt.  Being the paranoiacs we are we check anyway.
	 */
	if (!r2 || (read_c0_cause() & (1 << 30))) {
		/* Clear Count/Compare Interrupt */
		write_c0_compare(read_c0_compare());
		cd = &per_cpu(mips_clockevent_device, cpu);
#ifdef CONFIG_CEVT_GIC
		if (!gic_present)
#endif
		cd->event_handler(cd);
	}

out:
	return IRQ_HANDLED;
}

#endif /* Not CONFIG_MIPS_MT_SMTC */

struct irqaction c0_compare_irqaction = {
	.handler = c0_compare_interrupt,
	.flags = IRQF_PERCPU | IRQF_SHARED | IRQF_TIMER,
	.name = "timer",
};


void mips_event_handler(struct clock_event_device *dev)
{
}

/*
 * FIXME: This doesn't hold for the relocated E9000 compare interrupt.
 */
static int c0_compare_int_pending(void)
{
#ifdef CONFIG_IRQ_GIC
	if (cpu_has_veic)
		return gic_get_timer_pending();
#endif
	return (read_c0_cause() >> cp0_compare_irq_shift) & (1ul << CAUSEB_IP);
}

/*
 * Compare interrupt can be routed and latched outside the core,
 * so wait up to worst case number of cycle counter ticks for timer interrupt
 * changes to propagate to the cause register.
 */
#define COMPARE_INT_SEEN_TICKS 50

int c0_compare_int_usable(void)
{
	unsigned int delta;
	unsigned int cnt;

#ifdef CONFIG_KVM_GUEST
    return 1;
#endif

	/*
	 * IP7 already pending?	 Try to clear it by acking the timer.
	 */
	if (c0_compare_int_pending()) {
		cnt = read_c0_count();
		write_c0_compare(cnt);
		back_to_back_c0_hazard();
		while (read_c0_count() < (cnt  + COMPARE_INT_SEEN_TICKS))
			if (!c0_compare_int_pending())
				break;
		if (c0_compare_int_pending())
			return 0;
	}

	for (delta = 0x10; delta <= 0x400000; delta <<= 1) {
		cnt = read_c0_count();
		cnt += delta;
		write_c0_compare(cnt);
		back_to_back_c0_hazard();
		if ((int)(read_c0_count() - cnt) < 0)
		    break;
		/* increase delta if the timer was already expired */
	}

	while ((int)(read_c0_count() - cnt) <= 0)
		;	/* Wait for expiry  */

	while (read_c0_count() < (cnt + COMPARE_INT_SEEN_TICKS))
		if (c0_compare_int_pending())
			break;
	if (!c0_compare_int_pending())
		return 0;
	cnt = read_c0_count();
	write_c0_compare(cnt);
	back_to_back_c0_hazard();
	while (read_c0_count() < (cnt + COMPARE_INT_SEEN_TICKS))
		if (!c0_compare_int_pending())
			break;
	if (c0_compare_int_pending())
		return 0;

	/*
	 * Feels like a real count / compare timer.
	 */
	return 1;
}

#ifndef CONFIG_MIPS_MT_SMTC
int  r4k_clockevent_init(void)
{
	unsigned int cpu = smp_processor_id();
	struct clock_event_device *cd;
	unsigned int irq;

	if (!cpu_has_counter || !mips_hpt_frequency)
		return -ENXIO;

	if (!c0_compare_int_usable())
		return -ENXIO;

	/*
	 * With vectored interrupts things are getting platform specific.
	 * get_c0_compare_int is a hook to allow a platform to return the
	 * interrupt number of it's liking.
	 */
	irq = MIPS_CPU_IRQ_BASE + cp0_compare_irq;
	if (get_c0_compare_int)
		irq = get_c0_compare_int();

	cd = &per_cpu(mips_clockevent_device, cpu);

	cd->name		= "MIPS";
	cd->features		= CLOCK_EVT_FEAT_ONESHOT;

	cd->rating		= 300;
	cd->irq			= irq;
	cd->cpumask		= cpumask_of(cpu);
	cd->set_next_event	= mips_next_event;
	cd->set_mode		= mips_set_clock_mode;
	cd->event_handler	= mips_event_handler;

#if !defined(CONFIG_LOONGSON3_ENHANCEMENT)
#ifdef CONFIG_CEVT_GIC
	if (!gic_present)
#endif
	clockevents_config_and_register(cd, mips_hpt_frequency, 0x300, 0x7fffffff);
#else
#if !defined(CONFIG_KVM_GUEST_LS3A3000)

	cd->name		= "loongson3";
	cd->features		= CLOCK_EVT_FEAT_ONESHOT;

	cd->rating		= 300;
	cd->irq			= irq;
	cd->cpumask		= cpumask_of(cpu);
	cd->set_next_event	= loongson3_next_event;
	cd->set_mode		= mips_set_clock_mode;
	cd->event_handler	= mips_event_handler;

	clockevents_config_and_register(cd, mips_hpt_frequency * 2, 0x600, ((1UL << 48) - 1));
	irq = read_c0_config6();
	irq &= ~MIPS_CONF6_INNTIMER;
	irq |= MIPS_CONF6_EXTIMER;
	write_c0_config6(irq);
#endif
#endif
	if (cp0_timer_irq_installed)
		return 0;

	cp0_timer_irq_installed = 1;

	setup_irq(cd->irq, &c0_compare_irqaction);

	return 0;
}

#endif /* Not CONFIG_MIPS_MT_SMTC */
