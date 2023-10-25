/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef __KVM_X86_VMX_COMMON_H
#define __KVM_X86_VMX_COMMON_H

#include <linux/kvm_host.h>

#include <asm/traps.h>

#include "posted_intr.h"
#include "mmu.h"
#include "vmcs.h"
#include "vmx_ops.h"
#include "x86.h"
#include "tdx.h"
#include "td_part.h"

#define VT_BUILD_VMCS_HELPERS(type, bits, tdbits)			   \
static __always_inline type vmread##bits(struct kvm_vcpu *vcpu,		   \
					 unsigned long field)		   \
{									   \
	if (unlikely(is_td_vcpu(vcpu))) {				   \
		if (KVM_BUG_ON(!is_debug_td(vcpu), vcpu->kvm))		   \
			return 0;					   \
		return td_vmcs_read##tdbits(to_tdx(vcpu), field);	   \
	} else if (unlikely(is_td_part_vcpu(vcpu)))				\
		return tdg_vmcs_read##tdbits(vcpu, field); 			\
	return vmcs_read##bits(field);					   \
}									   \
static __always_inline void vmwrite##bits(struct kvm_vcpu *vcpu,	   \
					  unsigned long field, type value) \
{									   \
	if (unlikely(is_td_vcpu(vcpu))) {				   \
		if (KVM_BUG_ON(!is_debug_td(vcpu), vcpu->kvm))		   \
			return;						   \
		return td_vmcs_write##tdbits(to_tdx(vcpu), field, value);  \
	} else if (unlikely(is_td_part_vcpu(vcpu))) 			\
		return tdg_vmcs_write##tdbits(vcpu, field, value);	\
	vmcs_write##bits(field, value);					   \
}

VT_BUILD_VMCS_HELPERS(u16, 16, 16);
VT_BUILD_VMCS_HELPERS(u32, 32, 32);
VT_BUILD_VMCS_HELPERS(u64, 64, 64);
VT_BUILD_VMCS_HELPERS(unsigned long, l, 64);

#define BUILD_CONTROLS_SHADOW(lname, uname, bits)				\
static inline void lname##_controls_set(struct vcpu_vmx *vmx, u##bits val)	\
{										\
	if (vmx->loaded_vmcs->controls_shadow.lname != val) {			\
		if (unlikely(is_td_part_vcpu(&vmx->vcpu))) {			\
			tdg_##lname##_controls_set(vmx, val);			\
		} else {							\
			vmwrite##bits(&vmx->vcpu, uname, val);			\
			vmx->loaded_vmcs->controls_shadow.lname = val;		\
		}								\
	} \
}										\
static inline u##bits __##lname##_controls_get(struct loaded_vmcs *vmcs)	\
{										\
	return vmcs->controls_shadow.lname;					\
}										\
static inline u##bits lname##_controls_get(struct vcpu_vmx *vmx)		\
{										\
	return __##lname##_controls_get(vmx->loaded_vmcs);			\
}										\
static inline void lname##_controls_setbit(struct vcpu_vmx *vmx, u##bits val)	\
{										\
	lname##_controls_set(vmx, lname##_controls_get(vmx) | val);		\
}										\
static inline void lname##_controls_clearbit(struct vcpu_vmx *vmx, u##bits val)	\
{										\
	lname##_controls_set(vmx, lname##_controls_get(vmx) & ~val);		\
}
BUILD_CONTROLS_SHADOW(vm_entry, VM_ENTRY_CONTROLS, 32)
BUILD_CONTROLS_SHADOW(vm_exit, VM_EXIT_CONTROLS, 32)
BUILD_CONTROLS_SHADOW(pin, PIN_BASED_VM_EXEC_CONTROL, 32)
BUILD_CONTROLS_SHADOW(exec, CPU_BASED_VM_EXEC_CONTROL, 32)
BUILD_CONTROLS_SHADOW(secondary_exec, SECONDARY_VM_EXEC_CONTROL, 32)
BUILD_CONTROLS_SHADOW(tertiary_exec, TERTIARY_VM_EXEC_CONTROL, 64)

static __always_inline void vmcs_clear_bits(struct kvm_vcpu *vcpu, unsigned long field, u32 mask)
{
	BUILD_BUG_ON_MSG(__builtin_constant_p(field) && ((field) & 0x6000) == 0x2000,
			 "vmcs_clear_bits does not support 64-bit fields");
	if (kvm_is_using_evmcs())
		return evmcs_write32(field, evmcs_read32(field) & ~mask);

	if (enable_td_part)
		return tdg_vmcs_write32(vcpu, field, tdg_vmcs_read32(vcpu, field) & ~mask);

	__vmcs_writel(field, __vmcs_readl(field) & ~mask);
}

static __always_inline void vmcs_set_bits(struct kvm_vcpu *vcpu, unsigned long field, u32 mask)
{
	BUILD_BUG_ON_MSG(__builtin_constant_p(field) && ((field) & 0x6000) == 0x2000,
			 "vmcs_set_bits does not support 64-bit fields");
	if (kvm_is_using_evmcs())
		return evmcs_write32(field, evmcs_read32(field) | mask);

	if (enable_td_part)
		return tdg_vmcs_write32(vcpu, field, tdg_vmcs_read32(vcpu, field) | mask);

	__vmcs_writel(field, __vmcs_readl(field) | mask);
}

struct kvm_vmx_segment_field {
	unsigned int selector;
	unsigned int base;
	unsigned int limit;
	unsigned int ar_bytes;
};

extern const struct kvm_vmx_segment_field kvm_vmx_segment_fields[];
void vmx_do_nmi_irqoff(void);
void vmx_handle_nm_fault_irqoff(struct kvm_vcpu *vcpu);
void vmx_handle_exception_irqoff(struct kvm_vcpu *vcpu, u32 intr_info);
void vmx_handle_external_interrupt_irqoff(struct kvm_vcpu *vcpu, u32 intr_info);

static inline int __vmx_handle_ept_violation(struct kvm_vcpu *vcpu, gpa_t gpa,
					     unsigned long exit_qualification,
					     int err_page_level)
{
	u64 error_code;

	/* Is it a read fault? */
	error_code = (exit_qualification & EPT_VIOLATION_ACC_READ)
		     ? PFERR_USER_MASK : 0;
	/* Is it a write fault? */
	error_code |= (exit_qualification & EPT_VIOLATION_ACC_WRITE)
		      ? PFERR_WRITE_MASK : 0;
	/* Is it a fetch fault? */
	error_code |= (exit_qualification & EPT_VIOLATION_ACC_INSTR)
		      ? PFERR_FETCH_MASK : 0;
	/* ept page table entry is present? */
	error_code |= (exit_qualification & EPT_VIOLATION_RWX_MASK)
		      ? PFERR_PRESENT_MASK : 0;

	error_code |= (exit_qualification & EPT_VIOLATION_GVA_TRANSLATED) != 0 ?
	       PFERR_GUEST_FINAL_MASK : PFERR_GUEST_PAGE_MASK;

	if (kvm_is_private_gpa(vcpu->kvm, gpa))
		error_code |= PFERR_GUEST_ENC_MASK;

	if (err_page_level > PG_LEVEL_NONE)
		error_code |= (err_page_level << PFERR_LEVEL_START_BIT) & PFERR_LEVEL_MASK;

	return kvm_mmu_page_fault(vcpu, gpa, error_code, NULL, 0);
}

static inline void kvm_vcpu_trigger_posted_interrupt(struct kvm_vcpu *vcpu,
						     int pi_vec)
{
#ifdef CONFIG_SMP
	if (vcpu->mode == IN_GUEST_MODE) {
		/*
		 * The vector of the virtual has already been set in the PIR.
		 * Send a notification event to deliver the virtual interrupt
		 * unless the vCPU is the currently running vCPU, i.e. the
		 * event is being sent from a fastpath VM-Exit handler, in
		 * which case the PIR will be synced to the vIRR before
		 * re-entering the guest.
		 *
		 * When the target is not the running vCPU, the following
		 * possibilities emerge:
		 *
		 * Case 1: vCPU stays in non-root mode. Sending a notification
		 * event posts the interrupt to the vCPU.
		 *
		 * Case 2: vCPU exits to root mode and is still runnable. The
		 * PIR will be synced to the vIRR before re-entering the guest.
		 * Sending a notification event is ok as the host IRQ handler
		 * will ignore the spurious event.
		 *
		 * Case 3: vCPU exits to root mode and is blocked. vcpu_block()
		 * has already synced PIR to vIRR and never blocks the vCPU if
		 * the vIRR is not empty. Therefore, a blocked vCPU here does
		 * not wait for any requested interrupts in PIR, and sending a
		 * notification event also results in a benign, spurious event.
		 */

		if (vcpu != kvm_get_running_vcpu())
			__apic_send_IPI_mask(get_cpu_mask(vcpu->cpu), pi_vec);
		return;
	}
#endif
	/*
	 * The vCPU isn't in the guest; wake the vCPU in case it is blocking,
	 * otherwise do nothing as KVM will grab the highest priority pending
	 * IRQ via ->sync_pir_to_irr() in vcpu_enter_guest().
	 */
	kvm_vcpu_wake_up(vcpu);
}

/*
 * Send interrupt to vcpu via posted interrupt way.
 * 1. If target vcpu is running(non-root mode), send posted interrupt
 * notification to vcpu and hardware will sync PIR to vIRR atomically.
 * 2. If target vcpu isn't running(root mode), kick it to pick up the
 * interrupt from PIR in next vmentry.
 */
static inline void __vmx_deliver_posted_interrupt(struct kvm_vcpu *vcpu,
						  struct pi_desc *pi_desc, int vector)
{
	if (pi_test_and_set_pir(vector, pi_desc))
		return;

	/* If a previous notification has sent the IPI, nothing to do.  */
	if (pi_test_and_set_on(pi_desc))
		return;

	/*
	 * The implied barrier in pi_test_and_set_on() pairs with the smp_mb_*()
	 * after setting vcpu->mode in vcpu_enter_guest(), thus the vCPU is
	 * guaranteed to see PID.ON=1 and sync the PIR to IRR if triggering a
	 * posted interrupt "fails" because vcpu->mode != IN_GUEST_MODE.
	 */
	kvm_vcpu_trigger_posted_interrupt(vcpu, POSTED_INTR_VECTOR);
}

static inline u32 __vmx_get_interrupt_shadow(struct kvm_vcpu *vcpu)
{
	u32 interruptibility;
	int ret = 0;

	interruptibility = vmread32(vcpu, GUEST_INTERRUPTIBILITY_INFO);
	if (interruptibility & GUEST_INTR_STATE_STI)
		ret |= KVM_X86_SHADOW_INT_STI;
	if (interruptibility & GUEST_INTR_STATE_MOV_SS)
		ret |= KVM_X86_SHADOW_INT_MOV_SS;

	return ret;
}

static inline void vmx_decode_ar_bytes(struct kvm_segment *var, u32 ar)
{
	var->unusable = (ar >> 16) & 1;
	var->type = ar & 15;
	var->s = (ar >> 4) & 1;
	var->dpl = (ar >> 5) & 3;
	/*
	 * Some userspaces do not preserve unusable property. Since usable
	 * segment has to be present according to VMX spec we can use present
	 * property to amend userspace bug by making unusable segment always
	 * nonpresent. vmx_segment_access_rights() already marks nonpresent
	 * segment as unusable.
	 */
	var->present = !var->unusable;
	var->avl = (ar >> 12) & 1;
	var->l = (ar >> 13) & 1;
	var->db = (ar >> 14) & 1;
	var->g = (ar >> 15) & 1;

}

static inline unsigned long vmx_mask_out_guest_rip(struct kvm_vcpu *vcpu,
						   unsigned long orig_rip,
						   unsigned long new_rip)
{
	/*
	 * We need to mask out the high 32 bits of RIP if not in 64-bit
	 * mode, but just finding out that we are in 64-bit mode is
	 * quite expensive.  Only do it if there was a carry.
	 */
	if (unlikely(((new_rip ^ orig_rip) >> 31) == 3) &&
	    !is_64_bit_mode(vcpu))
		return (u32)new_rip;
	return new_rip;
}

static inline bool vmx_has_waitpkg(struct vcpu_vmx *vmx)
{
	return secondary_exec_controls_get(vmx) &
		SECONDARY_EXEC_ENABLE_USR_WAIT_PAUSE;
}

static inline bool is_unrestricted_guest(struct kvm_vcpu *vcpu)
{
	return enable_unrestricted_guest && (!is_guest_mode(vcpu) ||
	    (secondary_exec_controls_get(to_vmx(vcpu)) &
	    SECONDARY_EXEC_UNRESTRICTED_GUEST));
}

bool __vmx_guest_state_valid(struct kvm_vcpu *vcpu);
static inline bool vmx_guest_state_valid(struct kvm_vcpu *vcpu)
{
	return is_unrestricted_guest(vcpu) || __vmx_guest_state_valid(vcpu);
}

#endif /* __KVM_X86_VMX_COMMON_H */
