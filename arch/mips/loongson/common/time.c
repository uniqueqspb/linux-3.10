/*
 * Copyright (C) 2007 Lemote, Inc. & Institute of Computing Technology
 * Author: Fuxin Zhang, zhangfx@lemote.com
 *
 * Copyright (C) 2009 Lemote Inc.
 * Author: Wu Zhangjin, wuzhangjin@gmail.com
 *
 *  This program is free software; you can redistribute	 it and/or modify it
 *  under  the terms of	 the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the	License, or (at your
 *  option) any later version.
 */
#include <asm/mc146818-time.h>
#include <asm/time.h>
#include <asm/hpet.h>

#include <loongson.h>
#include <cs5536/cs5536_mfgpt.h>


#if defined(CONFIG_LOONGSON3_ENHANCEMENT) && !defined(CONFIG_KVM_GUEST_LS3A3000)
static cycle_t read_const_cntr64(struct clocksource *clk)
{
       cycle_t count;
       __asm__ __volatile__(
       "       .set push\n"
       "       .set mips32r2\n"
       "       rdhwr   %0, $30\n"
       "       .set pop\n"
       : "=r" (count));

       return count;
}

static struct clocksource clocksource_loongson = {
       .name           = "LOONGSON_CONST",
       .read           = read_const_cntr64,
       .mask           = CLOCKSOURCE_MASK(64),
       .flags          = CLOCK_SOURCE_IS_CONTINUOUS,
};



unsigned long long notrace sched_clock(void)
{
	/* 64-bit arithmatic can overflow, so use 128-bit.  */
	u64 t1, t2, t3;
	unsigned long long rv;
	u64 mult = clocksource_loongson.mult;
	u64 shift = clocksource_loongson.shift;
	u64 cnt = read_const_cntr64(NULL);

	asm (
		"dmultu\t%[cnt],%[mult]\n\t"
		"nor\t%[t1],$0,%[shift]\n\t"
		"mfhi\t%[t2]\n\t"
		"mflo\t%[t3]\n\t"
		"dsll\t%[t2],%[t2],1\n\t"
		"dsrlv\t%[rv],%[t3],%[shift]\n\t"
		"dsllv\t%[t1],%[t2],%[t1]\n\t"
		"or\t%[rv],%[t1],%[rv]\n\t"
		: [rv] "=&r" (rv), [t1] "=&r" (t1), [t2] "=&r" (t2), [t3] "=&r" (t3)
		: [cnt] "r" (cnt), [mult] "r" (mult), [shift] "r" (shift)
		: "hi", "lo");
	return rv;
}

static void loongson3_init_clock(void)
{
	int res = (read_c0_prid() & 0xffff);
	if (res >=0x6308 && res < 0x630f) {
		/* for 3a2000/3a3000 const-freq counter freq equal cpu_clock_freq */
		clocksource_loongson.rating = cpu_clock_freq/400000000;
		clocksource_register_hz(&clocksource_loongson, cpu_clock_freq);
	} else {
		panic("old loongson 3 cpu does not support const-frequence counter!");
	}


}
#else
static void loongson3_init_clock(void) {}
#endif


void __init plat_time_init(void)
{
	/* setup mips r4k timer */
	mips_hpt_frequency = cpu_clock_freq / 2;

	loongson3_init_clock();

#if defined(CONFIG_RS780_HPET) || defined(CONFIG_LS7A_HPET)
	if(cpu_has_vz)
		setup_hpet_timer();
#else
	setup_mfgpt0_timer();
#endif
}

void read_persistent_clock(struct timespec *ts)
{
	ts->tv_sec = mc146818_get_cmos_time();
	ts->tv_nsec = 0;
}
