/*
 * relocate_kernel.S for kexec
 * Created by <nschichan@corp.free.fr> on Thu Oct 12 17:49:57 2006
 *
 * This source code is licensed under the GNU General Public License,
 * Version 2.  See the file COPYING for more details.
 */

#include <asm/asm.h>
#include <asm/asmmacro.h>
#include <asm/regdef.h>
#include <asm/mipsregs.h>
#include <asm/stackframe.h>
#include <asm/addrspace.h>

LEAF(relocate_new_kernel)
	PTR_L a0,	arg0
	PTR_L a1,	arg1
	PTR_L a2,	arg2
	PTR_L a3,	arg3

	PTR_L		s0, kexec_indirection_page
	PTR_L		s1, kexec_start_address

process_entry:
	PTR_L		s2, (s0)
	PTR_ADD		s0, s0, SZREG

	/*
	 * In case of a kdump/crash kernel, the indirection page is not
	 * populated as the kernel is directly copied to a reserved location
	 */
	beqz		s2, done

	/* destination page */
	and		s3, s2, 0x1
	beq		s3, zero, 1f
	and		s4, s2, ~0x1	/* store destination addr in s4 */
	b		process_entry

1:
	/* indirection page, update s0	*/
	and		s3, s2, 0x2
	beq		s3, zero, 1f
	and		s0, s2, ~0x2
	b		process_entry

1:
	/* done page */
	and		s3, s2, 0x4
	beq		s3, zero, 1f
	b		done
1:
	/* source page */
	and		s3, s2, 0x8
	beq		s3, zero, process_entry
	and		s2, s2, ~0x8
	li		s6, (1 << _PAGE_SHIFT) / SZREG

copy_word:
	/* copy page word by word */
	REG_L		s5, (s2)
	REG_S		s5, (s4)
	PTR_ADD		s4, s4, SZREG
	PTR_ADD		s2, s2, SZREG
	LONG_SUB	s6, s6, 1
	beq		s6, zero, process_entry
	b		copy_word
	b		process_entry

done:
#ifdef CONFIG_SMP
	/* kexec_flag reset is signal to other CPUs what kernel
	   was moved to it's location. Note - we need relocated address
	   of kexec_flag.  */

	bal		1f
 1:	move		t1,ra;
	PTR_LA		t2,1b
	PTR_LA		t0,kexec_flag
	PTR_SUB		t0,t0,t2;
	PTR_ADD		t0,t1,t0;
	LONG_S		zero,(t0)
#endif

#ifdef CONFIG_CPU_CAVIUM_OCTEON
	/* We need to flush I-cache before jumping to new kernel.
	 * Unfortunatelly, this code is cpu-specific.
	 */
	.set push
	.set noreorder
	syncw
	syncw
	synci		0($0)
	.set pop
#else
	sync
#endif
	/* jump to kexec_start_address */
	j		s1
	END(relocate_new_kernel)

#ifdef CONFIG_SMP
/*
 * Other CPUs should wait until code is relocated and
 * then start at entry (?) point.
 */
LEAF(kexec_smp_wait)
	PTR_L		a0, s_arg0
	PTR_L		a1, s_arg1
	PTR_L		a2, s_arg2
	PTR_L		a3, s_arg3
	PTR_L		s1, kexec_start_address

	/* Non-relocated address works for args and kexec_start_address ( old
	 * kernel is not overwritten). But we need relocated address of
	 * kexec_flag.
	 */

	bal		1f
1:	move		t1,ra;
	PTR_LA		t2,1b
	PTR_LA		t0,kexec_flag
	PTR_SUB		t0,t0,t2;
	PTR_ADD		t0,t1,t0;

1:	LONG_L		s0, (t0)
	bne		s0, zero,1b

#ifdef CONFIG_CPU_CAVIUM_OCTEON
	.set push
	.set noreorder
	synci		0($0)
	.set pop
#else
	sync
#endif

#ifdef CONFIG_CPU_LOONGSON3
	/* s0:prid s1:initfn */
	/* t0:base t1:cpuid t2:node t3:core t9:count */
	mfc0  t1, $15, 1
	andi  t1, 0x3ff
	dli   t0, 0x900000003ff01000
	andi  t3, t1, 0x3
	sll   t3, 8               /* get core id */
	or    t0, t0, t3
	andi  t2, t1, 0xc
	dsll  t2, 42              /* get node id */
	or    t0, t0, t2
	mfc0  s0, $15, 0
	andi  s0, s0, 0xf
	blt   s0, 0x6, 1f         /* Loongson-3A1000 */
	bgt   s0, 0x7, 1f         /* Loongson-3A2000/3A3000 */
	dsrl  t2, 30              /* Loongson-3B1000/3B1500 need bit15:14 */
	or    t0, t0, t2
1:	li    t9, 0x100           /* wait for init loop */
2:	addiu t9, -1              /* limit mailbox access */
	bnez  t9, 2b
	ld    s1, 0x20(t0)        /* get PC via mailbox */
	beqz  s1, 1b
	ld    sp, 0x28(t0)        /* get SP via mailbox */
	ld    gp, 0x30(t0)        /* get GP via mailbox */
	ld    a1, 0x38(t0)
	jr    s1                  /* jump to initial PC */
#endif

	j		s1
	END(kexec_smp_wait)
#endif

#ifdef __mips64
       /* all PTR's must be aligned to 8 byte in 64-bit mode */
       .align  3
#endif

/* All parameters to new kernel are passed in registers a0-a3.
 * kexec_args[0..3] are uses to prepare register values.
 */

kexec_args:
	EXPORT(kexec_args)
arg0:	PTR		0x0
arg1:	PTR		0x0
arg2:	PTR		0x0
arg3:	PTR		0x0
	.size	kexec_args,PTRSIZE*4

#ifdef CONFIG_SMP
/*
 * Secondary CPUs may have different kernel parameters in
 * their registers a0-a3. secondary_kexec_args[0..3] are used
 * to prepare register values.
 */
secondary_kexec_args:
	EXPORT(secondary_kexec_args)
s_arg0: PTR		0x0
s_arg1: PTR		0x0
s_arg2: PTR		0x0
s_arg3: PTR		0x0
	.size	secondary_kexec_args,PTRSIZE*4
kexec_flag:
	LONG		0x1

#endif

kexec_start_address:
	EXPORT(kexec_start_address)
	PTR		0x0
	.size		kexec_start_address, PTRSIZE

kexec_indirection_page:
	EXPORT(kexec_indirection_page)
	PTR		0
	.size		kexec_indirection_page, PTRSIZE

relocate_new_kernel_end:

relocate_new_kernel_size:
	EXPORT(relocate_new_kernel_size)
	PTR		relocate_new_kernel_end - relocate_new_kernel
	.size		relocate_new_kernel_size, PTRSIZE
