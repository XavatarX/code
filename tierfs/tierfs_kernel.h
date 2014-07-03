#ifndef _TIERFS_KERNEL_H_

#define _TIERFS_KERNEL_H_
#include <linux/fs.h>
#include <linux/fs_stack.h>
#include <linux/namei.h>
#include <linux/scatterlist.h>
#include <linux/hash.h>
#include <linux/nsproxy.h>
#include <linux/backing-dev.h>

#define TIERFS_SUPER_MAGIC (0xC0FFEE)

#define MAX_SUPPORTED_TIER 4
#define TFS_MAX_PATH_LEN PATH_MAX + 1

typedef enum tfs_tier_type {
	TFS_TIER_TYPE_PRIMARY,
	TFS_TIER_TYPE_SECONDARY
} tfs_tier_type_t;

typedef struct tfs_tier {
	tfs_tier_type_t tier_type;
	char tier_path[TFS_MAX_PATH_LEN];
	struct path tier_kpath;
} tfs_tier_t;

typedef struct tfs_tier_list {
	int ntiers;
	int nkpaths;
	tfs_tier_t tiers[MAX_SUPPORTED_TIER];
} tfs_tier_list_t;

/* superblock private data. */
struct tierfs_sb_info {
	int nsbs;
	struct super_block *wsi_sb[MAX_SUPPORTED_TIER];
	struct backing_dev_info bdi;
};

struct tierfs_file_stat {
	u32 flags;
};
/* file private data. */
struct tierfs_file_info {
	struct file *wfi_file;
	struct tierfs_file_stat *crypt_stat;
};
/* inode private data. */
struct tierfs_inode_info {
	struct inode vfs_inode;
	struct inode *wii_inode;
	struct mutex lower_file_mutex;
	atomic_t lower_file_count;
	struct file *lower_file;
};

/* dentry private data. Each dentry must keep track of a lower
 * vfsmount too. */
struct tierfs_dentry_info {
	struct path lower_path;
};

#if 0
static inline size_t
tierfs_lower_header_size(struct tierfs_crypt_stat *crypt_stat)
{
	if (crypt_stat->flags & ECRYPTFS_METADATA_IN_XATTR)
		return 0;
	return crypt_stat->metadata_size;
}
#endif
int tierfs_get_lower_file(struct dentry *dentry, struct inode *inode);
void tierfs_put_lower_file(struct inode *inode);
int tierfs_write(struct inode *ecryptfs_inode, char *data, loff_t offset,
		   size_t size);
int tierfs_write_lower(struct inode *ecryptfs_inode, char *data,
			 loff_t offset, size_t size);
int tierfs_read_lower_page_segment(struct page *page_for_ecryptfs,
				     pgoff_t page_index,
				     size_t offset_in_page, size_t size,
				     struct inode *tierfs_inode);
int tierfs_truncate(struct dentry *dentry, loff_t new_length);
int tierfs_write_lower_page_segment(struct inode *ecryptfs_inode,
				      struct page *page_for_lower,
				      size_t offset_in_page, size_t size);
struct page *tierfs_get_locked_page(struct inode *inode, loff_t index);


static inline struct tierfs_file_info *
tierfs_file_to_private(struct file *file)
{
	return file->private_data;
}

static inline void
tierfs_set_file_private(struct file *file,
			  struct tierfs_file_info *file_info)
{
	file->private_data = file_info;
}

static inline struct file *tierfs_file_to_lower(struct file *file)
{
	return ((struct tierfs_file_info *)file->private_data)->wfi_file;
}

static inline void
tierfs_set_file_lower(struct file *file, struct file *lower_file)
{
	((struct tierfs_file_info *)file->private_data)->wfi_file =
		lower_file;
}
struct inode *tierfs_get_inode(struct inode *lower_inode,
				 struct super_block *sb);

static inline struct tierfs_inode_info *
tierfs_inode_to_private(struct inode *inode)
{
	return container_of(inode, struct tierfs_inode_info, vfs_inode);
}

static inline struct inode *tierfs_inode_to_lower(struct inode *inode)
{
	return tierfs_inode_to_private(inode)->wii_inode;
}

static inline void
tierfs_set_inode_lower(struct inode *inode, struct inode *lower_inode)
{
	tierfs_inode_to_private(inode)->wii_inode = lower_inode;
}

/*
 * ===============================================
 *     Superblock related functions and macros
 * ===============================================
 */

#define tierfs_superblock_to_private(tsb) ((struct tierfs_sb_info *)tsb->s_fs_info)
#define tierfs_tsb_to_lsb(tsb, i) tierfs_superblock_to_private(tsb)->wsi_sb[i]
#define tierfs_nlsbs(tsb) tierfs_superblock_to_private(tsb)->nsbs
#define tierfs_set_superblock_private(tsb, tsb_info) (tsb->s_fs_info = tsb_info)

static inline struct super_block *
tierfs_lookup_superblock_lower_uuid(struct super_block *tsb, u8 *lsb_uuid)
{
	int i;

	for (i = 0; i < tierfs_nlsbs(tsb); i++) {
		if (memcmp(lsb_uuid, tierfs_tsb_to_lsb(tsb, i)->s_uuid, 16)) {
			return tierfs_tsb_to_lsb(tsb, i);
		}
	}

	return NULL;
}

static inline struct super_block *
tierfs_lookup_superblock_lower(struct super_block *tsb, struct super_block *lsb)
{
	return tierfs_lookup_superblock_lower_uuid(tsb, lsb->s_uuid);
}

static inline void
tierfs_set_superblock_lower(struct super_block *tsb,
			      struct super_block *lsb)
{
	if (MAX_SUPPORTED_TIER <= tierfs_nlsbs(tsb))
		return;
	tierfs_tsb_to_lsb(tsb, tierfs_nlsbs(tsb)) = lsb;
}


/*
 * ===============================================
 *     dentry related functions and macros
 * ===============================================
 */

#define tierfs_dentry_to_private(tdentry) \
		((struct tierfs_dentry_info *)tdentry->d_fsdata)

#define tierfs_dentry_to_lower_path(tdentry) \
		(&tierfs_dentry_to_private(tdentry)->lower_path)

#define tierfs_dentry_to_lower(tdentry) \
		tierfs_dentry_to_lower_path(tdentry)->dentry

#define tierfs_dentry_to_lower_mnt(tdentry) \
		tierfs_dentry_to_lower_path(tdentry)->mnt

#define tierfs_set_dentry_private(tdentry, tdentry_info) \
		tdentry->d_fsdata = tdentry_info

#define tierfs_set_dentry_lower_dentry(tdentry, ldentry) \
		tierfs_dentry_to_lower(tdentry) = ldentry

#define tierfs_set_dentry_lower_mnt(tdentry, lmnt) \
		tierfs_dentry_to_lower_mnt(tdentry) = lmnt

static inline void
tierfs_set_dentry_lower_path(struct dentry *tfs_dentry, struct path *lower_path)
{
	tierfs_set_dentry_lower_dentry(tfs_dentry, lower_path->dentry);
	tierfs_set_dentry_lower_mnt(tfs_dentry, lower_path->mnt);
}

#define tierfs_printk(type, fmt, arg...) \
        __tierfs_printk(type "%s: " fmt, __func__, ## arg);
__printf(1, 2)
void __tierfs_printk(const char *fmt, ...);

#define TRACE_ENTRY()	tierfs_printk(KERN_DEBUG, "Entering \n")
#define TRACE_EXIT()	tierfs_printk(KERN_DEBUG, "Exiting\n")

extern const struct file_operations tierfs_main_fops;
extern const struct file_operations tierfs_dir_fops;
extern const struct inode_operations tierfs_main_iops;
extern const struct inode_operations tierfs_dir_iops;
extern const struct inode_operations tierfs_symlink_iops;
extern const struct super_operations tierfs_sops;
extern const struct dentry_operations tierfs_dops;
extern const struct address_space_operations tierfs_aops;

extern struct kmem_cache *tierfs_file_info_cache;
extern struct kmem_cache *tierfs_dentry_info_cache;
extern struct kmem_cache *tierfs_inode_info_cache;
extern struct kmem_cache *tierfs_sb_info_cache;
extern struct kmem_cache *tierfs_header_cache;

#endif 
