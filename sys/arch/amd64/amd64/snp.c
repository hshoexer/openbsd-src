/*	$OpenBSD:$	*/

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

#include <uvm/uvm.h>

#include <machine/ghcb.h>
#include <machine/snp.h>


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

	return (rv);
}

/*
 * pvalidate
 *
 * pvalidate range in 4K chunks.
 */
static int
pvalidate(paddr_t start, paddr_t end, uint32_t state, int early)
{
	paddr_t		pa;
	size_t		left;
	int		error;

	left = end - start;
	for (pa = start; pa < end; pa += PAGE_SIZE, left -= PAGE_SIZE) {
		if ((error = _pvalidate(PMAP_DIRECT_MAP(pa), state)) != 0) {
			/*
                         * During early bootstrap it is acceptable,
                         * when we initially claim a page, that is
                         * already private.  Therefore the valid
                         * flags was not changed in the RMP.
			 *
			 * However, after bootstrap, this may not happen.
			 */
			if (early && state == SNP_PSC_PRIVATE && error == -1)
				continue;
			return (error);
		}
	}

	return (0);
}

/*
 * _snp_page_state_change
 *
 * Set the host physical page, that is assigned to our VM as guest
 * physical page to either "private" (ie. encrypted) or "shared" (ie.
 * unencrypted).
 *
 * When sharing a page with hypervisor, ie. a page shall be used
 * as bounce buffer, we require the following order of events:
 *
 * 1) Set "validate" in the RMP to 0 using pvalidate.
 * 2) Request the hypervisor to set "assigend" in the RMP to
 *    "hypervisor owned".
 * 3) Establish a mapping with the C-bit cleared; with this mapping
 *    the page can be accessed unencrypted.
 *
 * When claiming a page, ie. when freeing a bounce buffer page, we
 * require the following order:
 *
 * 1) Remove the mapping for unencryped access.
 * 2) Request the hypervisor to set "assigend" to "guest owned"
 * 3) Set "validated" in the RMP to 1; this requires a mapping with
 *    C-bit set; we use the PMAP DIRECT mapping of that page, as
 *    these are always encrypted.
 */
static void
_snp_page_state_change(paddr_t start, paddr_t end, int state, int early)
{
	struct ghcb_psc	psc;
	paddr_t		pa;
	int		error;
	size_t		sz, left;

	left = end - start;
	for (pa = start; pa < end; pa += sz, left -= sz) {
		if ((pa & ~L2_FRAME) == 0 && left >= NBPD_L2)
			sz = NBPD_L2;
		else
			sz = NBPD_L1;
		memset(&psc, 0, sizeof(psc));
		psc.psc_hdr.end_entry = 0;
		psc.psc_entry.cur_page = 0;
		psc.psc_entry.gfn = pa >> PAGE_SHIFT;
		psc.psc_entry.operation = state;
		psc.psc_entry.pagesize = (sz == NBPD_L2);

		if (state == SNP_PSC_SHARED &&
		    (error = pvalidate(pa, pa + sz, state, early)) != 0) {
			panic("pvalidate failed: %d", error);
		}

		if ((error = ghcb_psc_vmgexit(&psc, sizeof(psc))) != 0)
			panic("ghcb_psc_vmgexit() failed: %d", error);

		if (state == SNP_PSC_PRIVATE &&
		    (error =  pvalidate(pa, pa + sz, state, early)) != 0) {
			panic("pvalidate failed: %d", error);
		}
	}
}

static void
_snp_claim(paddr_t start, paddr_t end, int early)
{
	_snp_page_state_change(start, end, SNP_PSC_PRIVATE, early);
}

static void
_snp_rescind(paddr_t start, paddr_t end, int early)
{
	_snp_page_state_change(start, end, SNP_PSC_SHARED, early);
}


void
snp_claim_early(paddr_t start, paddr_t end)
{
	if (!ISSET(cpu_sev_guestmode, SEV_STAT_SNP_ACTIVE))
		return;

	return _snp_claim(start, end, 1);
}

void
snp_claim_pages(struct pglist *mlist)
{
	struct vm_page *pg;

	if (!ISSET(cpu_sev_guestmode, SEV_STAT_SNP_ACTIVE))
		return;

	TAILQ_FOREACH(pg, mlist, pageq)
		_snp_claim(VM_PAGE_TO_PHYS(pg),
		    VM_PAGE_TO_PHYS(pg) + PAGE_SIZE, 0);
}

void
snp_rescind_pages(struct pglist *mlist)
{
	struct vm_page *pg;

	if (!ISSET(cpu_sev_guestmode, SEV_STAT_SNP_ACTIVE))
		return;

	TAILQ_FOREACH(pg, mlist, pageq)
		_snp_rescind(VM_PAGE_TO_PHYS(pg),
		    VM_PAGE_TO_PHYS(pg) + PAGE_SIZE, 0);
}
