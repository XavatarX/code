#include <linux/dcache.h>
#include <linux/namei.h>
#include <linux/mount.h>
#include <linux/fs_stack.h>
#include <linux/slab.h>
#include "tierfs_kernel.h"

/**
 * tierfs_d_revalidate - revalidate an ecryptfs dentry
 * @dentry: The tierfs dentry
 * @flags: lookup flags
 *
 * Called when the VFS needs to revalidate a dentry. This
 * is called whenever a name lookup finds a dentry in the
 * dcache. Most filesystems leave this as NULL, because all their
 * dentries in the dcache are valid.
 *
 * Returns 1 if valid, 0 otherwise.
 *
 */
static int tierfs_d_revalidate(struct dentry *dentry, unsigned int flags)
{
	struct dentry *lower_dentry;
	int rc = 1;

	TRACE_ENTRY();
	if (flags & LOOKUP_RCU)
		return -ECHILD;

	lower_dentry = tierfs_dentry_to_lower(dentry);
	if (!lower_dentry->d_op || !lower_dentry->d_op->d_revalidate)
		goto out;
	rc = lower_dentry->d_op->d_revalidate(lower_dentry, flags);
	if (dentry->d_inode) {
		struct inode *lower_inode =
			tierfs_inode_to_lower(dentry->d_inode);

		fsstack_copy_attr_all(dentry->d_inode, lower_inode);
	}
out:
	TRACE_EXIT();
	return rc;
}

struct kmem_cache *tierfs_dentry_info_cache;

/**
 * tierfs_d_release
 * @dentry: The tierfs dentry
 *
 * Called when a dentry is really deallocated.
 */
static void tierfs_d_release(struct dentry *dentry)
{
	TRACE_ENTRY();
	if (tierfs_dentry_to_private(dentry)) {
		if (tierfs_dentry_to_lower(dentry)) {
			dput(tierfs_dentry_to_lower(dentry));
			mntput(tierfs_dentry_to_lower_mnt(dentry));
		}
		kmem_cache_free(tierfs_dentry_info_cache,
				tierfs_dentry_to_private(dentry));
	}
	TRACE_EXIT();
	return;
}

const struct dentry_operations tierfs_dops = {
	.d_revalidate = tierfs_d_revalidate,
	.d_release = tierfs_d_release,
};
