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
/* superblock private data. */
struct tierfs_sb_info {
	struct super_block *wsi_sb;
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

static inline struct tierfs_sb_info *
tierfs_superblock_to_private(struct super_block *sb)
{
	return (struct tierfs_sb_info *)sb->s_fs_info;
}

static inline void
tierfs_set_superblock_private(struct super_block *sb,
				struct tierfs_sb_info *sb_info)
{
	sb->s_fs_info = sb_info;
}

static inline struct super_block *
tierfs_superblock_to_lower(struct super_block *sb)
{
	return ((struct tierfs_sb_info *)sb->s_fs_info)->wsi_sb;
}

static inline void
tierfs_set_superblock_lower(struct super_block *sb,
			      struct super_block *lower_sb)
{
	((struct tierfs_sb_info *)sb->s_fs_info)->wsi_sb = lower_sb;
}

static inline struct tierfs_dentry_info *
tierfs_dentry_to_private(struct dentry *dentry)
{
	return (struct tierfs_dentry_info *)dentry->d_fsdata;
}

static inline void
tierfs_set_dentry_private(struct dentry *dentry,
			    struct tierfs_dentry_info *dentry_info)
{
	dentry->d_fsdata = dentry_info;
}

static inline struct dentry *
tierfs_dentry_to_lower(struct dentry *dentry)
{
	return ((struct tierfs_dentry_info *)dentry->d_fsdata)->lower_path.dentry;
}

static inline void
tierfs_set_dentry_lower(struct dentry *dentry, struct dentry *lower_dentry)
{
	((struct tierfs_dentry_info *)dentry->d_fsdata)->lower_path.dentry =
		lower_dentry;
}

static inline struct vfsmount *
tierfs_dentry_to_lower_mnt(struct dentry *dentry)
{
	return ((struct tierfs_dentry_info *)dentry->d_fsdata)->lower_path.mnt;
}

static inline struct path *
tierfs_dentry_to_lower_path(struct dentry *dentry)
{
	return &((struct tierfs_dentry_info *)dentry->d_fsdata)->lower_path;
}

static inline void
tierfs_set_dentry_lower_mnt(struct dentry *dentry, struct vfsmount *lower_mnt)
{
	((struct tierfs_dentry_info *)dentry->d_fsdata)->lower_path.mnt =
		lower_mnt;
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
