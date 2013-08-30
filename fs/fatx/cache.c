/*
 *  linux/fs/fatx/cache.c
 *
 *  Written 1992,1993 by Werner Almesberger
 *
 *  Mar 1999. AV. Changed cache, so that it uses the starting cluster instead
 *	of inode number.
 *  May 1999. AV. Fixed the bogosity with FAT32 (read "FAT28"). Fscking lusers.
 */

#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/buffer_head.h>
#include "fatx.h"

/* this must be > 0. */
#define FATX_MAX_CACHE	8

struct fatx_cache {
	struct list_head cache_list;
	int nr_contig;	/* number of contiguous clusters */
	int fcluster;	/* cluster number in the file. */
	int dcluster;	/* cluster number on disk. */
};

struct fatx_cache_id {
	unsigned int id;
	int nr_contig;
	int fcluster;
	int dcluster;
};

static inline int fatx_max_cache(struct inode *inode)
{
	return FATX_MAX_CACHE;
}

static struct kmem_cache *fatx_cache_cachep;

static void init_once(void *foo)
{
	struct fatx_cache *cache = (struct fatx_cache *)foo;

	INIT_LIST_HEAD(&cache->cache_list);
}

int __init fatx_cache_init(void)
{
	fatx_cache_cachep = kmem_cache_create("fatx_cache",
				sizeof(struct fatx_cache),
				0, SLAB_RECLAIM_ACCOUNT|SLAB_MEM_SPREAD,
				init_once);
	if (fatx_cache_cachep == NULL)
		return -ENOMEM;
	return 0;
}

void fatx_cache_destroy(void)
{
	kmem_cache_destroy(fatx_cache_cachep);
}

static inline struct fatx_cache *fatx_cache_alloc(struct inode *inode)
{
	return kmem_cache_alloc(fatx_cache_cachep, GFP_NOFS);
}

static inline void fatx_cache_free(struct fatx_cache *cache)
{
	BUG_ON(!list_empty(&cache->cache_list));
	kmem_cache_free(fatx_cache_cachep, cache);
}

static inline void fatx_cache_update_lru(struct inode *inode,
					struct fatx_cache *cache)
{
	if (FATX_I(inode)->cache_lru.next != &cache->cache_list)
		list_move(&cache->cache_list, &FATX_I(inode)->cache_lru);
}

static int fatx_cache_lookup(struct inode *inode, int fclus,
			    struct fatx_cache_id *cid,
			    int *cached_fclus, int *cached_dclus)
{
	static struct fatx_cache nohit = { .fcluster = 0, };

	struct fatx_cache *hit = &nohit, *p;
	int offset = -1;

	spin_lock(&FATX_I(inode)->cache_lru_lock);
	list_for_each_entry(p, &FATX_I(inode)->cache_lru, cache_list) {
		/* Find the cache of "fclus" or nearest cache. */
		if (p->fcluster <= fclus && hit->fcluster < p->fcluster) {
			hit = p;
			if ((hit->fcluster + hit->nr_contig) < fclus) {
				offset = hit->nr_contig;
			} else {
				offset = fclus - hit->fcluster;
				break;
			}
		}
	}
	if (hit != &nohit) {
		fatx_cache_update_lru(inode, hit);

		cid->id = FATX_I(inode)->cache_valid_id;
		cid->nr_contig = hit->nr_contig;
		cid->fcluster = hit->fcluster;
		cid->dcluster = hit->dcluster;
		*cached_fclus = cid->fcluster + offset;
		*cached_dclus = cid->dcluster + offset;
	}
	spin_unlock(&FATX_I(inode)->cache_lru_lock);

	return offset;
}

static struct fatx_cache *fatx_cache_merge(struct inode *inode,
					 struct fatx_cache_id *new)
{
	struct fatx_cache *p;

	list_for_each_entry(p, &FATX_I(inode)->cache_lru, cache_list) {
		/* Find the same part as "new" in cluster-chain. */
		if (p->fcluster == new->fcluster) {
			BUG_ON(p->dcluster != new->dcluster);
			if (new->nr_contig > p->nr_contig)
				p->nr_contig = new->nr_contig;
			return p;
		}
	}
	return NULL;
}

static void fatx_cache_add(struct inode *inode, struct fatx_cache_id *new)
{
	struct fatx_cache *cache, *tmp;

	if (new->fcluster == -1) /* dummy cache */
		return;

	spin_lock(&FATX_I(inode)->cache_lru_lock);
	if (new->id != FATX_CACHE_VALID &&
	    new->id != FATX_I(inode)->cache_valid_id)
		goto out;	/* this cache was invalidated */

	cache = fatx_cache_merge(inode, new);
	if (cache == NULL) {
		if (FATX_I(inode)->nr_caches < fatx_max_cache(inode)) {
			FATX_I(inode)->nr_caches++;
			spin_unlock(&FATX_I(inode)->cache_lru_lock);

			tmp = fatx_cache_alloc(inode);
			if (!tmp) {
				spin_lock(&FATX_I(inode)->cache_lru_lock);
				FATX_I(inode)->nr_caches--;
				spin_unlock(&FATX_I(inode)->cache_lru_lock);
				return;
			}

			spin_lock(&FATX_I(inode)->cache_lru_lock);
			cache = fatx_cache_merge(inode, new);
			if (cache != NULL) {
				FATX_I(inode)->nr_caches--;
				fatx_cache_free(tmp);
				goto out_update_lru;
			}
			cache = tmp;
		} else {
			struct list_head *p = FATX_I(inode)->cache_lru.prev;
			cache = list_entry(p, struct fatx_cache, cache_list);
		}
		cache->fcluster = new->fcluster;
		cache->dcluster = new->dcluster;
		cache->nr_contig = new->nr_contig;
	}
out_update_lru:
	fatx_cache_update_lru(inode, cache);
out:
	spin_unlock(&FATX_I(inode)->cache_lru_lock);
}

/*
 * Cache invalidation occurs rarely, thus the LRU chain is not updated. It
 * fixes itself after a while.
 */
static void __fatx_cache_inval_inode(struct inode *inode)
{
	struct fatx_inode_info *i = FATX_I(inode);
	struct fatx_cache *cache;

	while (!list_empty(&i->cache_lru)) {
		cache = list_entry(i->cache_lru.next,
				   struct fatx_cache, cache_list);
		list_del_init(&cache->cache_list);
		i->nr_caches--;
		fatx_cache_free(cache);
	}
	/* Update. The copy of caches before this id is discarded. */
	i->cache_valid_id++;
	if (i->cache_valid_id == FATX_CACHE_VALID)
		i->cache_valid_id++;
}

void fatx_cache_inval_inode(struct inode *inode)
{
	spin_lock(&FATX_I(inode)->cache_lru_lock);
	__fatx_cache_inval_inode(inode);
	spin_unlock(&FATX_I(inode)->cache_lru_lock);
}

static inline int cache_contiguous(struct fatx_cache_id *cid, int dclus)
{
	cid->nr_contig++;
	return ((cid->dcluster + cid->nr_contig) == dclus);
}

static inline void cache_init(struct fatx_cache_id *cid, int fclus, int dclus)
{
	cid->id = FATX_CACHE_VALID;
	cid->fcluster = fclus;
	cid->dcluster = dclus;
	cid->nr_contig = 0;
}

int fatx_get_cluster(struct inode *inode, int cluster, int *fclus, int *dclus)
{
	struct super_block *sb = inode->i_sb;
	const int limit = sb->s_maxbytes >> FATX_SB(sb)->cluster_bits;
	struct fatx_entry fatxent;
	struct fatx_cache_id cid;
	int nr;

	BUG_ON(FATX_I(inode)->i_start == 0);

	*fclus = 0;
	*dclus = FATX_I(inode)->i_start;
	if (cluster == 0)
		return 0;

	if (fatx_cache_lookup(inode, cluster, &cid, fclus, dclus) < 0) {
		/*
		 * dummy, always not contiguous
		 * This is reinitialized by cache_init(), later.
		 */
		cache_init(&cid, -1, -1);
	}

	fatxent_init(&fatxent);
	while (*fclus < cluster) {
		/* prevent the infinite loop of cluster chain */
		if (*fclus > limit) {
			fatx_fs_error_ratelimit(sb,
					"%s: detected the cluster chain loop"
					" (i_pos %lld)", __func__,
					FATX_I(inode)->i_pos);
			nr = -EIO;
			goto out;
		}

		nr = fatx_ent_read(inode, &fatxent, *dclus);
		if (nr < 0)
			goto out;
		else if (nr == FATX_ENT_FREE) {
			fatx_fs_error_ratelimit(sb,
				       "%s: invalid cluster chain (i_pos %lld)",
				       __func__,
				       FATX_I(inode)->i_pos);
			nr = -EIO;
			goto out;
		} else if (nr == FATX_ENT_EOF) {
			fatx_cache_add(inode, &cid);
			goto out;
		}
		(*fclus)++;
		*dclus = nr;
		if (!cache_contiguous(&cid, *dclus))
			cache_init(&cid, *fclus, *dclus);
	}
	nr = 0;
	fatx_cache_add(inode, &cid);
out:
	fatxent_brelse(&fatxent);
	return nr;
}

static int fatx_bmap_cluster(struct inode *inode, int cluster)
{
	struct super_block *sb = inode->i_sb;
	int ret, fclus, dclus;

	if (FATX_I(inode)->i_start == 0)
		return 0;

	ret = fatx_get_cluster(inode, cluster, &fclus, &dclus);
	if (ret < 0)
		return ret;
	else if (ret == FATX_ENT_EOF) {
		fatx_fs_error(sb, "%s: request beyond EOF (i_pos %lld)",
			     __func__, FATX_I(inode)->i_pos);
		return -EIO;
	}
	return dclus;
}

int fatx_bmap(struct inode *inode, sector_t sector, sector_t *phys,
	     unsigned long *mapped_blocks, int create)
{
	struct super_block *sb = inode->i_sb;
	struct fatx_sb_info *sbi = FATX_SB(sb);
	const unsigned long blocksize = sb->s_blocksize;
	const unsigned char blocksize_bits = sb->s_blocksize_bits;
	sector_t last_block;
	int cluster, offset;

	*phys = 0;
	*mapped_blocks = 0;
	if ((sbi->fatx_bits != 32) && (inode->i_ino == FATX_ROOT_INO)) {
		if (sector < (sbi->dir_entries >> sbi->dir_per_block_bits)) {
			*phys = sector + sbi->dir_start;
			*mapped_blocks = 1;
		}
		return 0;
	}

	last_block = (i_size_read(inode) + (blocksize - 1)) >> blocksize_bits;
	if (sector >= last_block) {
		if (!create)
			return 0;

		/*
		 * ->mmu_private can access on only allocation path.
		 * (caller must hold ->i_mutex)
		 */
		last_block = (FATX_I(inode)->mmu_private + (blocksize - 1))
			>> blocksize_bits;
		if (sector >= last_block)
			return 0;
	}

	cluster = sector >> (sbi->cluster_bits - sb->s_blocksize_bits);
	offset  = sector & (sbi->sec_per_clus - 1);
	cluster = fatx_bmap_cluster(inode, cluster);
	if (cluster < 0)
		return cluster;
	else if (cluster) {
		*phys = fatx_clus_to_blknr(sbi, cluster) + offset;
		*mapped_blocks = sbi->sec_per_clus - offset;
		if (*mapped_blocks > last_block - sector)
			*mapped_blocks = last_block - sector;
	}
	return 0;
}
