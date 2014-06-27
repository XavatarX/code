#include <linux/file.h>
#include <linux/poll.h>
#include <linux/slab.h>
#include <linux/mount.h>
#include <linux/pagemap.h>
#include <linux/security.h>
#include <linux/compat.h>
#include <linux/fs_stack.h>
#include <linux/aio.h>
#include <linux/fs.h>
#include "tierfs_kernel.h"

/**
 * tierfs_read_update_atime
 *
 * generic_file_read updates the atime of upper layer inode.  But, it
 * doesn't give us a chance to update the atime of the lower layer
 * inode.  This function is a wrapper to generic_file_read.  It
 * updates the atime of the lower level inode if generic_file_read
 * returns without any errors. This is to be used only for file reads.
 * The function to be used for directory reads is tierfs_read.
 */
static ssize_t tierfs_read_update_atime(struct kiocb *iocb,
				const struct iovec *iov,
				unsigned long nr_segs, loff_t pos)
{
	ssize_t rc;
	struct path *path;
	struct file *file = iocb->ki_filp;

	TRACE_ENTRY();
	rc = generic_file_aio_read(iocb, iov, nr_segs, pos);
	/*
	 * Even though this is a async interface, we need to wait
	 * for IO to finish to update atime
	 */
	if (-EIOCBQUEUED == rc)
		rc = wait_on_sync_kiocb(iocb);
	if (rc >= 0) {
		path = tierfs_dentry_to_lower_path(file->f_path.dentry);
		touch_atime(path);
	}
	TRACE_EXIT();
	return rc;
}

struct tierfs_getdents_callback {
	struct dir_context ctx;
	struct dir_context *caller;
	struct super_block *sb;
	int filldir_called;
	int entries_written;
};

/* Inspired by generic filldir in fs/readdir.c */
static int
tierfs_filldir(void *dirent, const char *lower_name, int lower_namelen,
		 loff_t offset, u64 ino, unsigned int d_type)
{
	struct tierfs_getdents_callback *buf =
	    (struct tierfs_getdents_callback *)dirent;
	int rc;

	TRACE_ENTRY();
	buf->filldir_called++;
	buf->caller->pos = buf->ctx.pos;
	rc = !dir_emit(buf->caller, lower_name, lower_namelen, ino, d_type);
	if (!rc)
		buf->entries_written++;
	TRACE_EXIT();
	return rc;
}


/**
 * tierfs_readdir
 * @file: The eCryptfs directory file
 * @ctx: The actor to feed the entries to
 */
static int tierfs_readdir(struct file *file, struct dir_context *ctx)
{
	int rc;
	struct file *lower_file;
	struct inode *inode = file_inode(file);
	struct tierfs_getdents_callback buf = {
		.ctx.actor = tierfs_filldir,
		.caller = ctx,
		.sb = inode->i_sb,
	};
	TRACE_ENTRY();
	lower_file = tierfs_file_to_lower(file);
	lower_file->f_pos = ctx->pos;
	rc = iterate_dir(lower_file, &buf.ctx);
	ctx->pos = buf.ctx.pos;
	if (rc < 0)
		goto out;
	if (buf.filldir_called && !buf.entries_written)
		goto out;
	if (rc >= 0)
		fsstack_copy_attr_atime(inode,
					file_inode(lower_file));
out:
	TRACE_EXIT();
	return rc;
}

struct kmem_cache *tierfs_file_info_cache;

/**
 * tierfs_open
 * @inode: inode speciying file to open
 * @file: Structure to return filled in
 *
 * Opens the file specified by inode.
 *
 * Returns zero on success; non-zero otherwise
 */
static int tierfs_open(struct inode *inode, struct file *file)
{
	int rc = 0;
	struct dentry *tierfs_dentry = file->f_path.dentry;
	/* Private value of tierfs_dentry allocated in
	 * tierfs_lookup() */
	struct tierfs_file_info *file_info;

	TRACE_ENTRY();
	/* Released in tierfs_release or end of function if failure */
	file_info = kmem_cache_zalloc(tierfs_file_info_cache, GFP_KERNEL);
	tierfs_set_file_private(file, file_info);
	if (!file_info) {
		tierfs_printk(KERN_ERR,
				"Error attempting to allocate memory\n");
		rc = -ENOMEM;
		goto out;
	}
	rc = tierfs_get_lower_file(tierfs_dentry, inode);
	if (!(tierfs_inode_to_private(inode)->lower_file)) {
		printk(KERN_ERR "%s: Error attempting to initialize "
			"the lower file for the dentry with name "
			"[%s]; rc = [%d]\n", __func__,
			tierfs_dentry->d_name.name, rc);
		goto out_free;
	}
	if ((tierfs_inode_to_private(inode)->lower_file->f_flags & O_ACCMODE)
	    == O_RDONLY && (file->f_flags & O_ACCMODE) != O_RDONLY) {
		rc = -EPERM;
		printk(KERN_ERR "%s: Lower file is RO; eCryptfs "
		       "file must hence be opened RO\n", __func__);
		goto out_put;
	}
	tierfs_set_file_lower(
		file, tierfs_inode_to_private(inode)->lower_file);
	if (S_ISDIR(tierfs_dentry->d_inode->i_mode)) {
		rc = 0;
		goto out;
	}
	goto out;
out_put:
	tierfs_put_lower_file(inode);
out_free:
	kmem_cache_free(tierfs_file_info_cache,
			tierfs_file_to_private(file));
out:
	TRACE_EXIT();
	return rc;
}

static int tierfs_flush(struct file *file, fl_owner_t td)
{
	struct file *lower_file = tierfs_file_to_lower(file);

	TRACE_ENTRY();
	if (lower_file->f_op) {
		if (lower_file->f_op->flush ) {
			filemap_write_and_wait(file->f_mapping);
			return lower_file->f_op->flush(lower_file, td);
		}
	}

	TRACE_EXIT();
	return 0;
}

static int tierfs_release(struct inode *inode, struct file *file)
{
	TRACE_ENTRY();
	tierfs_put_lower_file(inode);
	kmem_cache_free(tierfs_file_info_cache,
			tierfs_file_to_private(file));
	TRACE_EXIT();
	return 0;
}

static int
tierfs_fsync(struct file *file, loff_t start, loff_t end, int datasync)
{
	int rc;

	TRACE_ENTRY();
	rc = filemap_write_and_wait(file->f_mapping);
	if (rc)
		return rc;

	TRACE_EXIT();
	return vfs_fsync(tierfs_file_to_lower(file), datasync);
}

static int tierfs_fasync(int fd, struct file *file, int flag)
{
	int rc = 0;
	struct file *lower_file = NULL;

	TRACE_ENTRY();
	lower_file = tierfs_file_to_lower(file);
	if (lower_file->f_op && lower_file->f_op->fasync)
		rc = lower_file->f_op->fasync(fd, lower_file, flag);
	TRACE_EXIT();
	return rc;
}

static long
tierfs_unlocked_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct file *lower_file = NULL;
	long rc = -ENOTTY;

	TRACE_ENTRY();
	if (tierfs_file_to_private(file))
		lower_file = tierfs_file_to_lower(file);
	if (lower_file && lower_file->f_op && lower_file->f_op->unlocked_ioctl)
		rc = lower_file->f_op->unlocked_ioctl(lower_file, cmd, arg);
	TRACE_EXIT();
	return rc;
}
ssize_t tierfs_writefile (struct file *file, const char __user *data, size_t size, loff_t *off) 
{
	struct file * lower_file = tierfs_file_to_lower(file);
	return lower_file->f_op->write(file,data, size, off);
}
ssize_t tierfs_readfile (struct file *file, char __user *data, size_t size, loff_t *off) 
{
	struct file * lower_file = tierfs_file_to_lower(file);
	return lower_file->f_op->read(file,data, size, off);
}

#ifdef CONFIG_COMPAT
static long
tierfs_compat_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct file *lower_file = NULL;
	long rc = -ENOIOCTLCMD;

	TRACE_ENTRY();
	if (tierfs_file_to_private(file))
		lower_file = tierfs_file_to_lower(file);
	if (lower_file && lower_file->f_op && lower_file->f_op->compat_ioctl)
		rc = lower_file->f_op->compat_ioctl(lower_file, cmd, arg);
	TRACE_EXIT();
	return rc;
}
#endif

const struct file_operations tierfs_dir_fops = {
	.iterate = tierfs_readdir,
	.read = generic_read_dir,
	.unlocked_ioctl = tierfs_unlocked_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl = tierfs_compat_ioctl,
#endif
	.open = tierfs_open,
	.flush = tierfs_flush,
	.release = tierfs_release,
	.fsync = tierfs_fsync,
	.fasync = tierfs_fasync,
	.splice_read = generic_file_splice_read,
	.llseek = default_llseek,
};

const struct file_operations tierfs_main_fops = {
	.llseek = generic_file_llseek,
//	.read = tierfs_readfile,
	.read = do_sync_read,
	.aio_read = tierfs_read_update_atime,
//	.write = tierfs_writefile,
	.write = do_sync_write,
	.aio_write = generic_file_aio_write,
	.iterate = tierfs_readdir,
	.unlocked_ioctl = tierfs_unlocked_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl = tierfs_compat_ioctl,
#endif
	.mmap = generic_file_mmap,
	.open = tierfs_open,
	.flush = tierfs_flush,
	.release = tierfs_release,
	.fsync = tierfs_fsync,
	.fasync = tierfs_fasync,
	.splice_read = generic_file_splice_read,
};
