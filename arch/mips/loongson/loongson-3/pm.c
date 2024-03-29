/*
 *  Lemote Loongson-3A family machines' specific suspend support
 *
 *  Copyright (C) 2009 Lemote Inc.
 *  Author: Wu Zhangjin <wuzhangjin@gmail.com>
 *  Author: Chen Huacai <chenhuacai@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/suspend.h>
#include <linux/interrupt.h>
#include <linux/pm.h>
#include <linux/cpu.h>
#include <linux/i8042.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/delay.h>

#include <asm/i8259.h>
#include <asm/mipsregs.h>
#include <asm/bootinfo.h>
#include <asm/tlbflush.h>

#include <loongson.h>
#include <ec_wpce775l.h>
#include <loongson-pch.h>

#define I8042_CTR_KBDINT	0x01
#define I8042_CTR_KBDDIS	0x10
#define I8042_KBD_IRQ		1

extern void acpi_sleep_prepare(void);
extern void acpi_sleep_complete(void);
extern void acpi_registers_setup(void);

struct loongson_registers {
	u32 config4;
	u32 config6;
	u64 pgd;
	u64 kpgd;
	u32 pwctl;
	u64 pwbase;
	u64 pwsize;
	u64 pwfield;
	u32 hwrena;
	u64 userlocal;
};

static unsigned char i8042_ctr;
static struct loongson_registers loongson_regs;

static int i8042_enable_kbd_port(void)
{
	if (i8042_command(&i8042_ctr, I8042_CMD_CTL_RCTR)) {
		pr_err("i8042.c: Can't read CTR while enabling i8042 kbd port."
		       "\n");
		return -EIO;
	}

	i8042_ctr &= ~I8042_CTR_KBDDIS;
	i8042_ctr |= I8042_CTR_KBDINT;

	if (i8042_command(&i8042_ctr, I8042_CMD_CTL_WCTR)) {
		i8042_ctr &= ~I8042_CTR_KBDINT;
		i8042_ctr |= I8042_CTR_KBDDIS;
		pr_err("i8042.c: Failed to enable KBD port.\n");

		return -EIO;
	}

	return 0;
}

void setup_wakeup_events(void)
{
	/* open the keyboard irq in i8259A */
	outb_p((0xff & ~(1 << I8042_KBD_IRQ)), PIC_MASTER_IMR);
	/* enable keyboard port */
	i8042_enable_kbd_port();
}

int wakeup_loongson(void)
{
	int irq;

	/* query the interrupt number */
	irq = LOONGSON_HT1_INT_VECTOR(0);

	if (irq & (1<<I8042_KBD_IRQ)) {
		return 1;
	}

	return 0;
}

void uncache_resume(void)
{
	if (loongson_pch->board_type == LS7A) {
		HT_uncache_enable_reg0	= HT_cache_enable_reg1; //for 7a gpu
		HT_uncache_base_reg0	= HT_cache_base_reg1;
	} else {
		HT_uncache_enable_reg0  = 0xc0000000; //Low 256M
		HT_uncache_base_reg0    = 0x0080fff0;
	}
	HT_uncache_enable_reg1	= 0xc0000000; //Node 0
	HT_uncache_base_reg1	= 0x0000e000;
	HT_uncache_enable_reg2	= 0xc0100000; //Node 1
	HT_uncache_base_reg2	= 0x2000e000;
	HT_uncache_enable_reg3	= 0xc0200000; //Node 2/3
	HT_uncache_base_reg3	= 0x4000c000;
	writeq(0x0000202000000000, (void *)0x900000003ff02708);
	writeq(0xffffffe000000000, (void *)0x900000003ff02748);
	writeq(0x0000300000000086, (void *)0x900000003ff02788);
}

void mach_suspend(suspend_state_t state)
{
	if (state == PM_SUSPEND_MEM) {
		if (loongson_pch->board_type == RS780E)
			acpi_sleep_prepare();

		if (cpu_has_ftlb) {
			loongson_regs.config4 = read_c0_config4();
			loongson_regs.config6 = read_c0_config6();
		}

		if (cpu_has_ldpte) {
			loongson_regs.pgd = read_c0_pgd();
			loongson_regs.kpgd = read_c0_kpgd();
			loongson_regs.pwctl = read_c0_pwctl();
			loongson_regs.pwbase = read_c0_pwbase();
			loongson_regs.pwsize = read_c0_pwsize();
			loongson_regs.pwfield = read_c0_pwfield();
		}

		if (cpu_has_userlocal) {
			loongson_regs.hwrena = read_c0_hwrena();
			loongson_regs.userlocal = read_c0_userlocal();
		}
	}

	/* Workaround: disable spurious IRQ1 via EC */
	if (state == PM_SUSPEND_STANDBY) {
		ec_write_noindex(CMD_RESET, STANDBY_ON);
		mdelay(900);
	}
}

void mach_resume(suspend_state_t state)
{
	if (state == PM_SUSPEND_MEM) {
		local_flush_tlb_all();

		if (cpu_has_ftlb) {
			write_c0_config4(loongson_regs.config4);
			write_c0_config6(loongson_regs.config6);
		}

		if (cpu_has_ldpte) {
			write_c0_pgd(loongson_regs.pgd);
			write_c0_kpgd(loongson_regs.kpgd);
			write_c0_pwctl(loongson_regs.pwctl);
			write_c0_pwbase(loongson_regs.pwbase);
			write_c0_pwsize(loongson_regs.pwsize);
			write_c0_pwfield(loongson_regs.pwfield);
		}

		if (cpu_has_userlocal) {
			write_c0_hwrena(loongson_regs.hwrena);
			write_c0_userlocal(loongson_regs.userlocal);
		}

		loongson_pch->early_config();

		if (hw_coherentio == 0)
			uncache_resume();

		if (loongson_pch->board_type == RS780E) {
			acpi_registers_setup();
			acpi_sleep_complete();
		}
	}
}
