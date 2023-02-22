#define COMMON_USER_PPC64	(PPC_FEATURE_32 | PPC_FEATURE_HAS_FPU | \
				 PPC_FEATURE_HAS_MMU | PPC_FEATURE_64)

static struct cpu_spec cpu_specs[] __initdata = {
	{	/* Xenon */
		.pvr_mask		= 0xffff0000,
		.pvr_value		= 0x00710000,
		.cpu_name		= "Xenon",
		.cpu_features		= CPU_FTRS_XENON,
		.cpu_user_features	= COMMON_USER_PPC64 | PPC_FEATURE_HAS_ALTIVEC_COMP |
			PPC_FEATURE_SMT,
		.mmu_features		= MMU_FTRS_XENON,
		.icache_bsize		= 128,
		.dcache_bsize		= 128,
		.platform		= "xenon",
	},
};
