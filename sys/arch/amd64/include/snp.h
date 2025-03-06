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

#ifndef _MACHINE_SNP_H_
#define _MACHINE_SNP_H_

#include <uvm/uvm.h>

#define SNP_PSC_PRIVATE		0x0001
#define SNP_PSC_SHARED		0x0002

void	snp_claim_early(paddr_t, paddr_t);
void	snp_claim_pages(struct pglist *);
void	snp_rescind_pages(struct pglist *);

#endif /* !_MACHINE_SNP_H_ */
