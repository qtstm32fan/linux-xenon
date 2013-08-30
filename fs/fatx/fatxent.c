/*
 * Copyright (C) 2004, OGAWA Hirofumi
 * Released under GPL v2.
 */

#include <linux/module.h>
#include <linux/fs.h>
#include <linux/blkdev.h>
#include "xbox_fs.h"
#include "fatx.h"

struct fatxent_operations {
	void (*ent_blocknr)(struct super_block *, int, int *, sector_t *);
	void (*ent_set_ptr)(struct fatx_entry *, int);
	int (*ent_bread)(struct super_block *, struct fatx_entry *,
			 int, sector_t);
	int (*ent_get)(struct fatx_entry *);
	void (*ent_put)(struct fatx_entry *, int);
	int (*ent_next)(struct fatx_entry *);
};

static DEFINE_SPINLOCK(fatx12_entry_lock);

static void fatx12_ent_blocknr(struct super_block *sb, int entry,
			      int *offset, sector_t *blocknr)
{
	struct fatx_sb_info *sbi = FATX_SB(sb);
	int bytes = entry + (entry >> 1);
	WARN_ON(entry < FATX_START_ENT || sbi->max_cluster <= entry);
	*offset = bytes & (sb->s_blocksize - 1);
	*blocknr = sbi->fatx_start + (bytes >> sb->s_blocksize_bits);
}

static void fatx_ent_blocknr(struct super_block *sb, int entry,
			    int *offset, sector_t *blocknr)
{
	struct fatx_sb_info *sbi = FATX_SB(sb);
	int bytes = (entry << sbi->fatxent_shift);
	WARN_ON(entry < FATX_START_ENT || sbi->max_cluster <= entry);
	*offset = bytes & (sb->s_blocksize - 1);
	*blocknr = sbi->fatx_start + (bytes >> sb->s_blocksize_bits);
}

static void fatx12_ent_set_ptr(struct fatx_entry *fatxent, int offset)
{
	struct buffer_head **bhs = fatxent->bhs;
	if (fatxent->nr_bhs == 1) {
		WARN_ON(offset >= (bhs[0]->b_size - 1));
		fatxent->u.ent12_p[0] = bhs[0]->b_data + offset;
		fatxent->u.ent12_p[1] = bhs[0]->b_data + (offset + 1);
	} else {
		WARN_ON(offset != (bhs[0]->b_size - 1));
		fatxent->u.ent12_p[0] = bhs[0]->b_data + offset;
		fatxent->u.ent12_p[1] = bhs[1]->b_data;
	}
}

static void fatx16_ent_set_ptr(struct fatx_entry *fatxent, int offset)
{
	WARN_ON(offset & (2 - 1));
	fatxent->u.ent16_p = (__le16 *)(fatxent->bhs[0]->b_data + offset);
}

static void fatx32_ent_set_ptr(struct fatx_entry *fatxent, int offset)
{
	WARN_ON(offset & (4 - 1));
	fatxent->u.ent32_p = (__le32 *)(fatxent->bhs[0]->b_data + offset);
}

static int fatx12_ent_bread(struct super_block *sb, struct fatx_entry *fatxent,
			   int offset, sector_t blocknr)
{
	struct buffer_head **bhs = fatxent->bhs;

	WARN_ON(blocknr < FATX_SB(sb)->fatx_start);
	fatxent->fatx_inode = FATX_SB(sb)->fatx_inode;

	bhs[0] = sb_bread(sb, blocknr);
	if (!bhs[0])
		goto err;

	if ((offset + 1) < sb->s_blocksize)
		fatxent->nr_bhs = 1;
	else {
		/* This entry is block boundary, it needs the next block */
		blocknr++;
		bhs[1] = sb_bread(sb, blocknr);
		if (!bhs[1])
			goto err_brelse;
		fatxent->nr_bhs = 2;
	}
	fatx12_ent_set_ptr(fatxent, offset);
	return 0;

err_brelse:
	brelse(bhs[0]);
err:
	fatx_msg(sb, KERN_ERR, "FATX read failed (blocknr %llu)", (llu)blocknr);
	return -EIO;
}

static int fatx_ent_bread(struct super_block *sb, struct fatx_entry *fatxent,
			 int offset, sector_t blocknr)
{
	struct fatxent_operations *ops = FATX_SB(sb)->fatxent_ops;

	WARN_ON(blocknr < FATX_SB(sb)->fatx_start);
	fatxent->fatx_inode = FATX_SB(sb)->fatx_inode;
	fatxent->bhs[0] = sb_bread(sb, blocknr);
	if (!fatxent->bhs[0]) {
		fatx_msg(sb, KERN_ERR, "FATX read failed (blocknr %llu)",
		       (llu)blocknr);
		return -EIO;
	}
	fatxent->nr_bhs = 1;
	ops->ent_set_ptr(fatxent, offset);
	return 0;
}

static int fatx12_ent_get(struct fatx_entry *fatxent)
{
	u8 **ent12_p = fatxent->u.ent12_p;
	int next;

	spin_lock(&fatx12_entry_lock);
	if (fatxent->entry & 1)
		next = (*ent12_p[0] >> 4) | (*ent12_p[1] << 4);
	else
		next = (*ent12_p[1] << 8) | *ent12_p[0];
	spin_unlock(&fatx12_entry_lock);

	next &= 0x0fff;
	if (next >= BAD_FATX12)
		next = FATX_ENT_EOF;
	return next;
}

static int fatx16_ent_get(struct fatx_entry *fatxent)
{
	int next = le16_to_cpu(*fatxent->u.ent16_p);
	WARN_ON((unsigned long)fatxent->u.ent16_p & (2 - 1));
	if (next >= BAD_FATX16)
		next = FATX_ENT_EOF;
	return next;
}

static int fatx32_ent_get(struct fatx_entry *fatxent)
{
	int next = le32_to_cpu(*fatxent->u.ent32_p) & 0x0fffffff;
	WARN_ON((unsigned long)fatxent->u.ent32_p & (4 - 1));
	if (next >= BAD_FATX32)
		next = FATX_ENT_EOF;
	return next;
}

static void fatx12_ent_put(struct fatx_entry *fatxent, int new)
{
	u8 **ent12_p = fatxent->u.ent12_p;

	if (new == FATX_ENT_EOF)
		new = EOF_FATX12;

	spin_lock(&fatx12_entry_lock);
	if (fatxent->entry & 1) {
		*ent12_p[0] = (new << 4) | (*ent12_p[0] & 0x0f);
		*ent12_p[1] = new >> 4;
	} else {
		*ent12_p[0] = new & 0xff;
		*ent12_p[1] = (*ent12_p[1] & 0xf0) | (new >> 8);
	}
	spin_unlock(&fatx12_entry_lock);

	mark_buffer_dirty_inode(fatxent->bhs[0], fatxent->fatx_inode);
	if (fatxent->nr_bhs == 2)
		mark_buffer_dirty_inode(fatxent->bhs[1], fatxent->fatx_inode);
}

static void fatx16_ent_put(struct fatx_entry *fatxent, int new)
{
	if (new == FATX_ENT_EOF)
		new = EOF_FATX16;

	*fatxent->u.ent16_p = cpu_to_le16(new);
	mark_buffer_dirty_inode(fatxent->bhs[0], fatxent->fatx_inode);
}

static void fatx32_ent_put(struct fatx_entry *fatxent, int new)
{
	WARN_ON(new & 0xf0000000);
	new |= le32_to_cpu(*fatxent->u.ent32_p) & ~0x0fffffff;
	*fatxent->u.ent32_p = cpu_to_le32(new);
	mark_buffer_dirty_inode(fatxent->bhs[0], fatxent->fatx_inode);
}

static int fatx12_ent_next(struct fatx_entry *fatxent)
{
	u8 **ent12_p = fatxent->u.ent12_p;
	struct buffer_head **bhs = fatxent->bhs;
	u8 *nextp = ent12_p[1] + 1 + (fatxent->entry & 1);

	fatxent->entry++;
	if (fatxent->nr_bhs == 1) {
		WARN_ON(ent12_p[0] > (u8 *)(bhs[0]->b_data +
							(bhs[0]->b_size - 2)));
		WARN_ON(ent12_p[1] > (u8 *)(bhs[0]->b_data +
							(bhs[0]->b_size - 1)));
		if (nextp < (u8 *)(bhs[0]->b_data + (bhs[0]->b_size - 1))) {
			ent12_p[0] = nextp - 1;
			ent12_p[1] = nextp;
			return 1;
		}
	} else {
		WARN_ON(ent12_p[0] != (u8 *)(bhs[0]->b_data +
							(bhs[0]->b_size - 1)));
		WARN_ON(ent12_p[1] != (u8 *)bhs[1]->b_data);
		ent12_p[0] = nextp - 1;
		ent12_p[1] = nextp;
		brelse(bhs[0]);
		bhs[0] = bhs[1];
		fatxent->nr_bhs = 1;
		return 1;
	}
	ent12_p[0] = NULL;
	ent12_p[1] = NULL;
	return 0;
}

static int fatx16_ent_next(struct fatx_entry *fatxent)
{
	const struct buffer_head *bh = fatxent->bhs[0];
	fatxent->entry++;
	if (fatxent->u.ent16_p < (__le16 *)(bh->b_data + (bh->b_size - 2))) {
		fatxent->u.ent16_p++;
		return 1;
	}
	fatxent->u.ent16_p = NULL;
	return 0;
}

static int fatx32_ent_next(struct fatx_entry *fatxent)
{
	const struct buffer_head *bh = fatxent->bhs[0];
	fatxent->entry++;
	if (fatxent->u.ent32_p < (__le32 *)(bh->b_data + (bh->b_size - 4))) {
		fatxent->u.ent32_p++;
		return 1;
	}
	fatxent->u.ent32_p = NULL;
	return 0;
}

static struct fatxent_operations fatx12_ops = {
	.ent_blocknr	= fatx12_ent_blocknr,
	.ent_set_ptr	= fatx12_ent_set_ptr,
	.ent_bread	= fatx12_ent_bread,
	.ent_get	= fatx12_ent_get,
	.ent_put	= fatx12_ent_put,
	.ent_next	= fatx12_ent_next,
};

static struct fatxent_operations fatx16_ops = {
	.ent_blocknr	= fatx_ent_blocknr,
	.ent_set_ptr	= fatx16_ent_set_ptr,
	.ent_bread	= fatx_ent_bread,
	.ent_get	= fatx16_ent_get,
	.ent_put	= fatx16_ent_put,
	.ent_next	= fatx16_ent_next,
};

static struct fatxent_operations fatx32_ops = {
	.ent_blocknr	= fatx_ent_blocknr,
	.ent_set_ptr	= fatx32_ent_set_ptr,
	.ent_bread	= fatx_ent_bread,
	.ent_get	= fatx32_ent_get,
	.ent_put	= fatx32_ent_put,
	.ent_next	= fatx32_ent_next,
};

static inline void lock_fatx(struct fatx_sb_info *sbi)
{
	mutex_lock(&sbi->fatx_lock);
}

static inline void unlock_fatx(struct fatx_sb_info *sbi)
{
	mutex_unlock(&sbi->fatx_lock);
}

void fatx_ent_access_init(struct super_block *sb)
{
	struct fatx_sb_info *sbi = FATX_SB(sb);

	mutex_init(&sbi->fatx_lock);

	switch (sbi->fatx_bits) {
	case 32:
		sbi->fatxent_shift = 2;
		sbi->fatxent_ops = &fatx32_ops;
		break;
	case 16:
		sbi->fatxent_shift = 1;
		sbi->fatxent_ops = &fatx16_ops;
		break;
	case 12:
		sbi->fatxent_shift = -1;
		sbi->fatxent_ops = &fatx12_ops;
		break;
	}
}

static void mark_fsinfo_dirty(struct super_block *sb)
{
	struct fatx_sb_info *sbi = FATX_SB(sb);

	if (sb->s_flags & MS_RDONLY || sbi->fatx_bits != 32)
		return;

	__mark_inode_dirty(sbi->fsinfo_inode, I_DIRTY_SYNC);
}

static inline int fatx_ent_update_ptr(struct super_block *sb,
				     struct fatx_entry *fatxent,
				     int offset, sector_t blocknr)
{
	struct fatx_sb_info *sbi = FATX_SB(sb);
	struct fatxent_operations *ops = sbi->fatxent_ops;
	struct buffer_head **bhs = fatxent->bhs;

	/* Is this fatxent's blocks including this entry? */
	if (!fatxent->nr_bhs || bhs[0]->b_blocknr != blocknr)
		return 0;
	if (sbi->fatx_bits == 12) {
		if ((offset + 1) < sb->s_blocksize) {
			/* This entry is on bhs[0]. */
			if (fatxent->nr_bhs == 2) {
				brelse(bhs[1]);
				fatxent->nr_bhs = 1;
			}
		} else {
			/* This entry needs the next block. */
			if (fatxent->nr_bhs != 2)
				return 0;
			if (bhs[1]->b_blocknr != (blocknr + 1))
				return 0;
		}
	}
	ops->ent_set_ptr(fatxent, offset);
	return 1;
}

int fatx_ent_read(struct inode *inode, struct fatx_entry *fatxent, int entry)
{
	struct super_block *sb = inode->i_sb;
	struct fatx_sb_info *sbi = FATX_SB(inode->i_sb);
	struct fatxent_operations *ops = sbi->fatxent_ops;
	int err, offset;
	sector_t blocknr;

	if (entry < FATX_START_ENT || sbi->max_cluster <= entry) {
		fatxent_brelse(fatxent);
		fatx_fs_error(sb, "invalid access to FATX (entry 0x%08x)", entry);
		return -EIO;
	}

	fatxent_set_entry(fatxent, entry);
	ops->ent_blocknr(sb, entry, &offset, &blocknr);

	if (!fatx_ent_update_ptr(sb, fatxent, offset, blocknr)) {
		fatxent_brelse(fatxent);
		err = ops->ent_bread(sb, fatxent, offset, blocknr);
		if (err)
			return err;
	}
	return ops->ent_get(fatxent);
}

/* FIXME: We can write the blocks as more big chunk. */
static int fatx_mirror_bhs(struct super_block *sb, struct buffer_head **bhs,
			  int nr_bhs)
{
	struct fatx_sb_info *sbi = FATX_SB(sb);
	struct buffer_head *c_bh;
	int err, n, copy;

	err = 0;
	for (copy = 1; copy < sbi->fatxs; copy++) {
		sector_t backup_fatx = sbi->fatx_length * copy;

		for (n = 0; n < nr_bhs; n++) {
			c_bh = sb_getblk(sb, backup_fatx + bhs[n]->b_blocknr);
			if (!c_bh) {
				err = -ENOMEM;
				goto error;
			}
			memcpy(c_bh->b_data, bhs[n]->b_data, sb->s_blocksize);
			set_buffer_uptodate(c_bh);
			mark_buffer_dirty_inode(c_bh, sbi->fatx_inode);
			if (sb->s_flags & MS_SYNCHRONOUS)
				err = sync_dirty_buffer(c_bh);
			brelse(c_bh);
			if (err)
				goto error;
		}
	}
error:
	return err;
}

int fatx_ent_write(struct inode *inode, struct fatx_entry *fatxent,
		  int new, int wait)
{
	struct super_block *sb = inode->i_sb;
	struct fatxent_operations *ops = FATX_SB(sb)->fatxent_ops;
	int err;

	ops->ent_put(fatxent, new);
	if (wait) {
		err = fatx_sync_bhs(fatxent->bhs, fatxent->nr_bhs);
		if (err)
			return err;
	}
	return fatx_mirror_bhs(sb, fatxent->bhs, fatxent->nr_bhs);
}

static inline int fatx_ent_next(struct fatx_sb_info *sbi,
			       struct fatx_entry *fatxent)
{
	if (sbi->fatxent_ops->ent_next(fatxent)) {
		if (fatxent->entry < sbi->max_cluster)
			return 1;
	}
	return 0;
}

static inline int fatx_ent_read_block(struct super_block *sb,
				     struct fatx_entry *fatxent)
{
	struct fatxent_operations *ops = FATX_SB(sb)->fatxent_ops;
	sector_t blocknr;
	int offset;

	fatxent_brelse(fatxent);
	ops->ent_blocknr(sb, fatxent->entry, &offset, &blocknr);
	return ops->ent_bread(sb, fatxent, offset, blocknr);
}

static void fatx_collect_bhs(struct buffer_head **bhs, int *nr_bhs,
			    struct fatx_entry *fatxent)
{
	int n, i;

	for (n = 0; n < fatxent->nr_bhs; n++) {
		for (i = 0; i < *nr_bhs; i++) {
			if (fatxent->bhs[n] == bhs[i])
				break;
		}
		if (i == *nr_bhs) {
			get_bh(fatxent->bhs[n]);
			bhs[i] = fatxent->bhs[n];
			(*nr_bhs)++;
		}
	}
}

int fatx_alloc_clusters(struct inode *inode, int *cluster, int nr_cluster)
{
	struct super_block *sb = inode->i_sb;
	struct fatx_sb_info *sbi = FATX_SB(sb);
	struct fatxent_operations *ops = sbi->fatxent_ops;
	struct fatx_entry fatxent, prev_ent;
	struct buffer_head *bhs[MAX_BUF_PER_PAGE];
	int i, count, err, nr_bhs, idx_clus;

	BUG_ON(nr_cluster > (MAX_BUF_PER_PAGE / 2));	/* fixed limit */

	lock_fatx(sbi);
	if (sbi->free_clusters != -1 && sbi->free_clus_valid &&
	    sbi->free_clusters < nr_cluster) {
		unlock_fatx(sbi);
		return -ENOSPC;
	}

	err = nr_bhs = idx_clus = 0;
	count = FATX_START_ENT;
	fatxent_init(&prev_ent);
	fatxent_init(&fatxent);
	fatxent_set_entry(&fatxent, sbi->prev_free + 1);
	while (count < sbi->max_cluster) {
		if (fatxent.entry >= sbi->max_cluster)
			fatxent.entry = FATX_START_ENT;
		fatxent_set_entry(&fatxent, fatxent.entry);
		err = fatx_ent_read_block(sb, &fatxent);
		if (err)
			goto out;

		/* Find the free entries in a block */
		do {
			if (ops->ent_get(&fatxent) == FATX_ENT_FREE) {
				int entry = fatxent.entry;

				/* make the cluster chain */
				ops->ent_put(&fatxent, FATX_ENT_EOF);
				if (prev_ent.nr_bhs)
					ops->ent_put(&prev_ent, entry);

				fatx_collect_bhs(bhs, &nr_bhs, &fatxent);

				sbi->prev_free = entry;
				if (sbi->free_clusters != -1)
					sbi->free_clusters--;

				cluster[idx_clus] = entry;
				idx_clus++;
				if (idx_clus == nr_cluster)
					goto out;

				/*
				 * fatx_collect_bhs() gets ref-count of bhs,
				 * so we can still use the prev_ent.
				 */
				prev_ent = fatxent;
			}
			count++;
			if (count == sbi->max_cluster)
				break;
		} while (fatx_ent_next(sbi, &fatxent));
	}

	/* Couldn't allocate the free entries */
	sbi->free_clusters = 0;
	sbi->free_clus_valid = 1;
	err = -ENOSPC;

out:
	unlock_fatx(sbi);
	mark_fsinfo_dirty(sb);
	fatxent_brelse(&fatxent);
	if (!err) {
		if (inode_needs_sync(inode))
			err = fatx_sync_bhs(bhs, nr_bhs);
		if (!err)
			err = fatx_mirror_bhs(sb, bhs, nr_bhs);
	}
	for (i = 0; i < nr_bhs; i++)
		brelse(bhs[i]);

	if (err && idx_clus)
		fatx_free_clusters(inode, cluster[0]);

	return err;
}

int fatx_free_clusters(struct inode *inode, int cluster)
{
	struct super_block *sb = inode->i_sb;
	struct fatx_sb_info *sbi = FATX_SB(sb);
	struct fatxent_operations *ops = sbi->fatxent_ops;
	struct fatx_entry fatxent;
	struct buffer_head *bhs[MAX_BUF_PER_PAGE];
	int i, err, nr_bhs;
	int first_cl = cluster, dirty_fsinfo = 0;

	nr_bhs = 0;
	fatxent_init(&fatxent);
	lock_fatx(sbi);
	do {
		cluster = fatx_ent_read(inode, &fatxent, cluster);
		if (cluster < 0) {
			err = cluster;
			goto error;
		} else if (cluster == FATX_ENT_FREE) {
			fatx_fs_error(sb, "%s: deleting FATX entry beyond EOF",
				     __func__);
			err = -EIO;
			goto error;
		}

		if (sbi->options.discard) {
			/*
			 * Issue discard for the sectors we no longer
			 * care about, batching contiguous clusters
			 * into one request
			 */
			if (cluster != fatxent.entry + 1) {
				int nr_clus = fatxent.entry - first_cl + 1;

				sb_issue_discard(sb,
					fatx_clus_to_blknr(sbi, first_cl),
					nr_clus * sbi->sec_per_clus,
					GFP_NOFS, 0);

				first_cl = cluster;
			}
		}

		ops->ent_put(&fatxent, FATX_ENT_FREE);
		if (sbi->free_clusters != -1) {
			sbi->free_clusters++;
			dirty_fsinfo = 1;
		}

		if (nr_bhs + fatxent.nr_bhs > MAX_BUF_PER_PAGE) {
			if (sb->s_flags & MS_SYNCHRONOUS) {
				err = fatx_sync_bhs(bhs, nr_bhs);
				if (err)
					goto error;
			}
			err = fatx_mirror_bhs(sb, bhs, nr_bhs);
			if (err)
				goto error;
			for (i = 0; i < nr_bhs; i++)
				brelse(bhs[i]);
			nr_bhs = 0;
		}
		fatx_collect_bhs(bhs, &nr_bhs, &fatxent);
	} while (cluster != FATX_ENT_EOF);

	if (sb->s_flags & MS_SYNCHRONOUS) {
		err = fatx_sync_bhs(bhs, nr_bhs);
		if (err)
			goto error;
	}
	err = fatx_mirror_bhs(sb, bhs, nr_bhs);
error:
	fatxent_brelse(&fatxent);
	for (i = 0; i < nr_bhs; i++)
		brelse(bhs[i]);
	unlock_fatx(sbi);
	if (dirty_fsinfo)
		mark_fsinfo_dirty(sb);

	return err;
}
EXPORT_SYMBOL_GPL(fatx_free_clusters);

/* 128kb is the whole sectors for FATX12 and FATX16 */
#define FATX_READA_SIZE		(128 * 1024)

static void fatx_ent_reada(struct super_block *sb, struct fatx_entry *fatxent,
			  unsigned long reada_blocks)
{
	struct fatxent_operations *ops = FATX_SB(sb)->fatxent_ops;
	sector_t blocknr;
	int i, offset;

	ops->ent_blocknr(sb, fatxent->entry, &offset, &blocknr);

	for (i = 0; i < reada_blocks; i++)
		sb_breadahead(sb, blocknr + i);
}

int fatx_count_free_clusters(struct super_block *sb)
{
	struct fatx_sb_info *sbi = FATX_SB(sb);
	struct fatxent_operations *ops = sbi->fatxent_ops;
	struct fatx_entry fatxent;
	unsigned long reada_blocks, reada_mask, cur_block;
	int err = 0, free;

	lock_fatx(sbi);
	if (sbi->free_clusters != -1 && sbi->free_clus_valid)
		goto out;

	reada_blocks = FATX_READA_SIZE >> sb->s_blocksize_bits;
	reada_mask = reada_blocks - 1;
	cur_block = 0;

	free = 0;
	fatxent_init(&fatxent);
	fatxent_set_entry(&fatxent, FATX_START_ENT);
	while (fatxent.entry < sbi->max_cluster) {
		/* readahead of fatx blocks */
		if ((cur_block & reada_mask) == 0) {
			unsigned long rest = sbi->fatx_length - cur_block;
			fatx_ent_reada(sb, &fatxent, min(reada_blocks, rest));
		}
		cur_block++;

		err = fatx_ent_read_block(sb, &fatxent);
		if (err)
			goto out;

		do {
			if (ops->ent_get(&fatxent) == FATX_ENT_FREE)
				free++;
		} while (fatx_ent_next(sbi, &fatxent));
	}
	sbi->free_clusters = free;
	sbi->free_clus_valid = 1;
	mark_fsinfo_dirty(sb);
	fatxent_brelse(&fatxent);
out:
	unlock_fatx(sbi);
	return err;
}
