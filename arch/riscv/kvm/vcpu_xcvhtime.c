// SPDX-License-Identifier: GPL-2.0
/*
 * CORE-V hidden time (xcvhtime): VM-exit time hiding.
 *
 * The CSRs are saved and restored in-kernel only (no ONE_REG). HHAE is
 * 0 by default. The test-only debugfs knob kvm/xcvhtime_hide enters the
 * guest with HHAE=1, so every exit handled in-kernel is hidden; WFI and
 * vcpu_put (exit to userspace, preemption) clear it, so sleep and
 * descheduled time stay visible.
 */

#include <linux/debugfs.h>
#include <linux/kvm_host.h>
#include <asm/csr.h>
#include <asm/cpufeature.h>
#include <asm/kvm_vcpu_xcvhtime.h>

static bool xcvhtime_hide;

static bool xcvhtime_available(void)
{
	return riscv_has_extension_unlikely(RISCV_ISA_EXT_XCVHTIME);
}

void kvm_riscv_xcvhtime_debugfs_init(void)
{
	struct dentry *dir;

	if (!xcvhtime_available())
		return;

	dir = debugfs_lookup("kvm", NULL);
	if (IS_ERR_OR_NULL(dir))
		return;
	debugfs_create_bool("xcvhtime_hide", 0644, dir, &xcvhtime_hide);
	dput(dir);
}

void kvm_riscv_xcvhtime_enable(void)
{
	if (!xcvhtime_available())
		return;

	/* mip[24] is only pending in V=1: it always traps out of the guest */
	csr_set(CSR_IE, BIT(IRQ_XCV_HVTI));
	/* Guest instret reads return vinstret = minstret - hhidden_instret */
	csr_set(CSR_HCOUNTEREN, BIT(CSR_INSTRET - CSR_CYCLE));
}

void kvm_riscv_xcvhtime_disable(void)
{
	if (xcvhtime_available())
		csr_clear(CSR_IE, BIT(IRQ_XCV_HVTI));
}

void kvm_riscv_vcpu_xcvhtime_reset(struct kvm_vcpu *vcpu)
{
	struct kvm_vcpu_xcvhtime *h = &vcpu->arch.xcvhtime;

	memset(h, 0, sizeof(*h));
	h->hvtimecmp = -1ULL;
	h->unhidden = true;
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
	csr_write(CSR_HEXIT_INSTRET, h->hexit_instret);
	csr_write(CSR_HHIDDEN_INSTRET, h->hhidden_instret);
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
	h->hexit_instret = csr_read(CSR_HEXIT_INSTRET);
	h->hhidden_instret = csr_read(CSR_HHIDDEN_INSTRET);

	/* Exit to userspace or preemption: not hidden (section 8.2) */
	h->htimectl &= ~HTIMECTL_HHAE;
	h->unhidden = true;

	/* Nothing hidden or armed while no vCPU is loaded */
	csr_write(CSR_HTIMECTL, 0);
	csr_write(CSR_HVTIMECMP, -1UL);
}

/* Called with interrupts off, right before entering the guest */
void kvm_riscv_vcpu_xcvhtime_enter(struct kvm_vcpu *vcpu)
{
	struct kvm_vcpu_xcvhtime *h = &vcpu->arch.xcvhtime;

	if (!xcvhtime_available())
		return;

	if (!READ_ONCE(xcvhtime_hide)) {
		if (h->htimectl & HTIMECTL_HHAE)
			kvm_riscv_vcpu_xcvhtime_unhide(vcpu);
		return;
	}

	/*
	 * HHAE must already be 1 when the exit happens, so it is set on every
	 * entry. After an exit that was not hidden, hexit_* still hold that
	 * exit's time: rebase them to now so the entry hides nothing.
	 */
	if (h->unhidden) {
		h->hexit_time = csr_read(CSR_TIME);
		h->hexit_cycle = csr_read(CSR_CYCLE);
		h->hexit_instret = csr_read(CSR_INSTRET);
		csr_write(CSR_HEXIT_TIME, h->hexit_time);
		csr_write(CSR_HEXIT_CYCLE, h->hexit_cycle);
		csr_write(CSR_HEXIT_INSTRET, h->hexit_instret);
		h->unhidden = false;
	}
	if (!(h->htimectl & HTIMECTL_HHAE)) {
		h->htimectl |= HTIMECTL_HHAE;
		csr_write(CSR_HTIMECTL, h->htimectl);
	}
}

/* Make the last exit visible: the guest clock runs live again (section 8.1) */
void kvm_riscv_vcpu_xcvhtime_unhide(struct kvm_vcpu *vcpu)
{
	struct kvm_vcpu_xcvhtime *h = &vcpu->arch.xcvhtime;

	if (!xcvhtime_available())
		return;

	h->htimectl &= ~HTIMECTL_HHAE;
	csr_write(CSR_HTIMECTL, h->htimectl);
	h->unhidden = true;
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

int kvm_riscv_vcpu_xcvhtime_hvti(struct kvm_vcpu *vcpu)
{
	/* Level-triggered: disarm, or it fires again on guest entry */
	csr_write(CSR_HVTIMECMP, -1UL);
	vcpu->arch.xcvhtime.hvtimecmp = -1ULL;
	vcpu->stat.hvti_exit_stat++;

	return 1;
}
