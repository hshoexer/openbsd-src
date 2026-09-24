/*	$OpenBSD$ */

/*
 * Copyright (c) 2025 Hans-Joerg Hoexer <hshoexer@genua.de>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/param.h>
#include <sys/types.h>
#include <sys/systm.h>

#include <machine/ghcb.h>
#include <machine/snp.h>

/*
 * Workaround for cache coherency CPU bug named CVE-2025-38560 in linux and
 * CVE-2024-36331 in OVMF.  It seems this can cause memory corruption even
 * without any attacker, corrupting page tables and causing panics while
 * booting.
 */
static void
snp_evict_cache(vaddr_t va, unsigned int npages)
{
	volatile uint8_t dummy;	/* Needs to be volatile. */
	uint8_t *ptr = (uint8_t *)va;
	unsigned int i;

	if (curcpu()->ci_feature_amdsev_ebx & CPUIDEBX_COHERENCY_SFW_NO)
		return;

	for (i = 0; i < npages; i++) {
		/*
		 * Reading from the first and last cache line of the page from
		 * the guest causes a flush of all cache-lines of that page
		 * and is cheaper than a complete clflush.
		 *
		 * dummy must be volatile to ensure the loads are not
		 * optimized away.
		 */
		dummy = ptr[i * PAGE_SIZE];
		dummy = ptr[i * PAGE_SIZE + PAGE_SIZE - 1];
	}
}

/*
 * _pvalidate
 *
 * pvalidate single 4K page.
 */
static __inline int
_pvalidate(vaddr_t va, uint32_t state)
{
	uint64_t	rax, rv;
	uint32_t	ecx, edx;
	uint8_t		unchanged = 0;

	rax = va;
	ecx = 0;	/* 4K page */
	if (state == SNP_PSC_PRIVATE)
		edx = 1;	/* set "validated" in RMP */
	else
		edx = 0;	/* clear "validated" RMP */

	__asm volatile(
		"	pvalidate				;"
		"	setc	%b0				;"
	    : "=qm" (unchanged), "=a" (rv)
	    : "a" (rax), "c" (ecx), "d" (edx)
	    : "memory", "cc");

	/*
	 * pvalidate was successful, however, the "validated" entry
	 * was already set to "state" (ie. no change).
	 */
	if (rv == 0 && unchanged)
		return (-1);

	if (state == SNP_PSC_PRIVATE)
		snp_evict_cache(va, 1);

	return (rv);
}

/*
 * pvalidate
 *
 * pvalidate range in 4K chunks.
 */
static int
pvalidate(paddr_t start, paddr_t end, uint32_t state)
{
	paddr_t		pa;
	int		error;

	for (pa = start; pa < end; pa += PAGE_SIZE) {
		if ((error = _pvalidate(PMAP_DIRECT_MAP(pa), state)) != 0)
			return (error);
	}

	return (0);
}

/*
 * snp_page_state_change
 *
 * Set the host physical page, that is assigned to our VM as guest
 * physical page to either "private" (ie. encrypted) or "shared" (ie.
 * unencrypted).
 *
 * When sharing a page with hypervisor, ie. a page shall be used
 * as bounce buffer, we require the following order of events:
 *
 * 1) Set "validate" in the RMP to 0 using pvalidate.
 * 2) Request the hypervisor to set "assigned" in the RMP to
 *    "hypervisor owned".
 * 3) Establish a mapping with the C-bit cleared; with this mapping
 *    the page can be accessed unencrypted.
 *
 * When claiming a page, ie. when freeing a bounce buffer page, we
 * require the following order:
 *
 * 1) Remove the mapping for unencrypted access.
 * 2) Request the hypervisor to set "assigned" to "guest owned"
 * 3) Set "validated" in the RMP to 1; this requires a mapping with
 *    C-bit set; we use the PMAP DIRECT mapping of that page, as
 *    these are always encrypted.
 */
static void
snp_page_state_change(paddr_t start, paddr_t end, int state)
{
	struct ghcb_psc	psc;
	paddr_t		pa;
	int		error;

	for (pa = start; pa < end; pa += PAGE_SIZE) {
		memset(&psc, 0, sizeof(psc));
		psc.psc_entry.gfn = pa >> PAGE_SHIFT;
		psc.psc_entry.operation = state;
		psc.psc_entry.pagesize = 0;	/* 4K page */

		if (state == SNP_PSC_SHARED &&
		    (error = pvalidate(pa, pa + PAGE_SIZE, state)) != 0)
			panic("pvalidate failed: %d", error);

		ghcb_psc_vmgexit(&psc);

		if (state == SNP_PSC_PRIVATE &&
		    (error = pvalidate(pa, pa + PAGE_SIZE, state)) != 0)
			panic("pvalidate failed: %d", error);
	}
}

static void
snp_claim(paddr_t start, paddr_t end)
{
	snp_page_state_change(start, end, SNP_PSC_PRIVATE);
}

static void
snp_rescind(paddr_t start, paddr_t end)
{
	snp_page_state_change(start, end, SNP_PSC_SHARED);
}

void
snp_claim_pages(struct pglist *mlist)
{
	struct vm_page *pg;

	if (!ISSET(cpu_sev_guestmode, SEV_STAT_SNP_ACTIVE))
		return;

	TAILQ_FOREACH(pg, mlist, pageq) {
		snp_claim(VM_PAGE_TO_PHYS(pg),
		    VM_PAGE_TO_PHYS(pg) + PAGE_SIZE);
	}
}

void
snp_rescind_pages(struct pglist *mlist)
{
	struct vm_page *pg;

	if (!ISSET(cpu_sev_guestmode, SEV_STAT_SNP_ACTIVE))
		return;

	TAILQ_FOREACH(pg, mlist, pageq) {
		snp_rescind(VM_PAGE_TO_PHYS(pg),
		    VM_PAGE_TO_PHYS(pg) + PAGE_SIZE);
	}
}
