/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_SHARED_TDX_H
#define _ASM_X86_SHARED_TDX_H

#include <linux/bits.h>
#include <linux/types.h>

#define TDX_HYPERCALL_STANDARD  0

#define TDX_CPUID_LEAF_ID	0x21
#define TDX_IDENT		"IntelTDX    "

/* TDX module Call Leaf IDs */
#define TDG_VP_VMCALL			0
#define TDG_VP_INFO			1
#define TDG_EXTEND_RTMR			2
#define TDG_VP_VEINFO_GET		3
#define TDG_MR_REPORT			4
#define TDG_MEM_PAGE_ACCEPT		6
#define TDG_VM_WR			8
#define TDG_SYS_RD			11
#define TDG_SYS_RDALL			12
#define TDG_VERIFYREPORT		22
#define TDG_DEVIF_VALIDATE		66
#define TDG_DEVIF_READ			67
#define TDG_DEVIF_REQUEST		68
#define TDG_DEVIF_RESPONSE		69
#define TDG_DMAR_ACCEPT			70
#define TDG_MMIO_ACCEPT			71

#define TDG_SYS_RD_SUPPORTED   BIT(0)
/*
 * TDX module metadata identifiers
 */
#define TDX_MD_FEATURES0		0x0A00000300000008
#define TDX_FEATURES0_TD_PART		BIT(7)

/* TDCS fields. To be used by TDG.VM.WR and TDG.VM.RD module calls */
#define TDCS_NOTIFY_ENABLES		0x9100000000000010

/* TDX hypercall Leaf IDs */
#define TDVMCALL_MAP_GPA		0x10001
#define TDVMCALL_GET_QUOTE		0x10002
#define TDVMCALL_REPORT_FATAL_ERROR	0x10003
#define TDVMCALL_SETUP_NOTIFY_INTR	0x10004
#define TDVMCALL_SERVICE		0x10005

#define TDVMCALL_STATUS_RETRY		1

/*
 * Bitmasks of exposed registers (with VMM).
 */
#define TDX_RDX		BIT(2)
#define TDX_RBX		BIT(3)
#define TDX_RSI		BIT(6)
#define TDX_RDI		BIT(7)
#define TDX_R8		BIT(8)
#define TDX_R9		BIT(9)
#define TDX_R10		BIT(10)
#define TDX_R11		BIT(11)
#define TDX_R12		BIT(12)
#define TDX_R13		BIT(13)
#define TDX_R14		BIT(14)
#define TDX_R15		BIT(15)

/*
 * These registers are clobbered to hold arguments for each
 * TDVMCALL. They are safe to expose to the VMM.
 * Each bit in this mask represents a register ID. Bit field
 * details can be found in TDX GHCI specification, section
 * titled "TDCALL [TDG.VP.VMCALL] leaf".
 */
#define TDVMCALL_EXPOSE_REGS_MASK	\
	(TDX_RDX | TDX_RBX | TDX_RSI | TDX_RDI | TDX_R8  | TDX_R9  | \
	 TDX_R10 | TDX_R11 | TDX_R12 | TDX_R13 | TDX_R14 | TDX_R15)

/* TDX supported page sizes from the TDX module ABI. */
#define TDX_PS_4K	0
#define TDX_PS_2M	1
#define TDX_PS_1G	2
#define TDX_PS_NR	(TDX_PS_1G + 1)

#define TDCALL_RETRY_MAX	10000
#define TDCALL_STATUS_MASK	0xFFFFFFFF00000000ULL

#define TDX_OPERAND_BUSY		0x8000020000000000ULL
#define TDX_OPERAND_BUSY_HOST_PRIORITY	0x8000020400000000ULL

#ifndef __ASSEMBLY__

/*
 * Used in __tdcall*() to gather the input/output registers' values of the
 * TDCALL instruction when requesting services from the TDX module. This is a
 * software only structure and not part of the TDX module/VMM ABI
 *
 * Note those *_unused are not used by the TDX_MODULE_CALL assembly.
 * The layout of this structure also matches KVM's kvm_vcpu_arch::regs[]
 * layout, which follows the "register index" order of x86 GPRs.  KVM
 * then can simply type cast kvm_vcpu_arch::regs[] to this structure to
 * avoid the extra memory copy between two structures when making
 * TDH.VP.ENTER SEAMCALL.
 */
struct tdx_module_args {
	u64 rax_unused;
	u64 rcx;
	u64 rdx;
	u64 rbx;
	u64 rsp_unused;
	u64 rbp_unused;
	u64 rsi;
	u64 rdi;
	u64 r8;
	u64 r9;
	u64 r10;
	u64 r11;
	u64 r12;
	u64 r13;
	u64 r14;
	u64 r15;
};

/* Used to communicate with the TDX module */
u64 __tdcall(u64 fn, struct tdx_module_args *args);
u64 __tdcall_ret(u64 fn, struct tdx_module_args *args);
u64 __tdcall_saved(u64 fn, struct tdx_module_args *args);
u64 __tdcall_saved_ret(u64 fn, struct tdx_module_args *args);

static inline u64 __tdcall_common(u64 fn, struct tdx_module_args *args,
				  bool tdcall_ret, bool tdcall_saved)
{
	u64 err, err_masked, retries = 0;

	do {
		if (tdcall_ret) {
			if (tdcall_saved)
				err = __tdcall_saved_ret(fn, args);
			else
				err = __tdcall_ret(fn, args);
		} else {
			if (tdcall_saved)
				err = __tdcall_saved(fn, args);
			else
				err = __tdcall(fn, args);

		}

		if (likely(!err) || retries++ > TDCALL_RETRY_MAX)
			break;

		err_masked = err & TDCALL_STATUS_MASK;
	} while (err_masked == TDX_OPERAND_BUSY ||
		 err_masked == TDX_OPERAND_BUSY_HOST_PRIORITY);

	return err;
}

static inline u64 tdcall(u64 fn, struct tdx_module_args *args)
{
	return __tdcall_common(fn, args, false, false);
}

static inline u64 tdcall_ret(u64 fn, struct tdx_module_args *args)
{
	return __tdcall_common(fn, args, true, false);
}

static inline u64 tdcall_saved(u64 fn, struct tdx_module_args *args)
{
	return __tdcall_common(fn, args, false, true);
}

static inline u64 tdcall_saved_ret(u64 fn, struct tdx_module_args *args)
{
	return __tdcall_common(fn, args, true, true);
}

/* Used to request services from the VMM */
u64 __tdx_hypercall(struct tdx_module_args *args);

/*
 * Wrapper for standard use of __tdx_hypercall with no output aside from
 * return code.
 */
static inline u64 _tdx_hypercall(u64 fn, u64 r12, u64 r13, u64 r14, u64 r15)
{
	struct tdx_module_args args = {
		.r10 = TDX_HYPERCALL_STANDARD,
		.r11 = fn,
		.r12 = r12,
		.r13 = r13,
		.r14 = r14,
		.r15 = r15,
	};

	return __tdx_hypercall(&args);
}


/* Called from __tdx_hypercall() for unrecoverable failure */
void __tdx_hypercall_failed(void);

bool tdx_accept_memory(phys_addr_t start, phys_addr_t end);

/*
 * The TDG.VP.VMCALL-Instruction-execution sub-functions are defined
 * independently from but are currently matched 1:1 with VMX EXIT_REASONs.
 * Reusing the KVM EXIT_REASON macros makes it easier to connect the host and
 * guest sides of these calls.
 */
static __always_inline u64 hcall_func(u64 exit_reason)
{
        return exit_reason;
}

#ifdef CONFIG_INTEL_TDX_GUEST
bool is_td_partitioning_supported(void);
#else
static inline bool is_td_partitioning_supported(void) { return false; }
#endif

#endif /* !__ASSEMBLY__ */
#endif /* _ASM_X86_SHARED_TDX_H */
