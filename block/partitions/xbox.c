/*
 * block/partitions/xbox.c
 * Xbox disk partition support.
 *
 * Copyright (C) 2002  John Scott Tillman <speedbump@users.sourceforge.net>
 * 
 * Xbox 360 support written by tuxuser <webmaster@libxenon.org> 2012
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/errno.h>
#include <linux/genhd.h>
#include <linux/kernel.h>

#include "check.h"
#include "xbox.h"

static int xbox_ext_partition_type = 0;

static int __init xbox_setup_ext_partition(char *str)
{
    int val;
    if (get_option(&str,&val) == 1) {
      xbox_ext_partition_type = val;
    }

    return 1;
}

__setup("xboxpartition=", xbox_setup_ext_partition);


static int xbox_check_magic(struct block_device *bdev, sector_t at_sect,
        char *magic)
{
    Sector sect;
    char *data;
    int ret;

    data = read_dev_sector(bdev, at_sect, &sect);
    if (!data)
        return -1;

    ret = (!strncmp(data, magic, strlen(magic)-1)) ? 1  : 0;
    put_dev_sector(sect);

    return ret;
}

static inline int xbox_drive_detect(struct block_device *bdev)
{
    /**
    * "BRFR" is apparently the magic number in the config area
    * the others are just paranoid checks to assure the expected
    * "FATX" tags for the other xbox partitions
    *
    * the odds against a non-xbox drive having random data to match is
    * astronomical...but it's possible I guess...you should only include
    * this check if you actually *have* an xbox drive...since it has to
    * be detected first
    *
    * @see check.c
    */
	return (xbox_check_magic(bdev, XBOX_MAGIC_SECT, XBOX_BRFR_MAGIC) &&
		xbox_check_magic(bdev, XBOX_SYSTEM_START, XBOX_FATX_MAGIC) &&
		xbox_check_magic(bdev, XBOX_DATA_START, XBOX_FATX_MAGIC)) ?
		0 : -ENODEV;
}

static inline int
xbox_ptbl_detect(struct block_device *bdev)
{
    /**
    * check for "BRFR" magic number, then for "****PARTINFO****" at
    * start of zeroth sector. This intentionally doesn't check for
    * FATX signatures at the expected system and store locations
    * as it's possible for these partitions to have been moved.
    */
	if(xbox_check_magic(bdev,XBOX_MAGIC_SECT, XBOX_BRFR_MAGIC)) {
		Sector sect;
		xbox_partition_table *table;

		table = (xbox_partition_table*)read_dev_sector(bdev, XBOX_CONFIG_START, &sect);

		if (!table)
			return 0;
			
		if (strncmp(table->magic, "****PARTINFO****", 16) == 0)
			return 1;
	}
	return 0;
}

int xbox_partition(struct parsed_partitions *state)
{
    sector_t disk_size = bdev_logical_block_size(state->bdev) / 512;

    if (xbox_ext_partition_type == 0 && xbox_ptbl_detect(state->bdev)) {

		Sector sect;
		int i, slot = 1;
		xbox_partition_table *table = {0};
		
		table = (xbox_partition_table*)read_dev_sector(state->bdev, XBOX_CONFIG_START, &sect);

		if (!table)
			return 0;
			
		printk(KERN_INFO "[fatx] Xbox XDK HDD detected\n");

		for (i=0; i<14; i++) {
			xbox_partition_entry *part = &table->partitions[i];
			if (part->flags & XBOX_PARTITION_IN_USE) {
				if (part->start >= disk_size)
					printk(KERN_INFO "[fatx] partition %d starts off the end of the disk, at sector %d, skipping\n", i, part->start);
				else if ((part->start + part->size) > disk_size)
					printk(KERN_INFO "[fatx] partition %d extends past the end of the disk, to sector %d, skipping\n", i, part->start + part->size - 1);
				else
					put_partition(state, slot++,
						part->start, part->size);
			}
		}
		put_dev_sector(sect);
		return 1; // xbox xdk drive found
    } else {
		int slot = 1;
        int err = xbox_drive_detect(state->bdev);
        if (err)
            return err;

        printk("[fatx] Xbox Retail HDD detected\n");

        put_partition(state, slot++, XBOX_DATA_START, XBOX_DATA_SIZE);
        put_partition(state, slot++, XBOX_SYSTEM_START, XBOX_SYSTEM_SIZE);
        put_partition(state, slot++, XBOX_CACHE1_START, XBOX_CACHE1_SIZE);
        put_partition(state, slot++, XBOX_CACHE2_START, XBOX_CACHE2_SIZE);
        put_partition(state, slot++, XBOX_CACHE3_START, XBOX_CACHE3_SIZE);

        if (disk_size <= XBOX_EXTEND_F_START)
            goto out; /* no extended partitions */

        /* Support for fixed size 'extended partitions' */
        if (XBOX_EXTEND_G_START < disk_size) {
            /* use NKPatcher67 style F and G drive */
            if ( xbox_ext_partition_type == XBOX_EXT_PART_NKP67 ) {
                /* There's an F and G on this system - F only goes to the LBA28 boundary */
                put_partition(state, slot++, XBOX_EXTEND_F_START,
                    XBOX_EXTEND_F_SIZE);
                /* G goes to end of drive */
                put_partition(state, slot++, XBOX_EXTEND_G_START,
                    disk_size - XBOX_EXTEND_G_START);
             } else {
             /* use NKPatcher06 style F drive */
                put_partition(state, slot++, XBOX_EXTEND_F_START,
                    disk_size - XBOX_EXTEND_F_START);
             }
        } else {
        /* Just a large F on this system - to end of drive*/
            put_partition(state, slot++, XBOX_EXTEND_F_START,
                disk_size - XBOX_EXTEND_F_START);
        }
    }

out:
    printk("\n");
    return 1; // xbox retail drive found
}

static inline int xbox360_drive_detect(struct block_device *bdev)
{
    /**
    * "BRFR" is apparently the magic number in the config area
    * the others are just paranoid checks to assure the expected
    * "FATX" tags for the other xbox partitions
    *
    * the odds against a non-xbox drive having random data to match is
    * astronomical...but it's possible I guess...you should only include
    * this check if you actually *have* an xbox drive...since it has to
    * be detected first
    *
    * @see check.c
    */
    return (xbox_check_magic(bdev, XBOX360_SYSTEM_CACHE_START, XBOX360_FATX_MAGIC) &&
		xbox_check_magic(bdev, XBOX360_GAME_CACHE_START, XBOX360_FATX_MAGIC) &&
		xbox_check_magic(bdev, XBOX360_CLASSIC_COMPATIBILITY_START, XBOX360_FATX_MAGIC) &&
		xbox_check_magic(bdev, XBOX360_DATA_START, XBOX360_FATX_MAGIC)) ?
		0 : -ENODEV;
}

static inline int
xbox360_ptbl_detect(struct block_device *bdev)
{
	Sector sect;
	xbox360_partition_table *table = {0};
		
	table = (xbox360_partition_table*)read_dev_sector(bdev, XBOX360_XDK_PTABLE_START, &sect);
		
	if (!table)
		return 0;
				
	printk("[xtaf 360] PTABLE MAGIC : 0x%08X\n",table->magic);
	if (table->magic == XBOX360_XDK_PTABLE_MAGIC)
			return 1;
			
	return 0;
}

int xbox360_partition(struct parsed_partitions *state)
{
    sector_t disk_size = bdev_logical_block_size(state->bdev) / 512;

    if (xbox360_ptbl_detect(state->bdev)) {

        Sector sect;
        int i, slot = 1;
        xbox360_partition_table *table = {0};

		table = (xbox360_partition_table*)read_dev_sector(state->bdev, XBOX360_XDK_PTABLE_START, &sect);
		

        if (!table)
			return 0;

		printk(KERN_INFO "[fatx] Xbox 360 XDK HDD detected\n");
		
		for (i=0; i<3; i++) {
			xbox360_partition_entry *part = &table->partitions[i];
			if (xbox_check_magic(state->bdev, part->start, XBOX360_FATX_MAGIC)) {
				if (part->start >= disk_size)
					printk(KERN_INFO "[fatx360] partition %d starts off the end of the disk, at sector %d, skipping\n", i, part->start);
				else if ((part->start + part->size) > disk_size)
					printk(KERN_INFO "[fatx360] partition %d extends past the end of the disk, to sector %d, skipping\n", i, part->start + part->size - 1);
				else
					put_partition(state, slot++, part->start, part->size);
			}
		}
		put_dev_sector(sect);
		return 1;
    } else if ((disk_size == XBOX360_SLIM_4GB_INTERNAL_MU_SIZE) && 
				xbox_check_magic(state->bdev, XBOX360_SLIM_4GB_INTERNAL_MU_START, XBOX360_FATX_MAGIC)) {
					
		printk(KERN_INFO "[fatx] Xbox 360 Memory Unit detected\n");
        put_partition(state, 1, XBOX360_SLIM_4GB_INTERNAL_MU_START, XBOX360_SLIM_4GB_INTERNAL_MU_SIZE);
        
        return 1;
		
	} else {
		int slot = 1;
        int err = xbox360_drive_detect(state->bdev);
        if (err)
            return err;

        printk(KERN_INFO "[fatx] Xbox 360 Retail HDD detected\n");
        
        put_partition(state, slot++, XBOX360_SYSTEM_CACHE_START, XBOX360_SYSTEM_CACHE_SIZE);
        put_partition(state, slot++, XBOX360_GAME_CACHE_START, XBOX360_GAME_CACHE_SIZE);
        put_partition(state, slot++, XBOX360_CLASSIC_COMPATIBILITY_START, XBOX360_CLASSIC_COMPATIBILITY_SIZE);

		/* DATA goes to end of drive */
        put_partition(state, slot++, XBOX360_DATA_START,
                    disk_size - (XBOX360_DATA_START));
                    
		printk(KERN_INFO "\n");
		return 1;
    }
    
    return -ENODEV;
}
