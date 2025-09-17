# $OpenBSD: $

.if "${MACHINE_ARCH}" != amd64
regress:
	# MACHINE_ARCH is not amd64
	@echo SKIPPED
.else

PROGS =			mmio
WARNINGS =		yes

REGRESS_TARGETS =	run-regress-mmio
run-regress-mmio: mmio
	${SUDO} ./mmio

.if ! (make(clean) || make(cleandir) || make(obj))
.include <bsd.own.mk>
ALLOWAPERTURE !=	/sbin/sysctl -n machdep.allowaperture
SEVES !=		/sbin/sysctl -n machdep.vmmode

.if "${ALLOWAPERTURE}" == 0 || "${SEVES}" != "SEV-ES"
REGRESS_SKIP_TARGETS =		${REGRESS_TARGETS}
.endif
.endif
.endif

.include <bsd.regress.mk>
