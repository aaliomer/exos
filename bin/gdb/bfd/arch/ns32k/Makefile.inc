
SRCS+=		netbsd-ns32k.c cpu-ns32k.c

CFLAGS+=	-DNETBSD_CORE

VECTORS=	-DDEFAULT_VECTOR=netbsd_ns32k_vec \
		-DSELECT_ARCHITECTURES=bfd_ns32k_arch \
		-DSELECT_VECS='&netbsd_ns32k_vec'


