/*	$OpenBSD: $	*/

/*
 * Copyright (c) 2015 Reyk Floeter <reyk@openbsd.org>
 * Copyright (c) 2024 Hans-Joerg Hoexer <hshoexer@genua.de>
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

#define PSP_NODE	"/dev/psp"

enum actions {
	NONE,
	CMD_STATUS,
	CMD_DEACTIVATE,
	CMD_DECOMM,
	CMD_ATTEST,
	CMD_SNP_STATUS,
	CMD_SHUTDOWN,
	CMD_INIT,
	CMD_SNP_INIT,
	CMD_FIRMWARE,
	CMD_DFFLUSH
};

struct ctl_command;

struct parse_result {
	enum actions		 action;
	struct ctl_command	*ctl;
};

struct ctl_command {
	const char	*name;
	enum actions	 action;
	int		(*main)(struct parse_result *, int, char *[]);
	const char	*usage;
};
