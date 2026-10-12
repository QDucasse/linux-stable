// SPDX-License-Identifier: GPL-2.0
/*
 * CORE-V hidden time (xcvhtime): VM-exit time hiding.
 *
 * The CSRs are saved and restored in-kernel only (no ONE_REG). HHAE is
 * left at 0, so the guest clock only differs from time + htimedelta by
 * whatever hhidden_time holds.
 */

#include <linux/kvm_host.h>
#include <asm/csr.h>
#include <asm/cpufeature.h>
#include <asm/kvm_vcpu_xcvhtime.h>

static bool xcvhtime_available(void)
{
	return riscv_has_extension_unlikely(RISCV_ISA_EXT_XCVHTIME);
}

void kvm_riscv_vcpu_xcvhtime_reset(struct kvm_vcpu *vcpu)
{
	struct kvm_vcpu_xcvhtime *h = &vcpu->arch.xcvhtime;

	memset(h, 0, sizeof(*h));
	h->hvtimecmp = -1ULL;
}

void kvm_riscv_vcpu_xcvhtime_load(struct kvm_vcpu *vcpu)
{
	struct kvm_vcpu_xcvhtime *h = &vcpu->arch.xcvhtime;

	if (!xcvhtime_available())
		return;

	csr_write(CSR_HEXIT_TIME, h->hexit_time);
	csr_write(CSR_HHIDDEN_TIME, h->hhidden_time);
	csr_write(CSR_HVTIMECMP, h->hvtimecmp);
	csr_write(CSR_HEXIT_CYCLE, h->hexit_cycle);
	csr_write(CSR_HHIDDEN_CYCLE, h->hhidden_cycle);
	csr_write(CSR_HTIMECTL, h->htimectl);
}

void kvm_riscv_vcpu_xcvhtime_put(struct kvm_vcpu *vcpu)
{
	struct kvm_vcpu_xcvhtime *h = &vcpu->arch.xcvhtime;

	if (!xcvhtime_available())
		return;

	h->htimectl = csr_read(CSR_HTIMECTL);
	h->hexit_time = csr_read(CSR_HEXIT_TIME);
	h->hhidden_time = csr_read(CSR_HHIDDEN_TIME);
	h->hvtimecmp = csr_read(CSR_HVTIMECMP);
	h->hexit_cycle = csr_read(CSR_HEXIT_CYCLE);
	h->hhidden_cycle = csr_read(CSR_HHIDDEN_CYCLE);

	/* Nothing hidden or armed while no vCPU is loaded */
	csr_write(CSR_HTIMECTL, 0);
	csr_write(CSR_HVTIMECMP, -1UL);
}

void kvm_riscv_vcpu_xcvhtime_sync(struct kvm_vcpu *vcpu)
{
	/* hhidden_time grows on each guest entry with HHAE=1 */
	if (xcvhtime_available())
		vcpu->arch.xcvhtime.hhidden_time = csr_read(CSR_HHIDDEN_TIME);
}

u64 kvm_riscv_vcpu_xcvhtime_hidden(struct kvm_vcpu *vcpu)
{
	return vcpu->arch.xcvhtime.hhidden_time;
}
