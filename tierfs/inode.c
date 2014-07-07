#include <linux/file.h>
#include <linux/vmalloc.h>
#include <linux/pagemap.h>
#include <linux/dcache.h>
#include <linux/namei.h>
#include <linux/mount.h>
#include <linux/crypto.h>
#include <linux/fs_stack.h>
#include <linux/slab.h>
#include <linux/xattr.h>
#include <asm/unaligned.h>
#include "tierfs_kernel.h"

static struct dentry *lock_parent(struct dentry *dentry)
{
	struct dentry *dir;

	TRACE_ENTRY();
	dir = dget_parent(dentry);
	mutex_lock_nested(&(dir->d_inode->i_mutex), i_mutex_parent);
	TRACE_EXIT();
	return dir;
}

static void unlock_dir(struct dentry *dir)
{
	TRACE_ENTRY();
	mutex_unlock(&dir->d_inode->i_mutex);
	dput(dir);
	TRACE_EXIT();
}

static int tierfs_inode_test(struct inode *inode, void *lower_inode)
{
	TRACE_ENTRY();
	if (tierfs_inode_to_lower(inode) == (struct inode *)lower_inode)
		return 1;
	TRACE_EXIT();
	return 0;
}
// inode is a tierfs_inode.
static int tierfs_inode_set(struct inode *inode, void *opaque)
{
	struct inode *lower_inode = opaque;

	TRACE_ENTRY();
	tierfs_set_inode_lower(inode, lower_inode);
	fsstack_copy_attr_all(inode, lower_inode);
#if 0
	/* i_size will be overwritten for encrypted regular files */
	fsstack_copy_inode_size(inode, lower_inode);
#endif
	inode->i_ino = lower_inode->i_ino;
	inode->i_version++;
	inode->i_mapping->a_ops = &tierfs_aops;
	inode->i_mapping->backing_dev_info = inode->i_sb->s_bdi;

	if (S_ISLNK(inode->i_mode))
		inode->i_op = &tierfs_symlink_iops;
	else if (S_ISDIR(inode->i_mode))
		inode->i_op = &tierfs_dir_iops;
	else
		inode->i_op = &tierfs_main_iops;

	if (S_ISDIR(inode->i_mode))
		inode->i_fop = &tierfs_dir_fops;
	else if (special_file(inode->i_mode))
		init_special_inode(inode, inode->i_mode, inode->i_rdev);
	else
		inode->i_fop = &tierfs_main_fops;

	TRACE_EXIT();

	return 0;
}


static struct inode *__tierfs_get_inode(struct inode *lower_inode,
					  struct super_block *tsb)
{
	struct inode *tfs_inode;

	TRACE_ENTRY();

	//if (!tierfs_lookup_superblock_lower(tsb, lower_inode->i_sb))
	//	return ERR_PTR(-EXDEV);
	print_uuid(lower_inode->i_sb->s_uuid);

	if (!igrab(lower_inode))
		return ERR_PTR(-ESTALE);
	tfs_inode = iget5_locked(tsb, (unsigned long)lower_inode,
			     tierfs_inode_test, tierfs_inode_set,
			     lower_inode);
	if (!tfs_inode) {
		iput(lower_inode);
		return ERR_PTR(-EACCES);
	}
	if (!(tfs_inode->i_state & I_NEW))
		iput(lower_inode);

	TRACE_EXIT();

	return tfs_inode;
}

struct inode *tierfs_get_inode(struct inode *lower_inode,
				 struct super_block *tsb)
{
	struct inode *inode = __tierfs_get_inode(lower_inode, tsb);

	TRACE_ENTRY();
	if (!IS_ERR(inode) && (inode->i_state & I_NEW))
		unlock_new_inode(inode);

	TRACE_EXIT();
	return inode;
}
/**
 * tierfs_interpose
 * @lower_dentry: Existing dentry in the lower filesystem
 * @dentry: tierfs' dentry
 * @sb: tierfs's super_block
 *
 * Interposes upper and lower dentries.
 *
 * Returns zero on success; non-zero otherwise
 */
static int tierfs_interpose(struct dentry *lower_dentry,
			      struct dentry *dentry, struct super_block *sb)
{
	struct inode *inode = tierfs_get_inode(lower_dentry->d_inode, sb);

	TRACE_ENTRY();
	if (IS_ERR(inode))
		return PTR_ERR(inode);
	d_instantiate(dentry, inode);

	TRACE_EXIT();
	return 0;
}

static int tierfs_do_unlink(struct inode *dir, struct dentry *dentry,
			      struct inode *inode)
{
	struct dentry *lower_dentry = tierfs_dentry_to_lower(dentry);
	struct inode *lower_dir_inode = tierfs_inode_to_lower(dir);
	struct dentry *lower_dir_dentry;
	int rc;

	TRACE_ENTRY();
	dget(lower_dentry);
	lower_dir_dentry = lock_parent(lower_dentry);
	rc = vfs_unlink(lower_dir_inode, lower_dentry);
	if (rc) {
		printk(KERN_ERR "Error in vfs_unlink; rc = [%d]\n", rc);
		goto out_unlock;
	}
	fsstack_copy_attr_times(dir, lower_dir_inode);
	set_nlink(inode, tierfs_inode_to_lower(inode)->i_nlink);
	inode->i_ctime = dir->i_ctime;
	d_drop(dentry);
out_unlock:
	unlock_dir(lower_dir_dentry);
	dput(lower_dentry);
	TRACE_EXIT();
	return rc;
}

/**
 * tierfs_do_create
 * @directory_inode: inode of the new file's dentry's parent in tierfs
 * @tierfs_dentry: New file's dentry in tierfs
 * @mode: The mode of the new file
 * @nd: nameidata of tierfs' parent's dentry & vfsmount
 *
 * Creates the underlying file and the eCryptfs inode which will link to
 * it. It will also update the eCryptfs directory inode to mimic the
 * stat of the lower directory inode.
 *
 * Returns the new eCryptfs inode on success; an ERR_PTR on error condition
 */
static struct inode *
tierfs_do_create(struct inode *directory_inode,
		   struct dentry *tierfs_dentry, umode_t mode)
{
	int rc;
	struct dentry *lower_dentry;
	struct dentry *lower_dir_dentry;
	struct inode *inode;

	TRACE_ENTRY();
	lower_dentry = tierfs_dentry_to_lower(tierfs_dentry);
	lower_dir_dentry = lock_parent(lower_dentry);
	if (IS_ERR(lower_dir_dentry)) {
		tierfs_printk(KERN_ERR, "Error locking directory of "
				"dentry\n");
		inode = ERR_CAST(lower_dir_dentry);
		goto out;
	}
	rc = vfs_create(lower_dir_dentry->d_inode, lower_dentry, mode, true);
	if (rc) {
		printk(KERN_ERR "%s: Failure to create dentry in lower fs; "
		       "rc = [%d]\n", __func__, rc);
		inode = ERR_PTR(rc);
		goto out_lock;
	}
	inode = __tierfs_get_inode(lower_dentry->d_inode,
				     directory_inode->i_sb);
	if (IS_ERR(inode)) {
		vfs_unlink(lower_dir_dentry->d_inode, lower_dentry);
		goto out_lock;
	}
	fsstack_copy_attr_times(directory_inode, lower_dir_dentry->d_inode);
	fsstack_copy_inode_size(directory_inode, lower_dir_dentry->d_inode);
out_lock:
	unlock_dir(lower_dir_dentry);
out:
	TRACE_EXIT();
	return inode;
}


/**
 * tierfs_create
 * @dir: The inode of the directory in which to create the file.
 * @dentry: The eCryptfs dentry
 * @mode: The mode of the new file.
 *
 * Creates a new file.
 *
 * Returns zero on success; non-zero on error condition
 */
static int
tierfs_create(struct inode *directory_inode, struct dentry *tierfs_dentry,
		umode_t mode, bool excl)
{
	struct inode *tierfs_inode;
	int rc = 0;

	TRACE_ENTRY();
	tierfs_inode = tierfs_do_create(directory_inode, tierfs_dentry,
					    mode);
	if (unlikely(IS_ERR(tierfs_inode))) {
		tierfs_printk(KERN_WARNING, "Failed to create file in"
				"lower filesystem\n");
		rc = PTR_ERR(tierfs_inode);
		goto out;
	}
	/* At this point, a file exists on "disk"; we need to make sure
	 * that this on disk file is prepared to be an tierfs file */
	unlock_new_inode(tierfs_inode);
	d_instantiate(tierfs_dentry, tierfs_inode);
out:
	TRACE_EXIT();
	return rc;
}
#if 0
static int tierfs_i_size_read(struct dentry *dentry, struct inode *inode)
{
        struct tierfs_crypt_stat *crypt_stat;
        int rc;

        rc = tierfs_get_lower_file(dentry, inode);
        if (rc) {
                printk(KERN_ERR "%s: Error attempting to initialize "
                        "the lower file for the dentry with name "
                        "[%s]; rc = [%d]\n", __func__,
                        dentry->d_name.name, rc);
                return rc;
        }

        crypt_stat = &tierfs_inode_to_private(inode)->crypt_stat;
        /* TODO: lock for crypt_stat comparison */
        if (!(crypt_stat->flags & ECRYPTFS_POLICY_APPLIED))
                tierfs_set_default_sizes(crypt_stat);

        rc = tierfs_read_and_validate_header_region(inode);
        tierfs_put_lower_file(inode);
        if (rc) {
                rc = tierfs_read_and_validate_xattr_region(dentry, inode);
                if (!rc)
                        crypt_stat->flags |= ECRYPTFS_METADATA_IN_XATTR;
        }

        /* Must return 0 to allow non-eCryptfs files to be looked up, too */
        return 0;
}
#endif


/**
 * tierfs_lookup_interpose - Dentry interposition for a lookup
 */
static int tierfs_lookup_interpose(struct dentry *dentry,
				     struct dentry *lower_dentry,
				     struct inode *dir_inode)
{
	struct inode *inode, *lower_inode = lower_dentry->d_inode;
	struct tierfs_dentry_info *dentry_info;
	struct vfsmount *lower_mnt;
	int rc = 0;

	TRACE_ENTRY();
	dentry_info = kmem_cache_alloc(tierfs_dentry_info_cache, GFP_KERNEL);
	if (!dentry_info) {
		printk(KERN_ERR "%s: Out of memory whilst attempting "
		       "to allocate tierfs_dentry_info struct\n",
			__func__);
		dput(lower_dentry);
		return -ENOMEM;
	}

	lower_mnt = mntget(tierfs_dentry_to_lower_mnt(dentry->d_parent));
	fsstack_copy_attr_atime(dir_inode, lower_dentry->d_parent->d_inode);
	BUG_ON(!d_count(lower_dentry));

	tierfs_set_dentry_private(dentry, dentry_info);
	tierfs_set_dentry_lower_dentry(dentry, lower_dentry);
	tierfs_set_dentry_lower_mnt(dentry, lower_mnt);

	if (!lower_dentry->d_inode) {
		/* We want to add because we couldn't find in lower */
		d_add(dentry, NULL);
		return 0;
	}
	inode = __tierfs_get_inode(lower_inode, dir_inode->i_sb);
	if (IS_ERR(inode)) {
		printk(KERN_ERR "%s: Error interposing; rc = [%ld]\n",
		       __func__, PTR_ERR(inode));
		return PTR_ERR(inode);
	}

	if (inode->i_state & I_NEW)
		unlock_new_inode(inode);
	d_add(dentry, inode);

	TRACE_EXIT();
	return rc;
}

/**
 * tierfs_lookup
 * @tierfs_dir_inode: The eCryptfs directory inode
 * @tierfs_dentry: The eCryptfs dentry that we are looking up
 * @tierfs_nd: nameidata; may be NULL
 *
 * Find a file on disk. If the file does not exist, then we'll add it to the
 * dentry cache and continue on to read it from the disk.
 */
static struct dentry *tierfs_lookup(struct inode *tierfs_dir_inode,
				      struct dentry *tierfs_dentry,
				      unsigned int flags)
{
	struct dentry *lower_dir_dentry, *lower_dentry;
	int rc = 0;

	TRACE_ENTRY();
	lower_dir_dentry = tierfs_dentry_to_lower(tierfs_dentry->d_parent);
	mutex_lock(&lower_dir_dentry->d_inode->i_mutex);
	lower_dentry = lookup_one_len(tierfs_dentry->d_name.name,
				      lower_dir_dentry,
				      tierfs_dentry->d_name.len);
	mutex_unlock(&lower_dir_dentry->d_inode->i_mutex);
	if (IS_ERR(lower_dentry)) {
		rc = PTR_ERR(lower_dentry);
		tierfs_printk(KERN_DEBUG, "%s: lookup_one_len() returned "
				"[%d] on lower_dentry = [%s]\n", __func__, rc,
				tierfs_dentry->d_name.name);
		goto out;
	}

	rc = tierfs_lookup_interpose(tierfs_dentry, lower_dentry,
				       tierfs_dir_inode);
out:
	TRACE_EXIT();
	return ERR_PTR(rc);
}

static int tierfs_link(struct dentry *old_dentry, struct inode *dir,
			 struct dentry *new_dentry)
{
	struct dentry *lower_old_dentry;
	struct dentry *lower_new_dentry;
	struct dentry *lower_dir_dentry;
	u64 file_size_save;
	int rc;

	TRACE_ENTRY();
	file_size_save = i_size_read(old_dentry->d_inode);
	lower_old_dentry = tierfs_dentry_to_lower(old_dentry);
	lower_new_dentry = tierfs_dentry_to_lower(new_dentry);
	dget(lower_old_dentry);
	dget(lower_new_dentry);
	lower_dir_dentry = lock_parent(lower_new_dentry);
	rc = vfs_link(lower_old_dentry, lower_dir_dentry->d_inode,
		      lower_new_dentry);
	if (rc || !lower_new_dentry->d_inode)
		goto out_lock;
	rc = tierfs_interpose(lower_new_dentry, new_dentry, dir->i_sb);
	if (rc)
		goto out_lock;
	fsstack_copy_attr_times(dir, lower_dir_dentry->d_inode);
	fsstack_copy_inode_size(dir, lower_dir_dentry->d_inode);
	set_nlink(old_dentry->d_inode,
		  tierfs_inode_to_lower(old_dentry->d_inode)->i_nlink);
	i_size_write(new_dentry->d_inode, file_size_save);
out_lock:
	unlock_dir(lower_dir_dentry);
	dput(lower_new_dentry);
	dput(lower_old_dentry);
	TRACE_EXIT();
	return rc;
}

static int tierfs_unlink(struct inode *dir, struct dentry *dentry)
{
	TRACE_ENTRY();
	TRACE_EXIT();
	return tierfs_do_unlink(dir, dentry, dentry->d_inode);
}

static int tierfs_symlink(struct inode *dir, struct dentry *dentry,
			    const char *symname)
{
	int rc;
	struct dentry *lower_dentry;
	struct dentry *lower_dir_dentry;

	TRACE_ENTRY();
	lower_dentry = tierfs_dentry_to_lower(dentry);
	dget(lower_dentry);
	lower_dir_dentry = lock_parent(lower_dentry);
	rc = vfs_symlink(lower_dir_dentry->d_inode, lower_dentry,
			 symname);
	if (rc || !lower_dentry->d_inode)
		goto out_lock;
	rc = tierfs_interpose(lower_dentry, dentry, dir->i_sb);
	if (rc)
		goto out_lock;
	fsstack_copy_attr_times(dir, lower_dir_dentry->d_inode);
	fsstack_copy_inode_size(dir, lower_dir_dentry->d_inode);
out_lock:
	unlock_dir(lower_dir_dentry);
	dput(lower_dentry);
	if (!dentry->d_inode)
		d_drop(dentry);
	TRACE_EXIT();
	return rc;
}


static int tierfs_mkdir(struct inode *dir, struct dentry *dentry, umode_t mode)
{
	int rc;
	struct dentry *lower_dentry;
	struct dentry *lower_dir_dentry;

	TRACE_ENTRY();
	lower_dentry = tierfs_dentry_to_lower(dentry);
	lower_dir_dentry = lock_parent(lower_dentry);
	rc = vfs_mkdir(lower_dir_dentry->d_inode, lower_dentry, mode);
	if (rc || !lower_dentry->d_inode)
		goto out;
	rc = tierfs_interpose(lower_dentry, dentry, dir->i_sb);
	if (rc)
		goto out;
	fsstack_copy_attr_times(dir, lower_dir_dentry->d_inode);
	fsstack_copy_inode_size(dir, lower_dir_dentry->d_inode);
	set_nlink(dir, lower_dir_dentry->d_inode->i_nlink);
out:
	unlock_dir(lower_dir_dentry);
	if (!dentry->d_inode)
		d_drop(dentry);
	TRACE_EXIT();
	return rc;
}

static int tierfs_rmdir(struct inode *dir, struct dentry *dentry)
{
	struct dentry *lower_dentry;
	struct dentry *lower_dir_dentry;
	int rc;

	TRACE_ENTRY();
	lower_dentry = tierfs_dentry_to_lower(dentry);
	dget(dentry);
	lower_dir_dentry = lock_parent(lower_dentry);
	dget(lower_dentry);
	rc = vfs_rmdir(lower_dir_dentry->d_inode, lower_dentry);
	dput(lower_dentry);
	if (!rc && dentry->d_inode)
		clear_nlink(dentry->d_inode);
	fsstack_copy_attr_times(dir, lower_dir_dentry->d_inode);
	set_nlink(dir, lower_dir_dentry->d_inode->i_nlink);
	unlock_dir(lower_dir_dentry);
	if (!rc)
		d_drop(dentry);
	dput(dentry);
	TRACE_EXIT();
	return rc;
}

static int
tierfs_mknod(struct inode *dir, struct dentry *dentry, umode_t mode, dev_t dev)
{
	int rc;
	struct dentry *lower_dentry;
	struct dentry *lower_dir_dentry;

	TRACE_ENTRY();
	lower_dentry = tierfs_dentry_to_lower(dentry);
	lower_dir_dentry = lock_parent(lower_dentry);
	rc = vfs_mknod(lower_dir_dentry->d_inode, lower_dentry, mode, dev);
	if (rc || !lower_dentry->d_inode)
		goto out;
	rc = tierfs_interpose(lower_dentry, dentry, dir->i_sb);
	if (rc)
		goto out;
	fsstack_copy_attr_times(dir, lower_dir_dentry->d_inode);
	fsstack_copy_inode_size(dir, lower_dir_dentry->d_inode);
out:
	unlock_dir(lower_dir_dentry);
	if (!dentry->d_inode)
		d_drop(dentry);
	TRACE_EXIT();
	return rc;
}

static int
tierfs_rename(struct inode *old_dir, struct dentry *old_dentry,
		struct inode *new_dir, struct dentry *new_dentry)
{
	int rc;
	struct dentry *lower_old_dentry;
	struct dentry *lower_new_dentry;
	struct dentry *lower_old_dir_dentry;
	struct dentry *lower_new_dir_dentry;
	struct dentry *trap = NULL;
	struct inode *target_inode;

	TRACE_ENTRY();
	lower_old_dentry = tierfs_dentry_to_lower(old_dentry);
	lower_new_dentry = tierfs_dentry_to_lower(new_dentry);
	dget(lower_old_dentry);
	dget(lower_new_dentry);
	lower_old_dir_dentry = dget_parent(lower_old_dentry);
	lower_new_dir_dentry = dget_parent(lower_new_dentry);
	target_inode = new_dentry->d_inode;
	trap = lock_rename(lower_old_dir_dentry, lower_new_dir_dentry);
	/* source should not be ancestor of target */
	if (trap == lower_old_dentry) {
		rc = -EINVAL;
		goto out_lock;
	}
	/* target should not be ancestor of source */
	if (trap == lower_new_dentry) {
		rc = -ENOTEMPTY;
		goto out_lock;
	}
	rc = vfs_rename(lower_old_dir_dentry->d_inode, lower_old_dentry,
			lower_new_dir_dentry->d_inode, lower_new_dentry);
	if (rc)
		goto out_lock;
	if (target_inode)
		fsstack_copy_attr_all(target_inode,
				      tierfs_inode_to_lower(target_inode));
	fsstack_copy_attr_all(new_dir, lower_new_dir_dentry->d_inode);
	if (new_dir != old_dir)
		fsstack_copy_attr_all(old_dir, lower_old_dir_dentry->d_inode);
out_lock:
	unlock_rename(lower_old_dir_dentry, lower_new_dir_dentry);
	dput(lower_new_dir_dentry);
	dput(lower_old_dir_dentry);
	dput(lower_new_dentry);
	dput(lower_old_dentry);
	TRACE_EXIT();
	return rc;
}

static int tierfs_readlink_lower(struct dentry *dentry, char **buf,
				   size_t *bufsiz)
{
	struct dentry *lower_dentry = tierfs_dentry_to_lower(dentry);
	char *lower_buf;
	mm_segment_t old_fs;
	int rc;

	TRACE_ENTRY();
	lower_buf = kmalloc(PATH_MAX, GFP_KERNEL);
	if (!lower_buf) {
		rc = -ENOMEM;
		goto out;
	}
	old_fs = get_fs();
	set_fs(get_ds());
	rc = lower_dentry->d_inode->i_op->readlink(lower_dentry,
						   (char __user *)lower_buf,
						   PATH_MAX);
	set_fs(old_fs);
	kfree(lower_buf);
out:
	TRACE_EXIT();
	return rc;
}


static void *tierfs_follow_link(struct dentry *dentry, struct nameidata *nd)
{
	char *buf= NULL;
	size_t len = PATH_MAX;
	int rc;

	TRACE_ENTRY();
	rc = tierfs_readlink_lower(dentry, &buf, &len);
	if (rc)
		goto out;
	fsstack_copy_attr_atime(dentry->d_inode,
				tierfs_dentry_to_lower(dentry)->d_inode);
	buf[len] = '\0';
out:
	nd_set_link(nd, buf);
	TRACE_EXIT();
	return NULL;
}

static void
tierfs_put_link(struct dentry *dentry, struct nameidata *nd, void *ptr)
{
	char *buf = nd_get_link(nd);
	TRACE_ENTRY();
	if (!IS_ERR(buf)) {
		/* Free the char* */
		kfree(buf);
	}
	TRACE_EXIT();
}


/**
 * truncate_upper
 * @dentry: The tierfs layer dentry
 * @ia: Address of the tierfs inode's attributes
 * @lower_ia: Address of the lower inode's attributes
 *
 * Function to handle truncations modifying the size of the file. Note
 * that the file sizes are interpolated. When expanding, we are simply
 * writing strings of 0's out. When truncating, we truncate the upper
 * inode and update the lower_ia according to the page index
 * interpolations. If ATTR_SIZE is set in lower_ia->ia_valid upon return,
 * the caller must use lower_ia in a call to notify_change() to perform
 * the truncation of the lower inode.
 *
 * Returns zero on success; non-zero otherwise
 */
static int truncate_upper(struct dentry *dentry, struct iattr *ia,
			  struct iattr *lower_ia)
{
	int rc = 0;
	struct inode *inode = dentry->d_inode;
	loff_t i_size = i_size_read(inode);

	TRACE_ENTRY();
	if (unlikely((ia->ia_size == i_size))) {
		lower_ia->ia_valid &= ~ATTR_SIZE;
		return 0;
	}
	rc = tierfs_get_lower_file(dentry, inode);
	if (rc)
		return rc;
	/* Switch on growing or shrinking file */
	if (ia->ia_size > i_size) {
		char zero[] = { 0x00 };

		lower_ia->ia_valid &= ~ATTR_SIZE;
		/* Write a single 0 at the last position of the file;
		 * this triggers code that will fill in 0's throughout
		 * the intermediate portion of the previous end of the
		 * file and the new and of the file */
		rc = tierfs_write(inode, zero,
				    (ia->ia_size - 1), 1);
	} else { /* ia->ia_size < i_size_read(inode) */
		/* We're chopping off all the pages down to the page
		 * in which ia->ia_size is located. Fill in the end of
		 * that page from (ia->ia_size & ~PAGE_CACHE_MASK) to
		 * PAGE_CACHE_SIZE with zeros. */

		truncate_setsize(inode, ia->ia_size);
		lower_ia->ia_size = ia->ia_size;
		lower_ia->ia_valid |= ATTR_SIZE;
	}
	tierfs_put_lower_file(inode);
	TRACE_EXIT();
	return rc;
}
static int tierfs_inode_newsize_ok(struct inode *inode, loff_t offset)
{
	TRACE_ENTRY();
	TRACE_EXIT();
	return inode_newsize_ok(inode, offset);
}

/**
 * tierfs_truncate
 * @dentry: The tierfs layer dentry
 * @new_length: The length to expand the file to
 *
 * Simple function that handles the truncation of an eCryptfs inode and
 * its corresponding lower inode.
 *
 * Returns zero on success; non-zero otherwise
 */
int tierfs_truncate(struct dentry *dentry, loff_t new_length)
{
	struct iattr ia = { .ia_valid = ATTR_SIZE, .ia_size = new_length };
	struct iattr lower_ia = { .ia_valid = 0 };
	int rc;

	TRACE_ENTRY();
	rc = tierfs_inode_newsize_ok(dentry->d_inode, new_length);
	if (rc)
		return rc;

	rc = truncate_upper(dentry, &ia, &lower_ia);
	if (!rc && lower_ia.ia_valid & ATTR_SIZE) {
		struct dentry *lower_dentry = tierfs_dentry_to_lower(dentry);

		mutex_lock(&lower_dentry->d_inode->i_mutex);
		rc = notify_change(lower_dentry, &lower_ia);
		mutex_unlock(&lower_dentry->d_inode->i_mutex);
	}
	TRACE_EXIT();
	return rc;
}

static int
tierfs_permission(struct inode *inode, int mask)
{
	TRACE_ENTRY();
	TRACE_EXIT();
	return inode_permission(tierfs_inode_to_lower(inode), mask);
}
/**
 * tierfs_setattr
 * @dentry: dentry handle to the inode to modify
 * @ia: Structure with flags of what to change and values
 *
 * Updates the metadata of an inode. If the update is to the size
 * i.e. truncation, then tierfs_truncate will handle the size modification
 * of both the tierfs inode and the lower inode.
 *
 * All other metadata changes will be passed right to the lower filesystem,
 * and we will just update our inode to look like the lower.
 */
static int tierfs_setattr(struct dentry *dentry, struct iattr *ia)
{
	int rc = 0;
	struct dentry *lower_dentry;
	struct iattr lower_ia;
	struct inode *inode;
	struct inode *lower_inode;

	TRACE_ENTRY();
	inode = dentry->d_inode;
	lower_inode = tierfs_inode_to_lower(inode);
	lower_dentry = tierfs_dentry_to_lower(dentry);

	rc = inode_change_ok(inode, ia);
	if (rc)
		goto out;
	if (ia->ia_valid & ATTR_SIZE) {
		rc = tierfs_inode_newsize_ok(inode, ia->ia_size);
		if (rc)
			goto out;
	}

	memcpy(&lower_ia, ia, sizeof(lower_ia));
	if (ia->ia_valid & ATTR_FILE)
		lower_ia.ia_file = tierfs_file_to_lower(ia->ia_file);
	if (ia->ia_valid & ATTR_SIZE) {
		rc = truncate_upper(dentry, ia, &lower_ia);
		if (rc < 0)
			goto out;
	}

	/*
	 * mode change is for clearing setuid/setgid bits. Allow lower fs
	 * to interpret this in its own way.
	 */
	if (lower_ia.ia_valid & (ATTR_KILL_SUID | ATTR_KILL_SGID))
		lower_ia.ia_valid &= ~ATTR_MODE;

	mutex_lock(&lower_dentry->d_inode->i_mutex);
	rc = notify_change(lower_dentry, &lower_ia);
	mutex_unlock(&lower_dentry->d_inode->i_mutex);
out:
	fsstack_copy_attr_all(inode, lower_inode);
	TRACE_EXIT();
	return rc;
}

static int tierfs_getattr_link(struct vfsmount *mnt, struct dentry *dentry,
				 struct kstat *stat)
{
	int rc = 0;

	TRACE_ENTRY();
	generic_fillattr(dentry->d_inode, stat);
	TRACE_EXIT();
	return rc;
}

static int tierfs_getattr(struct vfsmount *mnt, struct dentry *dentry,
			    struct kstat *stat)
{
	struct kstat lower_stat;
	int rc;

	TRACE_ENTRY();
	rc = vfs_getattr(tierfs_dentry_to_lower_path(dentry), &lower_stat);
	if (!rc) {
		fsstack_copy_attr_all(dentry->d_inode,
				      tierfs_inode_to_lower(dentry->d_inode));
		generic_fillattr(dentry->d_inode, stat);
		stat->blocks = lower_stat.blocks;
	}
	TRACE_EXIT();
	return rc;
}

int
tierfs_setxattr(struct dentry *dentry, const char *name, const void *value,
		  size_t size, int flags)
{
	int rc = 0;
	struct dentry *lower_dentry;

	TRACE_ENTRY();
	lower_dentry = tierfs_dentry_to_lower(dentry);
	if (!lower_dentry->d_inode->i_op->setxattr) {
		rc = -EOPNOTSUPP;
		goto out;
	}

	rc = vfs_setxattr(lower_dentry, name, value, size, flags);
	if (!rc)
		fsstack_copy_attr_all(dentry->d_inode, lower_dentry->d_inode);
out:
	TRACE_EXIT();
	return rc;
}

ssize_t
tierfs_getxattr_lower(struct dentry *lower_dentry, const char *name,
			void *value, size_t size)
{
	int rc = 0;

	TRACE_ENTRY();
	if (!lower_dentry->d_inode->i_op->getxattr) {
		rc = -EOPNOTSUPP;
		goto out;
	}
	mutex_lock(&lower_dentry->d_inode->i_mutex);
	rc = lower_dentry->d_inode->i_op->getxattr(lower_dentry, name, value,
						   size);
	mutex_unlock(&lower_dentry->d_inode->i_mutex);
out:
	TRACE_EXIT();
	return rc;
}

static ssize_t
tierfs_getxattr(struct dentry *dentry, const char *name, void *value,
		  size_t size)
{
	TRACE_ENTRY();
	TRACE_EXIT();
	return tierfs_getxattr_lower(tierfs_dentry_to_lower(dentry), name,
				       value, size);
}

static ssize_t
tierfs_listxattr(struct dentry *dentry, char *list, size_t size)
{
	int rc = 0;
	struct dentry *lower_dentry;

	TRACE_ENTRY();
	lower_dentry = tierfs_dentry_to_lower(dentry);
	if (!lower_dentry->d_inode->i_op->listxattr) {
		rc = -EOPNOTSUPP;
		goto out;
	}
	mutex_lock(&lower_dentry->d_inode->i_mutex);
	rc = lower_dentry->d_inode->i_op->listxattr(lower_dentry, list, size);
	mutex_unlock(&lower_dentry->d_inode->i_mutex);
out:
	TRACE_EXIT();
	return rc;
}

static int tierfs_removexattr(struct dentry *dentry, const char *name)
{
	int rc = 0;
	struct dentry *lower_dentry;

	TRACE_ENTRY();
	lower_dentry = tierfs_dentry_to_lower(dentry);
	if (!lower_dentry->d_inode->i_op->removexattr) {
		rc = -EOPNOTSUPP;
		goto out;
	}
	mutex_lock(&lower_dentry->d_inode->i_mutex);
	rc = lower_dentry->d_inode->i_op->removexattr(lower_dentry, name);
	mutex_unlock(&lower_dentry->d_inode->i_mutex);
out:
	TRACE_EXIT();
	return rc;
}

const struct inode_operations tierfs_symlink_iops = {
	.readlink = generic_readlink,
	.follow_link = tierfs_follow_link,
	.put_link = tierfs_put_link,
	.permission = tierfs_permission,
	.setattr = tierfs_setattr,
	.getattr = tierfs_getattr_link,
	.setxattr = tierfs_setxattr,
	.getxattr = tierfs_getxattr,
	.listxattr = tierfs_listxattr,
	.removexattr = tierfs_removexattr
};

const struct inode_operations tierfs_dir_iops = {
	.create = tierfs_create,
	.lookup = tierfs_lookup,
	.link = tierfs_link,
	.unlink = tierfs_unlink,
	.symlink = tierfs_symlink,
	.mkdir = tierfs_mkdir,
	.rmdir = tierfs_rmdir,
	.mknod = tierfs_mknod,
	.rename = tierfs_rename,
	.permission = tierfs_permission,
	.setattr = tierfs_setattr,
	.setxattr = tierfs_setxattr,
	.getxattr = tierfs_getxattr,
	.listxattr = tierfs_listxattr,
	.removexattr = tierfs_removexattr
};

const struct inode_operations tierfs_main_iops = {
	.permission = tierfs_permission,
	.setattr = tierfs_setattr,
	.getattr = tierfs_getattr,
	.setxattr = tierfs_setxattr,
	.getxattr = tierfs_getxattr,
	.listxattr = tierfs_listxattr,
	.removexattr = tierfs_removexattr
};
