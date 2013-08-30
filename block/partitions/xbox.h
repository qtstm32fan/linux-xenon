/*
 *  fs/partitions/xbox.h
 */

/*
 * The native Xbox kernel makes use of an implicit partitioning
 * scheme. Partition locations and sizes on-disk are hard-coded.
 */

#ifndef _PARTITIONS_XBOX_H
#define _PARTITIONS_XBOX_H

/* Classic Xbox */

#define XBOX_FATX_MAGIC		"FATX"
#define XBOX_BRFR_MAGIC		"BRFR"
 
#define XBOX_CONFIG_START   0x00000000L
#define XBOX_CACHE1_START   0x00000400L
#define XBOX_CACHE2_START   0x00177400L
#define XBOX_CACHE3_START   0x002EE400L
#define XBOX_SYSTEM_START   0x00465400L
#define XBOX_DATA_START     0x0055F400L
#define XBOX_EXTEND_F_START 0x00EE8AB0L
#define XBOX_EXTEND_G_START 0x0FFFFFFFL

#define XBOX_CONFIG_SIZE    (XBOX_CACHE1_START - XBOX_CONFIG_START)
#define XBOX_CACHE1_SIZE    (XBOX_CACHE2_START - XBOX_CACHE1_START)
#define XBOX_CACHE2_SIZE    (XBOX_CACHE3_START - XBOX_CACHE2_START)
#define XBOX_CACHE3_SIZE    (XBOX_SYSTEM_START - XBOX_CACHE3_START)
#define XBOX_SYSTEM_SIZE    (XBOX_DATA_START - XBOX_SYSTEM_START)
#define XBOX_DATA_SIZE      (XBOX_EXTEND_F_START - XBOX_DATA_START)
#define XBOX_EXTEND_F_SIZE  (XBOX_EXTEND_G_START - XBOX_EXTEND_F_START)

#define XBOX_MAGIC_SECT       3L
#define XBOX_PARTITION_IN_USE 0x80000000
#define XBOX_DEV_MINOR_START		50

#define XBOX_EXT_PART_NKP06  1
#define XBOX_EXT_PART_NKP67  2

/* Xbox 360 */

#define XBOX360_FATX_MAGIC			"XTAF"
#define XBOX360_JOSH_MAGIC			"Josh"

#define XBOX360_SECTOR_SIZE			0x200 

#define XBOX360_JOSH_SECT			4
#define XBOX360_XDK_PTABLE_START	0
#define XBOX360_XDK_PTABLE_MAGIC	0x20000
#define XBOX360_XDK_PTABLE_SIZE		0x18

#define XBOX360_SYSTEM_CACHE_START			0x000080000L / XBOX360_SECTOR_SIZE
#define XBOX360_GAME_CACHE_START			0x080080000L / XBOX360_SECTOR_SIZE
#define XBOX360_SYSTEM_EXTENDED_START		0x10C080000L / XBOX360_SECTOR_SIZE
#define XBOX360_SYSTEM_AUXILARY_START		0x118EB0000L / XBOX360_SECTOR_SIZE
#define XBOX360_CLASSIC_COMPATIBILITY_START		0x120EB0000L / XBOX360_SECTOR_SIZE
#define XBOX360_DATA_START					0x130EB0000L / XBOX360_SECTOR_SIZE

#define XBOX360_SYSTEM_CACHE_SIZE			(XBOX360_GAME_CACHE_START - XBOX360_SYSTEM_CACHE_START)
#define XBOX360_GAME_CACHE_SIZE				(XBOX360_SYSTEM_EXTENDED_START - XBOX360_GAME_CACHE_START)
#define XBOX360_SYSTEM_EXTENDED_SIZE		(XBOX360_SYSTEM_AUXILARY_START - XBOX360_SYSTEM_EXTENDED_START)
#define XBOX360_SYSTEM_AUXILARY_SIZE		(XBOX360_CLASSIC_COMPATIBILITY_START - XBOX360_SYSTEM_AUXILARY_START)
#define XBOX360_CLASSIC_COMPATIBILITY_SIZE		(XBOX360_DATA_START - XBOX360_CLASSIC_COMPATIBILITY_START)

#define XBOX360_SLIM_4GB_INTERNAL_MU_SIZE	7831552
#define XBOX360_SLIM_4GB_INTERNAL_MU_START	0

typedef struct
{
    char name[16];
    u32  flags;
    u32  start;
    u32  size;
    u32  reserved;
} xbox_partition_entry;

typedef struct
{
    char                 magic[16];
    char                 reserved[32];
    xbox_partition_entry partitions[14];
} xbox_partition_table;

typedef struct
{
	u32  start; // sectorcount
    u32  size; // sectorcount
} xbox360_partition_entry;

typedef struct
{
    u32                 	magic;
    u32						reserved;
    xbox360_partition_entry partitions[3];
    char					padding[480];
} xbox360_partition_table;

int xbox_partition(struct parsed_partitions *state);
int xbox360_partition(struct parsed_partitions *state);

#endif
