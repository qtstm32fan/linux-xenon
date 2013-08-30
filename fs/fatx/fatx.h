#ifndef _FATX_H
#define _FATX_H

#include <linux/buffer_head.h>
#include <linux/string.h>
#include <linux/nls.h>
#include <linux/fs.h>
#include <linux/hash.h>
#include <linux/mutex.h>
#include <linux/ratelimit.h>
#include "xbox_fs.h"

/*
 * vfat shortname flags
 */
#define VFAT_SFN_DISPLAY_LOWER	0x0001 /* convert to lowercase for display */
#define VFAT_SFN_DISPLAY_WIN95	0x0002 /* emulate win95 rule for display */
#define VFAT_SFN_DISPLAY_WINNT	0x0004 /* emulate winnt rule for display */
#define VFAT_SFN_CREATE_WIN95	0x0100 /* emulate win95 rule for create */
#define VFAT_SFN_CREATE_WINNT	0x0200 /* emulate winnt rule for create */

#define FATX_ERRORS_CONT		1      /* ignore error and continue */
#define FATX_ERRORS_PANIC	2      /* panic on error */
#define FATX_ERRORS_RO		3      /* remount r/o on error */

#define FATX_NFS_STALE_RW	1      /* NFS RW support, can cause ESTALE */
#define FATX_NFS_NOSTALE_RO	2      /* NFS RO support, no ESTALE issue */

struct fatx_mount_options {
	kuid_t fs_uid;
	kgid_t fs_gid;
	unsigned short fs_fmask;
	unsigned short fs_dmask;
	unsigned short codepage;   /* Codepage for shortname conversions */
	int time_offset;	   /* Offset of timestamps from UTC (in minutes) */
	char *iocharset;           /* Charset used for filename input/display */
	unsigned short shortname;  /* flags for shortname display/create rule */
	unsigned char name_check;  /* r = relaxed, n = normal, s = strict */
	unsigned char errors;	   /* On error: continue, panic, remount-ro */
	unsigned char nfs;	  /* NFS support: nostale_ro, stale_rw */
	unsigned short allow_utime;/* permission for setting the [am]time */
	unsigned quiet:1,          /* set = fake successful chmods and chowns */
		 showexec:1,       /* set = only set x bit for com/exe/bat */
		 sys_immutable:1,  /* set = system files are immutable */
		 dotsOK:1,         /* set = hidden and system files are named '.filename' */
		 isvfat:1,         /* 0=no vfat long filename support, 1=vfat support */
		 utf8:1,	   /* Use of UTF-8 character set (Default) */
		 unicode_xlate:1,  /* create escape sequences for unhandled Unicode */
		 numtail:1,        /* Does first alias have a numeric '~1' type tail? */
		 flush:1,	   /* write things quickly */
		 nocase:1,	   /* Does this need case conversion? 0=need case conversion*/
		 usefree:1,	   /* Use free_clusters for FATX32 */
		 tz_set:1,	   /* Filesystem timestamps' offset set */
		 rodir:1,	   /* allow ATTR_RO for directory */
		 discard:1;	   /* Issue discard requests on deletions */
};

#define FATX_HASH_BITS	8
#define FATX_HASH_SIZE	(1UL << FATX_HASH_BITS)

/*
 * MS-DOS file system in-core superblock data
 */
struct fatx_sb_info {
	unsigned short sec_per_clus;  /* sectors/cluster */
	unsigned short cluster_bits;  /* log2(cluster_size) */
	unsigned int cluster_size;    /* cluster size */
	unsigned char fatxs, fatx_bits; /* number of FATXs, FATX bits (12 or 16) */
	unsigned short fatx_start;
	unsigned long fatx_length;     /* FATX start & length (sec.) */
	unsigned long dir_start;
	unsigned short dir_entries;   /* root dir start & entries */
	unsigned long data_start;     /* first data sector */
	unsigned long max_cluster;    /* maximum cluster number */
	unsigned long root_cluster;   /* first cluster of the root directory */
	unsigned long fsinfo_sector;  /* sector number of FATX32 fsinfo */
	struct mutex fatx_lock;
	struct mutex nfs_build_inode_lock;
	struct mutex s_lock;
	unsigned int prev_free;      /* previously allocated cluster number */
	unsigned int free_clusters;  /* -1 if undefined */
	unsigned int free_clus_valid; /* is free_clusters valid? */
	struct fatx_mount_options options;
	struct nls_table *nls_disk;   /* Codepage used on disk */
	struct nls_table *nls_io;     /* Charset used for input and display */
	const void *dir_ops;	      /* Opaque; default directory operations */
	int dir_per_block;	      /* dir entries per block */
	int dir_per_block_bits;	      /* log2(dir_per_block) */

	int fatxent_shift;
	struct fatxent_operations *fatxent_ops;
	struct inode *fatx_inode;
	struct inode *fsinfo_inode;

	struct ratelimit_state ratelimit;

	spinlock_t inode_hash_lock;
	struct hlist_head inode_hashtable[FATX_HASH_SIZE];

	spinlock_t dir_hash_lock;
	struct hlist_head dir_hashtable[FATX_HASH_SIZE];

	unsigned int dirty;           /* fs state before mount */
};

#define FATX_CACHE_VALID	0	/* special case for valid cache */

/*
 * MS-DOS file system inode data in memory
 */
struct fatx_inode_info {
	spinlock_t cache_lru_lock;
	struct list_head cache_lru;
	int nr_caches;
	/* for avoiding the race between fatx_free() and fatx_get_cluster() */
	unsigned int cache_valid_id;

	/* NOTE: mmu_private is 64bits, so must hold ->i_mutex to access */
	loff_t mmu_private;	/* physically allocated size */

	int i_start;		/* first cluster or 0 */
	int i_logstart;		/* logical first cluster */
	int i_attrs;		/* unused attribute bits */
	loff_t i_pos;		/* on-disk position of directory entry or 0 */
	struct hlist_node i_fatx_hash;	/* hash by i_location */
	struct hlist_node i_dir_hash;	/* hash by i_logstart */
	struct rw_semaphore truncate_lock; /* protect bmap against truncate */
	struct inode vfs_inode;
};

struct fatx_slot_info {
	loff_t i_pos;		/* on-disk position of directory entry */
	loff_t slot_off;	/* offset for slot or de start */
	int nr_slots;		/* number of slots + 1(de) in filename */
	struct fatx_dir_entry *de;
	struct buffer_head *bh;
};

static inline struct fatx_sb_info *FATX_SB(struct super_block *sb)
{
	return sb->s_fs_info;
}

static inline struct fatx_inode_info *FATX_I(struct inode *inode)
{
	return container_of(inode, struct fatx_inode_info, vfs_inode);
}

/*
 * If ->i_mode can't hold S_IWUGO (i.e. ATTR_RO), we use ->i_attrs to
 * save ATTR_RO instead of ->i_mode.
 *
 * If it's directory and !sbi->options.rodir, ATTR_RO isn't read-only
 * bit, it's just used as flag for app.
 */
static inline int fatx_mode_can_hold_ro(struct inode *inode)
{
	struct fatx_sb_info *sbi = FATX_SB(inode->i_sb);
	umode_t mask;

	if (S_ISDIR(inode->i_mode)) {
		if (!sbi->options.rodir)
			return 0;
		mask = ~sbi->options.fs_dmask;
	} else
		mask = ~sbi->options.fs_fmask;

	if (!(mask & S_IWUGO))
		return 0;
	return 1;
}

/* Convert attribute bits and a mask to the UNIX mode. */
static inline umode_t fatx_make_mode(struct fatx_sb_info *sbi,
				   u8 attrs, umode_t mode)
{
	if (attrs & ATTR_RO && !((attrs & ATTR_DIR) && !sbi->options.rodir))
		mode &= ~S_IWUGO;

	if (attrs & ATTR_DIR)
		return (mode & ~sbi->options.fs_dmask) | S_IFDIR;
	else
		return (mode & ~sbi->options.fs_fmask) | S_IFREG;
}

/* Return the FATX attribute byte for this inode */
static inline u8 fatx_make_attrs(struct inode *inode)
{
	u8 attrs = FATX_I(inode)->i_attrs;
	if (S_ISDIR(inode->i_mode))
		attrs |= ATTR_DIR;
	if (fatx_mode_can_hold_ro(inode) && !(inode->i_mode & S_IWUGO))
		attrs |= ATTR_RO;
	return attrs;
}

static inline void fatx_save_attrs(struct inode *inode, u8 attrs)
{
	if (fatx_mode_can_hold_ro(inode))
		FATX_I(inode)->i_attrs = attrs & ATTR_UNUSED;
	else
		FATX_I(inode)->i_attrs = attrs & (ATTR_UNUSED | ATTR_RO);
}

static inline unsigned char fatx_checksum(const __u8 *name)
{
	unsigned char s = name[0];
	s = (s<<7) + (s>>1) + name[1];	s = (s<<7) + (s>>1) + name[2];
	s = (s<<7) + (s>>1) + name[3];	s = (s<<7) + (s>>1) + name[4];
	s = (s<<7) + (s>>1) + name[5];	s = (s<<7) + (s>>1) + name[6];
	s = (s<<7) + (s>>1) + name[7];	s = (s<<7) + (s>>1) + name[8];
	s = (s<<7) + (s>>1) + name[9];	s = (s<<7) + (s>>1) + name[10];
	return s;
}

static inline sector_t fatx_clus_to_blknr(struct fatx_sb_info *sbi, int clus)
{
	return ((sector_t)clus - FATX_START_ENT) * sbi->sec_per_clus
		+ sbi->data_start;
}

static inline void fatx_get_blknr_offset(struct fatx_sb_info *sbi,
				loff_t i_pos, sector_t *blknr, int *offset)
{
	*blknr = i_pos >> sbi->dir_per_block_bits;
	*offset = i_pos & (sbi->dir_per_block - 1);
}

static inline loff_t fatx_i_pos_read(struct fatx_sb_info *sbi,
					struct inode *inode)
{
	loff_t i_pos;
#if BITS_PER_LONG == 32
	spin_lock(&sbi->inode_hash_lock);
#endif
	i_pos = FATX_I(inode)->i_pos;
#if BITS_PER_LONG == 32
	spin_unlock(&sbi->inode_hash_lock);
#endif
	return i_pos;
}

static inline void fatx16_towchar(wchar_t *dst, const __u8 *src, size_t len)
{
#ifdef __BIG_ENDIAN
	while (len--) {
		*dst++ = src[0] | (src[1] << 8);
		src += 2;
	}
#else
	memcpy(dst, src, len * 2);
#endif
}

static inline int fatx_get_start(const struct fatx_sb_info *sbi,
				const struct fatx_dir_entry *de)
{
	int cluster = le16_to_cpu(de->start);
	if (sbi->fatx_bits == 32)
		cluster |= (le16_to_cpu(de->starthi) << 16);
	return cluster;
}

static inline void fatx_set_start(struct fatx_dir_entry *de, int cluster)
{
	de->start   = cpu_to_le16(cluster);
	de->starthi = cpu_to_le16(cluster >> 16);
}

static inline void fatxwchar_to16(__u8 *dst, const wchar_t *src, size_t len)
{
#ifdef __BIG_ENDIAN
	while (len--) {
		dst[0] = *src & 0x00FF;
		dst[1] = (*src & 0xFF00) >> 8;
		dst += 2;
		src++;
	}
#else
	memcpy(dst, src, len * 2);
#endif
}

/* fatx/cache.c */
extern void fatx_cache_inval_inode(struct inode *inode);
extern int fatx_get_cluster(struct inode *inode, int cluster,
			   int *fclus, int *dclus);
extern int fatx_bmap(struct inode *inode, sector_t sector, sector_t *phys,
		    unsigned long *mapped_blocks, int create);

/* fatx/dir.c */
extern const struct file_operations fatx_dir_operations;
extern int fatx_search_long(struct inode *inode, const unsigned char *name,
			   int name_len, struct fatx_slot_info *sinfo);
extern int fatx_dir_empty(struct inode *dir);
extern int fatx_subdirs(struct inode *dir);
extern int fatx_scan(struct inode *dir, const unsigned char *name,
		    struct fatx_slot_info *sinfo);
extern int fatx_scan_logstart(struct inode *dir, int i_logstart,
			     struct fatx_slot_info *sinfo);
extern int fatx_get_dotdot_entry(struct inode *dir, struct buffer_head **bh,
				struct fatx_dir_entry **de);
extern int fatx_alloc_new_dir(struct inode *dir, struct timespec *ts);
extern int fatx_add_entries(struct inode *dir, void *slots, int nr_slots,
			   struct fatx_slot_info *sinfo);
extern int fatx_remove_entries(struct inode *dir, struct fatx_slot_info *sinfo);

/* fatx/fatxent.c */
struct fatx_entry {
	int entry;
	union {
		u8 *ent12_p[2];
		__le16 *ent16_p;
		__le32 *ent32_p;
	} u;
	int nr_bhs;
	struct buffer_head *bhs[2];
	struct inode *fatx_inode;
};

static inline void fatxent_init(struct fatx_entry *fatxent)
{
	fatxent->nr_bhs = 0;
	fatxent->entry = 0;
	fatxent->u.ent32_p = NULL;
	fatxent->bhs[0] = fatxent->bhs[1] = NULL;
	fatxent->fatx_inode = NULL;
}

static inline void fatxent_set_entry(struct fatx_entry *fatxent, int entry)
{
	fatxent->entry = entry;
	fatxent->u.ent32_p = NULL;
}

static inline void fatxent_brelse(struct fatx_entry *fatxent)
{
	int i;
	fatxent->u.ent32_p = NULL;
	for (i = 0; i < fatxent->nr_bhs; i++)
		brelse(fatxent->bhs[i]);
	fatxent->nr_bhs = 0;
	fatxent->bhs[0] = fatxent->bhs[1] = NULL;
	fatxent->fatx_inode = NULL;
}

extern void fatx_ent_access_init(struct super_block *sb);
extern int fatx_ent_read(struct inode *inode, struct fatx_entry *fatxent,
			int entry);
extern int fatx_ent_write(struct inode *inode, struct fatx_entry *fatxent,
			 int new, int wait);
extern int fatx_alloc_clusters(struct inode *inode, int *cluster,
			      int nr_cluster);
extern int fatx_free_clusters(struct inode *inode, int cluster);
extern int fatx_count_free_clusters(struct super_block *sb);

/* fatx/file.c */
extern long fatx_generic_ioctl(struct file *filp, unsigned int cmd,
			      unsigned long arg);
extern const struct file_operations fatx_file_operations;
extern const struct inode_operations fatx_file_inode_operations;
extern int fatx_setattr(struct dentry *dentry, struct iattr *attr);
extern void fatx_truncate_blocks(struct inode *inode, loff_t offset);
extern int fatx_getattr(struct vfsmount *mnt, struct dentry *dentry,
		       struct kstat *stat);
extern int fatx_file_fsync(struct file *file, loff_t start, loff_t end,
			  int datasync);

/* fatx/inode.c */
extern void fatx_attach(struct inode *inode, loff_t i_pos);
extern void fatx_detach(struct inode *inode);
extern struct inode *fatx_iget(struct super_block *sb, loff_t i_pos);
extern struct inode *fatx_build_inode(struct super_block *sb,
			struct fatx_dir_entry *de, loff_t i_pos);
extern int fatx_sync_inode(struct inode *inode);
extern int fatx_fill_super(struct super_block *sb, void *data, int silent,
			  int isvfat, void (*setup)(struct super_block *));
extern int fatx_fill_inode(struct inode *inode, struct fatx_dir_entry *de);

extern int fatx_flush_inodes(struct super_block *sb, struct inode *i1,
			    struct inode *i2);
static inline unsigned long fatx_dir_hash(int logstart)
{
	return hash_32(logstart, FATX_HASH_BITS);
}

/* fatx/misc.c */
extern __printf(3, 4) __cold
void __fatx_fs_error(struct super_block *sb, int report, const char *fmt, ...);
#define fatx_fs_error(sb, fmt, args...)		\
	__fatx_fs_error(sb, 1, fmt , ## args)
#define fatx_fs_error_ratelimit(sb, fmt, args...) \
	__fatx_fs_error(sb, __ratelimit(&FATX_SB(sb)->ratelimit), fmt , ## args)
__printf(3, 4) __cold
void fatx_msg(struct super_block *sb, const char *level, const char *fmt, ...);
#define fatx_msg_ratelimit(sb, level, fmt, args...)	\
	do {	\
			if (__ratelimit(&FATX_SB(sb)->ratelimit))	\
				fatx_msg(sb, level, fmt, ## args);	\
	 } while (0)
extern int fatx_clusters_flush(struct super_block *sb);
extern int fatx_chain_add(struct inode *inode, int new_dclus, int nr_cluster);
extern void fatx_time_fat2unix(struct fatx_sb_info *sbi, struct timespec *ts,
			      __le16 __time, __le16 __date, u8 time_cs);
extern void fatx_time_unix2fat(struct fatx_sb_info *sbi, struct timespec *ts,
			      __le16 *time, __le16 *date, u8 *time_cs);
extern int fatx_sync_bhs(struct buffer_head **bhs, int nr_bhs);

int fatx_cache_init(void);
void fatx_cache_destroy(void);

/* fatx/nfs.c */
extern const struct export_operations fatx_export_ops;
extern const struct export_operations fatx_export_ops_nostale;

/* helper for printk */
typedef unsigned long long	llu;

#endif /* !_FATX_H */
