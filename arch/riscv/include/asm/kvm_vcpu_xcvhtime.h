/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * CORE-V hidden time (xcvhtime): per-vCPU state of the six hidden-time CSRs.
 */

#ifndef __KVM_VCPU_RISCV_XCVHTIME_H
#define __KVM_VCPU_RISCV_XCVHTIME_H

#include <linux/types.h>

struct kvm_vcpu;

struct kvm_vcpu_xcvhtime {
	u64 htimectl;
	u64 hexit_time;
	u64 hhidden_time;
	u64 hvtimecmp;
	u64 hexit_cycle;
	u64 hhidden_cycle;
};

void kvm_riscv_xcvhtime_enable(void);
void kvm_riscv_xcvhtime_disable(void);
void kvm_riscv_vcpu_xcvhtime_reset(struct kvm_vcpu *vcpu);
void kvm_riscv_vcpu_xcvhtime_load(struct kvm_vcpu *vcpu);
void kvm_riscv_vcpu_xcvhtime_put(struct kvm_vcpu *vcpu);
void kvm_riscv_vcpu_xcvhtime_sync(struct kvm_vcpu *vcpu);
u64 kvm_riscv_vcpu_xcvhtime_hidden(struct kvm_vcpu *vcpu);
int kvm_riscv_vcpu_xcvhtime_hvti(struct kvm_vcpu *vcpu);

#endif
