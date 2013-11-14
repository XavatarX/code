

#include <linux/fs.h>
#include <linux/mount.h>
#include <linux/key.h>
#include <linux/slab.h>
#include <linux/seq_file.h>
#include <linux/file.h>
#include <linux/crypto.h>
#include <linux/statfs.h>
#include <linux/magic.h>
#include "tierfs_kernel.h"

struct kmem_cache *tierfs_inode_info_cache;

static struct inode *tierfs_alloc_inode(struct super_block *sb)
{
	struct tierfs_inode_info *inode_info;
	struct inode *inode = NULL;

	TRACE_ENTRY();
	inode_info = kmem_cache_alloc(tierfs_inode_info_cache, GFP_KERNEL);
	if (unlikely(!inode_info))
		goto out;
	mutex_init(&inode_info->lower_file_mutex);
	atomic_set(&inode_info->lower_file_count, 0);
	inode_info->lower_file = NULL;
	inode = &inode_info->vfs_inode;
	
out:
	TRACE_EXIT();
	return inode;
}
static void tierfs_i_callback(struct rcu_head *head)
{
	struct inode *inode = container_of(head, struct inode, i_rcu);
	struct tierfs_inode_info *inode_info;
	TRACE_ENTRY();
	inode_info = tierfs_inode_to_private(inode);

	kmem_cache_free(tierfs_inode_info_cache, inode_info);
	TRACE_EXIT();
}

/**
 * tierfs_destroy_inode
 * @inode: The tierfs inode
 *
 * This is used during the final destruction of the inode.  All
 * allocation of memory related to the inode, including allocated
 * memory in the crypt_stat struct, will be released here.
 * There should be no chance that this deallocation will be missed.
 */
static void tierfs_destroy_inode(struct inode *inode)
{
	struct tierfs_inode_info *inode_info;

	TRACE_ENTRY();
	inode_info = tierfs_inode_to_private(inode);
	BUG_ON(inode_info->lower_file);
	call_rcu(&inode->i_rcu, tierfs_i_callback);
	TRACE_EXIT();
}


/**
 * tierfs_statfs
 * @sb: The tierfs super block
 * @buf: The struct kstatfs to fill in with stats
 *
 * Get the filesystem statistics. Currently, we let this pass right through
 * to the lower filesystem and take no action ourselves.
 */
static int tierfs_statfs(struct dentry *dentry, struct kstatfs *buf)
{
	struct dentry *lower_dentry = tierfs_dentry_to_lower(dentry);
	int rc;

	TRACE_ENTRY();
	if (!lower_dentry->d_sb->s_op->statfs)
		return -ENOSYS;

	rc = lower_dentry->d_sb->s_op->statfs(lower_dentry, buf);
	if (rc)
		return rc;

	buf->f_type = ECRYPTFS_SUPER_MAGIC;
	TRACE_EXIT();
	return rc;
}


/**
 * tierfs_evict_inode
 * @inode - The tierfs inode
 *
 * Called by iput() when the inode reference count reached zero
 * and the inode is not hashed anywhere.  Used to clear anything
 * that needs to be, before the inode is completely destroyed and put
 * on the inode free list. We use this to drop out reference to the
 * lower inode.
 */
static void tierfs_evict_inode(struct inode *inode)
{
	TRACE_ENTRY();
	truncate_inode_pages(&inode->i_data, 0);
	clear_inode(inode);
	iput(tierfs_inode_to_lower(inode));
	TRACE_EXIT();
}

static int tierfs_show_options(struct seq_file *m, struct dentry *root)
{
	TRACE_ENTRY();
	seq_printf(m,"None");
	TRACE_EXIT();
	return 0;
}
const struct super_operations tierfs_sops = {
	.alloc_inode = tierfs_alloc_inode,
	.destroy_inode = tierfs_destroy_inode,
	.statfs = tierfs_statfs,
	.remount_fs = NULL,
	.evict_inode = tierfs_evict_inode,
	.show_options = tierfs_show_options
};
