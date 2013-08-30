/* fs/fatx/nfs.c
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/exportfs.h>
#include "fatx.h"

struct fatx_fid {
	u32 i_gen;
	u32 i_pos_low;
	u16 i_pos_hi;
	u16 parent_i_pos_hi;
	u32 parent_i_pos_low;
	u32 parent_i_gen;
};

#define FATX_FID_SIZE_WITHOUT_PARENT 3
#define FATX_FID_SIZE_WITH_PARENT (sizeof(struct fatx_fid)/sizeof(u32))

/**
 * Look up a directory inode given its starting cluster.
 */
static struct inode *fatx_dget(struct super_block *sb, int i_logstart)
{
	struct fatx_sb_info *sbi = FATX_SB(sb);
	struct hlist_head *head;
	struct fatx_inode_info *i;
	struct inode *inode = NULL;

	head = sbi->dir_hashtable + fatx_dir_hash(i_logstart);
	spin_lock(&sbi->dir_hash_lock);
	hlist_for_each_entry(i, head, i_dir_hash) {
		BUG_ON(i->vfs_inode.i_sb != sb);
		if (i->i_logstart != i_logstart)
			continue;
		inode = igrab(&i->vfs_inode);
		if (inode)
			break;
	}
	spin_unlock(&sbi->dir_hash_lock);
	return inode;
}

static struct inode *fatx_ilookup(struct super_block *sb, u64 ino, loff_t i_pos)
{
	if (FATX_SB(sb)->options.nfs == FATX_NFS_NOSTALE_RO)
		return fatx_iget(sb, i_pos);

	else {
		if ((ino < FATX_ROOT_INO) || (ino == FATX_FSINFO_INO))
			return NULL;
		return ilookup(sb, ino);
	}
}

static struct inode *__fatx_nfs_get_inode(struct super_block *sb,
				       u64 ino, u32 generation, loff_t i_pos)
{
	struct inode *inode = fatx_ilookup(sb, ino, i_pos);

	if (inode && generation && (inode->i_generation != generation)) {
		iput(inode);
		inode = NULL;
	}
	if (inode == NULL && FATX_SB(sb)->options.nfs == FATX_NFS_NOSTALE_RO) {
		struct buffer_head *bh = NULL;
		struct fatx_dir_entry *de ;
		sector_t blocknr;
		int offset;
		fatx_get_blknr_offset(FATX_SB(sb), i_pos, &blocknr, &offset);
		bh = sb_bread(sb, blocknr);
		if (!bh) {
			fatx_msg(sb, KERN_ERR,
				"unable to read block(%llu) for building NFS inode",
				(llu)blocknr);
			return inode;
		}
		de = (struct fatx_dir_entry *)bh->b_data;
		/* If a file is deleted on server and client is not updated
		 * yet, we must not build the inode upon a lookup call.
		 */
		if (IS_FREE(de[offset].name))
			inode = NULL;
		else
			inode = fatx_build_inode(sb, &de[offset], i_pos);
		brelse(bh);
	}

	return inode;
}

static struct inode *fatx_nfs_get_inode(struct super_block *sb,
				       u64 ino, u32 generation)
{

	return __fatx_nfs_get_inode(sb, ino, generation, 0);
}

static int
fatx_encode_fh_nostale(struct inode *inode, __u32 *fh, int *lenp,
		      struct inode *parent)
{
	int len = *lenp;
	struct fatx_sb_info *sbi = FATX_SB(inode->i_sb);
	struct fatx_fid *fid = (struct fatx_fid *) fh;
	loff_t i_pos;
	int type = FILEID_FAT_WITHOUT_PARENT;

	if (parent) {
		if (len < FATX_FID_SIZE_WITH_PARENT) {
			*lenp = FATX_FID_SIZE_WITH_PARENT;
			return FILEID_INVALID;
		}
	} else {
		if (len < FATX_FID_SIZE_WITHOUT_PARENT) {
			*lenp = FATX_FID_SIZE_WITHOUT_PARENT;
			return FILEID_INVALID;
		}
	}

	i_pos = fatx_i_pos_read(sbi, inode);
	*lenp = FATX_FID_SIZE_WITHOUT_PARENT;
	fid->i_gen = inode->i_generation;
	fid->i_pos_low = i_pos & 0xFFFFFFFF;
	fid->i_pos_hi = (i_pos >> 32) & 0xFFFF;
	if (parent) {
		i_pos = fatx_i_pos_read(sbi, parent);
		fid->parent_i_pos_hi = (i_pos >> 32) & 0xFFFF;
		fid->parent_i_pos_low = i_pos & 0xFFFFFFFF;
		fid->parent_i_gen = parent->i_generation;
		type = FILEID_FAT_WITH_PARENT;
		*lenp = FATX_FID_SIZE_WITH_PARENT;
	}

	return type;
}

/**
 * Map a NFS file handle to a corresponding dentry.
 * The dentry may or may not be connected to the filesystem root.
 */
static struct dentry *fatx_fh_to_dentry(struct super_block *sb, struct fid *fid,
				int fh_len, int fh_type)
{
	return generic_fh_to_dentry(sb, fid, fh_len, fh_type,
				    fatx_nfs_get_inode);
}

static struct dentry *fatx_fh_to_dentry_nostale(struct super_block *sb,
					       struct fid *fh, int fh_len,
					       int fh_type)
{
	struct inode *inode = NULL;
	struct fatx_fid *fid = (struct fatx_fid *)fh;
	loff_t i_pos;

	switch (fh_type) {
	case FILEID_FAT_WITHOUT_PARENT:
		if (fh_len < FATX_FID_SIZE_WITHOUT_PARENT)
			return NULL;
		break;
	case FILEID_FAT_WITH_PARENT:
		if (fh_len < FATX_FID_SIZE_WITH_PARENT)
			return NULL;
		break;
	default:
		return NULL;
	}
	i_pos = fid->i_pos_hi;
	i_pos = (i_pos << 32) | (fid->i_pos_low);
	inode = __fatx_nfs_get_inode(sb, 0, fid->i_gen, i_pos);

	return d_obtain_alias(inode);
}

/*
 * Find the parent for a file specified by NFS handle.
 * This requires that the handle contain the i_ino of the parent.
 */
static struct dentry *fatx_fh_to_parent(struct super_block *sb, struct fid *fid,
				int fh_len, int fh_type)
{
	return generic_fh_to_parent(sb, fid, fh_len, fh_type,
				    fatx_nfs_get_inode);
}

static struct dentry *fatx_fh_to_parent_nostale(struct super_block *sb,
					       struct fid *fh, int fh_len,
					       int fh_type)
{
	struct inode *inode = NULL;
	struct fatx_fid *fid = (struct fatx_fid *)fh;
	loff_t i_pos;

	if (fh_len < FATX_FID_SIZE_WITH_PARENT)
		return NULL;

	switch (fh_type) {
	case FILEID_FAT_WITH_PARENT:
		i_pos = fid->parent_i_pos_hi;
		i_pos = (i_pos << 32) | (fid->parent_i_pos_low);
		inode = __fatx_nfs_get_inode(sb, 0, fid->parent_i_gen, i_pos);
		break;
	}

	return d_obtain_alias(inode);
}

/*
 * Rebuild the parent for a directory that is not connected
 *  to the filesystem root
 */
static
struct inode *fatx_rebuild_parent(struct super_block *sb, int parent_logstart)
{
	int search_clus, clus_to_match;
	struct fatx_dir_entry *de;
	struct inode *parent = NULL;
	struct inode *dummy_grand_parent = NULL;
	struct fatx_slot_info sinfo;
	struct fatx_sb_info *sbi = FATX_SB(sb);
	sector_t blknr = fatx_clus_to_blknr(sbi, parent_logstart);
	struct buffer_head *parent_bh = sb_bread(sb, blknr);
	if (!parent_bh) {
		fatx_msg(sb, KERN_ERR,
			"unable to read cluster of parent directory");
		return NULL;
	}

	de = (struct fatx_dir_entry *) parent_bh->b_data;
	clus_to_match = fatx_get_start(sbi, &de[0]);
	search_clus = fatx_get_start(sbi, &de[1]);

	dummy_grand_parent = fatx_dget(sb, search_clus);
	if (!dummy_grand_parent) {
		dummy_grand_parent = new_inode(sb);
		if (!dummy_grand_parent) {
			brelse(parent_bh);
			return parent;
		}

		dummy_grand_parent->i_ino = iunique(sb, FATX_ROOT_INO);
		fatx_fill_inode(dummy_grand_parent, &de[1]);
		FATX_I(dummy_grand_parent)->i_pos = -1;
	}

	if (!fatx_scan_logstart(dummy_grand_parent, clus_to_match, &sinfo))
		parent = fatx_build_inode(sb, sinfo.de, sinfo.i_pos);

	brelse(parent_bh);
	iput(dummy_grand_parent);

	return parent;
}

/*
 * Find the parent for a directory that is not currently connected to
 * the filesystem root.
 *
 * On entry, the caller holds child_dir->d_inode->i_mutex.
 */
static struct dentry *fatx_get_parent(struct dentry *child_dir)
{
	struct super_block *sb = child_dir->d_sb;
	struct buffer_head *bh = NULL;
	struct fatx_dir_entry *de;
	struct inode *parent_inode = NULL;
	struct fatx_sb_info *sbi = FATX_SB(sb);

	if (!fatx_get_dotdot_entry(child_dir->d_inode, &bh, &de)) {
		int parent_logstart = fatx_get_start(sbi, de);
		parent_inode = fatx_dget(sb, parent_logstart);
		if (!parent_inode && sbi->options.nfs == FATX_NFS_NOSTALE_RO)
			parent_inode = fatx_rebuild_parent(sb, parent_logstart);
	}
	brelse(bh);

	return d_obtain_alias(parent_inode);
}

const struct export_operations fatx_export_ops = {
	.fh_to_dentry   = fatx_fh_to_dentry,
	.fh_to_parent   = fatx_fh_to_parent,
	.get_parent     = fatx_get_parent,
};

const struct export_operations fatx_export_ops_nostale = {
	.encode_fh      = fatx_encode_fh_nostale,
	.fh_to_dentry   = fatx_fh_to_dentry_nostale,
	.fh_to_parent   = fatx_fh_to_parent_nostale,
	.get_parent     = fatx_get_parent,
};
