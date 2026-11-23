/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Compatibility definitions for the MTK IOMMU debug page-table walker.
 *
 * Linux 6.18 moved the arm_lpae_io_pgtable internals and the old
 * io_pgtable_walk_data API out of the public io-pgtable-arm.h into
 * drivers/iommu/io-pgtable-arm.c.  The MTK debug module still walks the
 * tables directly, so mirror the private layout here.  The struct layout
 * must stay in sync with kernel/common drivers/iommu/io-pgtable-arm.c.
 */
#ifndef __MTK_IO_PGTABLE_ARM_COMPAT_H
#define __MTK_IO_PGTABLE_ARM_COMPAT_H

#include <linux/io-pgtable.h>
#include "io-pgtable-arm.h"

#define ARM_LPAE_MAX_LEVELS		4

#define ARM_LPAE_LVL_SHIFT(l, d)					\
	(((ARM_LPAE_MAX_LEVELS - (l)) * (d)->bits_per_level) +		\
	 ilog2(sizeof(arm_lpae_iopte)))
#define ARM_LPAE_GRANULE(d)						\
	(sizeof(arm_lpae_iopte) << (d)->bits_per_level)
#define ARM_LPAE_PGD_SIZE(d)						\
	(sizeof(arm_lpae_iopte) << (d)->pgd_bits)
#define ARM_LPAE_PTES_PER_TABLE(d)					\
	(ARM_LPAE_GRANULE(d) >> ilog2(sizeof(arm_lpae_iopte)))
#define ARM_LPAE_PGD_IDX(l, d)						\
	((l) == (d)->start_level ? (d)->pgd_bits - (d)->bits_per_level : 0)
#define ARM_LPAE_LVL_IDX(a, l, d)					\
	(((u64)(a) >> ARM_LPAE_LVL_SHIFT(l, d)) &			\
	 ((1 << ((d)->bits_per_level + ARM_LPAE_PGD_IDX(l, d))) - 1))
#define ARM_LPAE_BLOCK_SIZE(l, d)	(1ULL << ARM_LPAE_LVL_SHIFT(l, d))

#define ARM_LPAE_PTE_TYPE_SHIFT		0
#define ARM_LPAE_PTE_TYPE_MASK		0x3
#define ARM_LPAE_PTE_TYPE_BLOCK		1
#define ARM_LPAE_PTE_TYPE_TABLE		3
#define ARM_LPAE_PTE_TYPE_PAGE		3
#define ARM_LPAE_PTE_ADDR_MASK		GENMASK_ULL(47, 12)
#define ARM_LPAE_PTE_VALID		(((arm_lpae_iopte)1) << 0)

/* 6.18 removed the old walk quirk; keep the branch false. */
#ifndef IO_PGTABLE_QUIRK_UNMAP_INVAL
#define IO_PGTABLE_QUIRK_UNMAP_INVAL	0
#endif

struct arm_lpae_io_pgtable {
	struct io_pgtable	iop;
	int			pgd_bits;
	int			start_level;
	int			bits_per_level;
	void			*pgd;
};

struct io_pgtable_walk_common {
	void (*visit_leaf)(phys_addr_t paddr, size_t size,
			   struct io_pgtable_walk_common *data,
			   void *wd);
	void *data;
};

struct io_pgtable_walk_data {
	struct io_pgtable		*iop;
	struct io_pgtable_walk_common	*data;
	int (*visit)(struct io_pgtable_walk_data *walk_data, int lvl,
		     arm_lpae_iopte *ptep, size_t size);
	unsigned long			flags;
	u64				addr;
	const u64			end;
};

#define io_pgtable_to_data(x)						\
	container_of((x), struct arm_lpae_io_pgtable, iop)
#define io_pgtable_ops_to_data(x)					\
	io_pgtable_to_data(io_pgtable_ops_to_pgtable(x))

static inline u64 iopte_type(arm_lpae_iopte pte)
{
	return (pte >> ARM_LPAE_PTE_TYPE_SHIFT) & ARM_LPAE_PTE_TYPE_MASK;
}

static inline bool iopte_leaf(arm_lpae_iopte pte, int lvl,
			      enum io_pgtable_fmt fmt)
{
	if (lvl == (ARM_LPAE_MAX_LEVELS - 1) && fmt != ARM_MALI_LPAE)
		return iopte_type(pte) == ARM_LPAE_PTE_TYPE_PAGE;

	return iopte_type(pte) == ARM_LPAE_PTE_TYPE_BLOCK;
}

static inline bool iopte_table(arm_lpae_iopte pte, int lvl)
{
	if (lvl == (ARM_LPAE_MAX_LEVELS - 1))
		return false;
	return iopte_type(pte) == ARM_LPAE_PTE_TYPE_TABLE;
}

static inline bool iopte_valid(arm_lpae_iopte pte)
{
	return !!(pte & ARM_LPAE_PTE_VALID);
}

#endif /* __MTK_IO_PGTABLE_ARM_COMPAT_H */
