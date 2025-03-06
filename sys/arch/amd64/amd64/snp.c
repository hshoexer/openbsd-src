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

#include <uvm/uvm_extern.h>

#include <machine/ghcb.h>
#include <machine/snp.h>

static void
_snp_page_state_change(paddr_t start, paddr_t end, int state)
{
	struct ghcb_psc	psc;
	paddr_t		pa;
	int		error;

	for (pa = start; pa < end; pa += PAGE_SIZE) {
		memset(&psc, 0, sizeof(psc));
		psc.psc_hdr.end_entry = 0;
		psc.psc_entry.cur_page = 0;
		psc.psc_entry.gfn = pa >> PAGE_SHIFT;
		psc.psc_entry.operation = state;
		psc.psc_entry.pagesize = 0;	/* 4K */

		if ((error = ghcb_psc_vmgexit(&psc, sizeof(psc))) != 0)
			panic("ghcb_psc_vmgexit() failed: %d", error);

		if ((error = pvalidate(PMAP_DIRECT_MAP(pa), state)) > 0)
			panic("pvalidate failed: %d", error);
	}
}

void
snp_claim(paddr_t start, paddr_t end)
{
	_snp_page_state_change(start, end, SNP_PSC_PRIVATE);
}

void
snp_rescind(paddr_t start, paddr_t end)
{
	_snp_page_state_change(start, end, SNP_PSC_SHARED);
}
