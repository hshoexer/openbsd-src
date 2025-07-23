/*	$OpenBSD: $	*/

/*
 * Copyright (c) 2015 Reyk Floeter <reyk@openbsd.org>
 * Copyright (c) 2023, 2024 Hans-Joerg Hoexer <hshoexer@genua.de>
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

#include <sys/types.h>
#include <sys/device.h>
#include <sys/ioctl.h>
#include <sys/rwlock.h>
#include <sys/stat.h>
#include <sys/mman.h>

#include <machine/bus.h>
#include <dev/ic/pspvar.h>

#include <err.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#include "pspctl.h"

__dead void	usage(int);
__dead void	ctl_usage(struct ctl_command *);
int		parse(int, char *[], int);
int		ctl_status(struct parse_result *, int , char *[]);
int		ctl_deactivate(struct parse_result *, int , char *[]);
int		ctl_decommission(struct parse_result *, int , char *[]);
int		ctl_attest(struct parse_result *, int , char *[]);
int		ctl_snp_status(struct parse_result *, int , char *[]);
int		ctl_shutdown(struct parse_result *, int , char *[]);
int		ctl_init(struct parse_result *, int , char *[]);
int		ctl_snp_init(struct parse_result *, int , char *[]);
int		ctl_snp_shutdown(struct parse_result *, int , char *[]);
#if 0
int		ctl_firmware(struct parse_result *, int , char *[]);
#endif
int		ctl_dfflush(struct parse_result *, int, char *[]);

struct ctl_command ctl_commands[] = {
	{ "status",		CMD_STATUS,	ctl_status,		"[id]" },
	{ "deactivate",		CMD_DEACTIVATE,	ctl_deactivate,		"id" },
	{ "decommission",	CMD_DECOMM,	ctl_decommission,	"id" },
	{ "attestation",	CMD_ATTEST,	ctl_attest, 		"id" },
	{ "snp_status",		CMD_SNP_STATUS,	ctl_snp_status,		"" },
	{ "shutdown",		CMD_SHUTDOWN,	ctl_shutdown,		"" },
	{ "init",		CMD_INIT,	ctl_init,		"" },
	{ "snp_init",		CMD_SNP_INIT,	ctl_snp_init,		"" },
	{ "snp_shutdown",	CMD_SNP_SHUTDOWN, ctl_snp_shutdown,	"" },
#if 0
	{ "firmware",		CMD_FIRMWARE,	ctl_firmware,		"file" },
#endif
	{ "dfflush",		CMD_DFFLUSH,	ctl_dfflush,		"" },
	{ NULL }
};

__dead void
usage(int help)
{
	extern char	*__progname;
	int		 i;

	if (help) {
		for (i = 0; ctl_commands[i]. name != NULL; i++) {
			fprintf(stderr, "usage:\t%s [-h] %s %s\n", __progname,
			    ctl_commands[i].name, ctl_commands[i].usage);
		}
	} else
		fprintf(stderr, "usage:\t%s [-h] command [argv ...]\n",
		    __progname);

	exit(1);
}

__dead void
ctl_usage(struct ctl_command *ctl)
{
	extern	char	*__progname;

	fprintf(stderr, "usage:\t%s [-h] %s %s\n", __progname, ctl->name,
	    ctl->usage);

	exit(1);
}

int
main(int argc, char *argv[])
{
	int	ch, help = 0;

	while ((ch = getopt(argc, argv, "h")) != -1) {
		switch (ch) {
		case 'h':
			help++;
			/* FALLTHROUGH */
		default:
			usage(help);
			/* NOTREACHED */
		}
	}
	argc -= optind;
	argv += optind;
	optreset = 1;
	optind = 1;

	if (argc < 1)
		usage(help);

	return (parse(argc, argv, help));
}

int
parse(int argc, char *argv[], int help)
{
	struct ctl_command	*ctl = NULL;
	struct parse_result	 res;
	int			 i;

	memset(&res, 0, sizeof(res));

	for (i = 0; ctl_commands[i]. name != NULL; i++) {
		if (strncmp(ctl_commands[i].name,
		    argv[0], strlen(argv[0])) == 0) {
			if (ctl != NULL) {
				fprintf(stderr,
				    "ambiguous argument: %s\n", argv[0]);
				usage(help);
			}
			ctl = &ctl_commands[i];
		}
	}

	if (ctl == NULL) {
		fprintf(stderr, "unknown argument: %s\n", argv[0]);
		usage(help);
	}

	res.action = ctl->action;
	res.ctl = ctl;

	if (ctl->main(&res, argc, argv) != 0)
		exit(1);

	return (0);
}

void
hexdump(void *data, size_t len)
{
	int		 i;
	unsigned char	*p;

	for (i = 0, p = (unsigned char *)data; i < len; i++) {
		if ((i % 16) == 0)
			printf("\n");
		if ((i % 8) == 0)
			printf(" ");
		printf("%02x", p[i]);
	}
	printf("\n");
}

static const char *
pspctl_state(struct psp_platform_status *pst)
{
	switch (pst->state) {
	case PSP_PSTATE_UNINIT:
		return "UNINIT";
	case PSP_PSTATE_INIT:
		return "INIT";
	case PSP_PSTATE_WORKING:
		return "WORKING";
	default:
		return "unknown";
	}
}

static const char *
pspctl_gstate(struct psp_guest_status *gst)
{
	switch (gst->state) {
	case PSP_GSTATE_UNINIT:
		return "UNINIT";
	case PSP_GSTATE_LUPDATE:
		return "LUPDATE";
	case PSP_GSTATE_LSECRET:
		return "LSECRET";
	case PSP_GSTATE_RUNNING:
		return "RUNNING";
	case PSP_GSTATE_SUPDATE:
		return "SUPDATE";
	case PSP_GSTATE_RUPDATE:
		return "RUPDATE";
	case PSP_GSTATE_SENT:
		return "SENT";
	default:
		return "unknown";
	}
}

static const char *
pspctl_snp_state(struct psp_snp_platform_status *snppst)
{
	switch (snppst->state) {
	case PSP_PSTATE_UNINIT:
		return "UNINIT";
	case PSP_PSTATE_INIT:
		return "INIT";
	default:
		return "unknown";
	}
}

int
ctl_status(struct parse_result *res, int argc, char *argv[])
{
	struct psp_platform_status	 pst;
	struct psp_guest_status		 gst;
	const char			*errstr;
	int				 fd, id = -1;

	if (argc < 1 || argc > 2)
		ctl_usage(res->ctl);

	if (argc == 2) {
		id = strtonum(argv[1], 1, 256, &errstr);	/* XXX 256? */
		if (errstr != NULL)
			ctl_usage(res->ctl);
	}

	/* get platform state */
	memset(&pst, 0, sizeof(pst));

	if ((fd = open(PSP_NODE, O_RDWR)) < 0)
		err(1, "open");
	if (ioctl(fd, PSP_IOC_GET_PSTATUS, &pst) < 0)
		err(1, "ioctl");

	printf("SEV platform status:\nmajor\t\t%hhd\nminor\t\t%hhd\n"
	    "build\t\t%hhd\n",
	    pst.api_major, pst.api_minor, (pst.cfges_build >> 24) & 0xff);
	printf("state\t\t%s\nowner\t\t%d\nSEV-ES\t\t%d\nguests\t\t%d\n",
	    pspctl_state(&pst), (pst.owner & 0x1), (pst.cfges_build & 0x1),
	    pst.guest_count);

	if (id < 0)
		goto out;

	/* if requested, also get guest state */
	memset(&gst, 0, sizeof(gst));
	gst.handle = id;

	if (ioctl(fd, PSP_IOC_GET_GSTATUS, &gst) < 0)
		err(1, "ioctl");

	printf("\nguest status:\nhandle\t0x%x\npolicy\t0x%x\nasid\t0x%x\n"
	    "state\t%s\n", gst.handle, gst.policy, gst.asid,
	    pspctl_gstate(&gst));

out:
	if (close(fd) < 0)
		err(1, "close");

	return (0);
}

int
ctl_snp_status(struct parse_result *res, int argc, char *argv[])
{
	struct psp_snp_platform_status	 snppst;
	int				 fd;

	if (argc != 1)
		ctl_usage(res->ctl);

	/* get snp platform state */
	memset(&snppst, 0, sizeof(snppst));

	if ((fd = open(PSP_NODE, O_RDWR)) < 0)
		err(1, "open");
	if (ioctl(fd, PSP_IOC_SNP_GET_PSTATUS, &snppst) < 0)
		err(1, "ioctl");
	if (close(fd) < 0)
		err(1, "close");

	printf("SNP platform status:\nmajor\t\t%hhd\nminor\t\t%hhd\n"
	    "build\t\t%d\n", snppst.api_major, snppst.api_minor, snppst.build);
	printf("state\t\t%s\nfeatures1\t0x%hhx\nfeatures2\t0x%x\n"
	    "guests\t\t%d\n",
	    pspctl_snp_state(&snppst), snppst.features1,
	    snppst.features2, snppst.guest_count);
	printf("\ncurrent TCB\nmicrocode\t%llu\nSNP\t\t%llu\nTEE\t\t%llu\n"
	    "bootloader\t%llu\n",
	    (snppst.current_tcb >> 56) & 0xff,
	    (snppst.current_tcb >> 48) & 0xff,
	    (snppst.current_tcb >> 8) & 0xff,
	    (snppst.current_tcb >> 0) & 0xff);
	printf("\nreported TCB\nmicrocode\t%llu\nSNP\t\t%llu\nTEE\t\t%llu\n"
	    "bootloader\t%llu\n",
	    (snppst.reported_tcb >> 56) & 0xff,
	    (snppst.reported_tcb >> 48) & 0xff,
	    (snppst.reported_tcb >> 8) & 0xff,
	    (snppst.reported_tcb >> 0) & 0xff);

	return (0);
}

int
ctl_decommission(struct parse_result *res, int argc, char *argv[])
{
	struct psp_decommission	 pdecomm;
	const char		*errstr;
	int			 fd, id;

	if (argc != 2)
		ctl_usage(res->ctl);

	id = strtonum(argv[1], 1, 256, &errstr);	/* XXX 256? */
	if (errstr != NULL)
		ctl_usage(res->ctl);

	memset(&pdecomm, 0, sizeof(pdecomm));
	pdecomm.handle = id;

	if ((fd = open(PSP_NODE, O_RDWR)) < 0)
		err(1, "open");
	if (ioctl(fd, PSP_IOC_DECOMMISSION, &pdecomm) < 0)
		err(1, "ioctl");
	if (close(fd) < 0)
		err(1, "close");

	return (0);
}


int
ctl_deactivate(struct parse_result *res, int argc, char *argv[])
{
	struct psp_deactivate	 pdeact;
	const char		*errstr;
	int			 fd, id;

	if (argc != 2)
		ctl_usage(res->ctl);

	id = strtonum(argv[1], 1, 256, &errstr);	/* XXX 256? */
	if (errstr != NULL)
		ctl_usage(res->ctl);

	memset(&pdeact, 0, sizeof(pdeact));
	pdeact.handle = id;

	if ((fd = open(PSP_NODE, O_RDWR)) < 0)
		err(1, "open");
	if (ioctl(fd, PSP_IOC_DEACTIVATE, &pdeact) < 0)
		err(1, "ioctl");
	if (close(fd) < 0)
		err(1, "close");

	return (0);
}

int
ctl_attest(struct parse_result *res, int argc, char *argv[])
{
	struct psp_attestation	 pattest;
	const char		*errstr;
	int			 fd, id;

	if (argc != 2)
		ctl_usage(res->ctl);

	id = strtonum(argv[1], 1, 256, &errstr);	/* XXX 256? */
	if (errstr != NULL)
		ctl_usage(res->ctl);

	memset(&pattest, 0, sizeof(pattest));
	pattest.handle = id;
	arc4random_buf(&pattest.attest_nonce, sizeof(pattest.attest_nonce));
	pattest.attest_len = sizeof(pattest.psp_report);

	if ((fd = open(PSP_NODE, O_RDWR)) < 0)
		err(1, "open");
	if (ioctl(fd, PSP_IOC_ATTESTATION, &pattest) < 0)
		err(1, "ioctl");
	if (close(fd) < 0)
		err(1, "close");

	if (memcmp(&pattest.attest_nonce, &pattest.report_nonce,
	    sizeof(pattest.attest_nonce)) != 0)
		errx(1, "nonce mismatch");

	printf("attestation report:\n");
	hexdump(&pattest, sizeof(pattest));

	return (0);
}

int
ctl_shutdown(struct parse_result *res, int argc, char *argv[])
{
	int	fd;

	if (argc != 1)
		ctl_usage(res->ctl);

	if ((fd = open(PSP_NODE, O_RDWR)) < 0)
		err(1, "open");
	if (ioctl(fd, PSP_IOC_SHUTDOWN) < 0)
		err(1, "ioctl");
	if (close(fd) < 0)
		err(1, "close");

	return (0);
}

int
ctl_init(struct parse_result *res, int argc, char *argv[])
{
	int	fd;

	if (argc != 1)
		ctl_usage(res->ctl);

	if ((fd = open(PSP_NODE, O_RDWR)) < 0)
		err(1, "open");
	if (ioctl(fd, PSP_IOC_INIT) < 0)
		err(1, "ioctl");
	if (close(fd) < 0)
		err(1, "close");

	return (0);
}

int
ctl_snp_init(struct parse_result *res, int argc, char *argv[])
{
	int	fd;

	if (argc != 1)
		ctl_usage(res->ctl);

	if ((fd = open(PSP_NODE, O_RDWR)) < 0)
		err(1, "open");
	if (ioctl(fd, PSP_IOC_SNP_INIT) < 0)
		err(1, "ioctl");
	if (close(fd) < 0)
		err(1, "close");

	return (0);
}

int
ctl_snp_shutdown(struct parse_result *res, int argc, char *argv[])
{
	int	fd;

	if (argc != 1)
		ctl_usage(res->ctl);

	if ((fd = open(PSP_NODE, O_RDWR)) < 0)
		err(1, "open");
	if (ioctl(fd, PSP_IOC_SNP_SHUTDOWN) < 0)
		err(1, "ioctl");
	if (close(fd) < 0)
		err(1, "close");

	return (0);
}


#if 0
int
ctl_firmware(struct parse_result *res, int argc, char *argv[])
{
	struct psp_downloadfirmware	 dlfw;
	struct stat			 sb;
	void				*p;
	int				 fdfw, fd;

	if (argc != 2)
		ctl_usage(res->ctl);

	if ((fdfw = open(argv[1], O_RDONLY)) < 0)
		err(1, "open");
	if (fstat(fdfw, &sb) < 0)
		err(1, "fstat");
	if ((p = mmap(NULL, sb.st_size, PROT_READ, 0, fdfw, 0)) == NULL)
		err(1, "mmap");

	memset(&dlfw, 0, sizeof(dlfw));
	dlfw.fw_paddr = (uint64_t)p;
	dlfw.fw_len = (uint32_t)sb.st_size;

	if ((fd = open(PSP_NODE, O_RDWR)) < 0)
		err(1, "open");
	if (ioctl(fd, PSP_IOC_DOWNLOADFIRMWARE, &dlfw) < 0)
		err(1, "ioctl");
	if (close(fd) < 0)
		err(1, "close");
	if (close(fdfw) < 0)
		err(1, "close");

	return (0);
}
#endif

int
ctl_dfflush(struct parse_result *res, int argc, char *argv[])
{
	int	fd;

	if (argc != 1)
		ctl_usage(res->ctl);

	if ((fd = open(PSP_NODE, O_RDWR)) < 0)
		err(1, "open");
	if (ioctl(fd, PSP_IOC_DF_FLUSH) < 0)
		err(1, "ioctl");
	if (close(fd) < 0)
		err(1, "close");

	return (0);
}
