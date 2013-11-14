#include <linux/dcache.h>
#include <linux/file.h>
#include <linux/module.h>
#include <linux/namei.h>
#include <linux/skbuff.h>
#include <linux/crypto.h>
#include <linux/mount.h>
#include <linux/pagemap.h>
#include <linux/key.h>
#include <linux/parser.h>
#include <linux/fs_stack.h>
#include <linux/slab.h>
#include <linux/magic.h>
#include "tierfs_kernel.h"


static struct file_system_type tierfs_fs_type;
static void tierfs_free_kmem_caches(void);
int tierfs_verbosity = 0;
void __tierfs_printk(const char *fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	if (fmt[1] == '7') { /* KERN_DEBUG */
		if (tierfs_verbosity >= 1)
			vprintk(fmt, args);
	} else
		vprintk(fmt, args);
	va_end(args);
}
/**
 * tierfs_privileged_open
 * @lower_file: Result of dentry_open by root on lower dentry
 * @lower_dentry: Lower dentry for file to open
 * @lower_mnt: Lower vfsmount for file to open
 *
 * This function gets a r/w file opened againt the lower dentry.
 *
 * Returns zero on success; non-zero otherwise
 */
int tierfs_privileged_open(struct file **lower_file,
                             struct dentry *lower_dentry,
                             struct vfsmount *lower_mnt)
{
        struct path path;
        int flags = O_LARGEFILE;
        int rc = 0;
	TRACE_ENTRY();
        path.dentry = lower_dentry;
        path.mnt = lower_mnt;

        /* Corresponding dput() and mntput() are done when the
         * lower file is fput() when all tierfs files for the inode are
         * released. */
        flags |= IS_RDONLY(lower_dentry->d_inode) ? O_RDONLY : O_RDWR;
        (*lower_file) = dentry_open(&path, flags, current_cred());
        if (!IS_ERR(*lower_file)) {
                goto out;
	}
	tierfs_printk(KERN_ERR, "Error in opening file %ld %p\n", PTR_ERR((*lower_file)), *lower_file);
        if ((flags & O_ACCMODE) == O_RDONLY) {
                rc = PTR_ERR((*lower_file));
        }
out:
	TRACE_EXIT();
	return rc;
}

/**
 * tierfs_init_lower_file
 * @tierfs_dentry: Fully initialized tierfs dentry object, with
 *                   the lower dentry and the lower mount set
 *
 * tierfs only ever keeps a single open file for every lower
 * inode. All I/O operations to the lower inode occur through that
 * file. When the first tierfs dentry that interposes with the first
 * lower dentry for that inode is created, this function creates the
 * lower file struct and associates it with the tierfs
 * inode. When all tierfs files associated with the inode are released, the
 * file is closed.
 *
 * The lower file will be opened with read/write permissions, if
 * possible. Otherwise, it is opened read-only.
 *
 * This function does nothing if a lower file is already
 * associated with the tierfs inode.
 *
 * Returns zero on success; non-zero otherwise
 */
static int tierfs_init_lower_file(struct dentry *dentry,
				    struct file **lower_file)
{
	struct path *path = tierfs_dentry_to_lower_path(dentry);
	int rc;

	TRACE_ENTRY();
	rc = tierfs_privileged_open(lower_file, path->dentry, path->mnt);
	if (rc) {
		printk(KERN_ERR "Error opening lower file "
		       "for lower_dentry [0x%p] and lower_mnt [0x%p]; "
		       "rc = [%d]\n", path->dentry, path->mnt, rc);
		(*lower_file) = NULL;
	}
	TRACE_EXIT();
	return rc;
}

int tierfs_get_lower_file(struct dentry *dentry, struct inode *inode)
{
	struct tierfs_inode_info *inode_info;
	int count, rc = 0;

	TRACE_ENTRY();
	inode_info = tierfs_inode_to_private(inode);
	mutex_lock(&inode_info->lower_file_mutex);
	count = atomic_inc_return(&inode_info->lower_file_count);
	if (WARN_ON_ONCE(count < 1))
		rc = -EINVAL;
	else if (count == 1) {
		rc = tierfs_init_lower_file(dentry,
					      &inode_info->lower_file);
		if (rc)
			atomic_set(&inode_info->lower_file_count, 0);
	}
	mutex_unlock(&inode_info->lower_file_mutex);
	TRACE_EXIT();
	return rc;
}

void tierfs_put_lower_file(struct inode *inode)
{
	struct tierfs_inode_info *inode_info;

	TRACE_ENTRY();
	inode_info = tierfs_inode_to_private(inode);
	if (atomic_dec_and_mutex_lock(&inode_info->lower_file_count,
				      &inode_info->lower_file_mutex)) {
		filemap_write_and_wait(inode->i_mapping);
		fput(inode_info->lower_file);
		inode_info->lower_file = NULL;
		mutex_unlock(&inode_info->lower_file_mutex);
	}
	TRACE_EXIT();
}


struct kmem_cache *tierfs_sb_info_cache;
static int tierfs_parse_options(struct tierfs_sb_info *sbi, char *options)
{
	return 0;
}

static struct dentry *tierfs_mount(struct file_system_type *fs_type, int flags,
			const char *dev_name, void *raw_data)
{
	struct super_block *s;
	struct tierfs_sb_info *sbi;
	struct tierfs_dentry_info *root_info;
	const char *err = "Getting sb failed";
	struct inode *inode;
	struct path path;
	int rc;

	TRACE_ENTRY();
	sbi = kmem_cache_zalloc(tierfs_sb_info_cache, GFP_KERNEL);
	if (!sbi) {
		rc = -ENOMEM;
		goto out;
	}
	rc = tierfs_parse_options(sbi, raw_data);
	if (rc) {
		err = "Error parsing options";
		goto out;
	}

	s = sget(fs_type, NULL, set_anon_super, flags, NULL);
	if (IS_ERR(s)) {
		rc = PTR_ERR(s);
		goto out;
	}

	rc = bdi_setup_and_register(&sbi->bdi, "tierfs", BDI_CAP_MAP_COPY);
	if (rc)
		goto out1;

	tierfs_set_superblock_private(s, sbi);
	s->s_bdi = &sbi->bdi;

	/* ->kill_sb() will take care of sbi after that point */
	sbi = NULL;
	s->s_op = &tierfs_sops;
	s->s_d_op = &tierfs_dops;

	err = "Reading sb failed";
	rc = kern_path(dev_name, LOOKUP_FOLLOW | LOOKUP_DIRECTORY, &path);
	if (rc) {
		tierfs_printk(KERN_WARNING, "kern_path() failed\n");
		goto out1;
	}
	if (path.dentry->d_sb->s_type == &tierfs_fs_type) {
		rc = -EINVAL;
		printk(KERN_ERR "Mount on filesystem of type "
			"tierfs explicitly disallowed due to "
			"known incompatibilities\n");
		goto out_free;
	}
#if 0
	if (check_ruid && !uid_eq(path.dentry->d_inode->i_uid, current_uid())) {
		rc = -EPERM;
		printk(KERN_ERR "Mount of device (uid: %d) not owned by "
		       "requested user (uid: %d)\n",
			i_uid_read(path.dentry->d_inode),
			from_kuid(&init_user_ns, current_uid()));
		goto out_free;
	}
#endif
	tierfs_set_superblock_lower(s, path.dentry->d_sb);

	/**
	 * Set the POSIX ACL flag based on whether they're enabled in the lower
	 * mount. Force a read-only tierfs mount if the lower mount is ro.
	 * Allow a ro tierfs mount even when the lower mount is rw.
	 */
	s->s_flags = flags & ~MS_POSIXACL;
	s->s_flags |= path.dentry->d_sb->s_flags & (MS_RDONLY | MS_POSIXACL);

	s->s_maxbytes = path.dentry->d_sb->s_maxbytes;
	s->s_blocksize = path.dentry->d_sb->s_blocksize;
	s->s_magic = TIERFS_SUPER_MAGIC;

	inode = tierfs_get_inode(path.dentry->d_inode, s);
	rc = PTR_ERR(inode);
	if (IS_ERR(inode))
		goto out_free;

	s->s_root = d_make_root(inode);
	if (!s->s_root) {
		rc = -ENOMEM;
		goto out_free;
	}

	rc = -ENOMEM;
	root_info = kmem_cache_zalloc(tierfs_dentry_info_cache, GFP_KERNEL);
	if (!root_info)
		goto out_free;

	/* ->kill_sb() will take care of root_info */
	tierfs_set_dentry_private(s->s_root, root_info);
	tierfs_set_dentry_lower(s->s_root, path.dentry);
	tierfs_set_dentry_lower_mnt(s->s_root, path.mnt);

	s->s_flags |= MS_ACTIVE;
	TRACE_EXIT();
	return dget(s->s_root);

out_free:
	path_put(&path);
out1:
	deactivate_locked_super(s);
out:
	if (sbi) {
		kmem_cache_free(tierfs_sb_info_cache, sbi);
	}
	printk(KERN_ERR "%s; rc = [%d]\n", err, rc);
	TRACE_EXIT();
	return ERR_PTR(rc);
}


static void tierfs_kill_block_super(struct super_block *sb)
{
	struct tierfs_sb_info *sb_info = tierfs_superblock_to_private(sb);
	
	TRACE_ENTRY();
	kill_anon_super(sb);
	if (!sb_info)
		return;
	bdi_destroy(&sb_info->bdi);
	kmem_cache_free(tierfs_sb_info_cache, sb_info);
	TRACE_EXIT();
}

static struct file_system_type tierfs_fs_type = {
	.owner = THIS_MODULE,
	.name = "tierfs",
	.mount = tierfs_mount,
	.kill_sb = tierfs_kill_block_super,
	.fs_flags = 0
};

static void
inode_info_init_once(void *vptr)
{
	struct tierfs_inode_info *ei = (struct tierfs_inode_info *)vptr;

	TRACE_ENTRY();
	inode_init_once(&ei->vfs_inode);
	TRACE_EXIT();
}

static struct tierfs_cache_info {
	struct kmem_cache **cache;
	const char *name;
	size_t size;
	void (*ctor)(void *obj);
} tierfs_cache_infos[] = {
	{
		.cache = &tierfs_file_info_cache,
		.name = "tierfs_file_cache",
		.size = sizeof(struct tierfs_file_info),
	},
	{
		.cache = &tierfs_dentry_info_cache,
		.name = "tierfs_dentry_info_cache",
		.size = sizeof(struct tierfs_dentry_info),
	},
	{
		.cache = &tierfs_inode_info_cache,
		.name = "tierfs_inode_cache",
		.size = sizeof(struct tierfs_inode_info),
		.ctor = inode_info_init_once,
	},
	{
		.cache = &tierfs_sb_info_cache,
		.name = "tierfs_sb_cache",
		.size = sizeof(struct tierfs_sb_info),
	},
};

static int tierfs_init_kmem_caches(void)
{
	int i;

	TRACE_ENTRY();
	for (i = 0; i < ARRAY_SIZE(tierfs_cache_infos); i++) {
		struct tierfs_cache_info *info;

		info = &tierfs_cache_infos[i];
		*(info->cache) = kmem_cache_create(info->name, info->size,
				0, SLAB_HWCACHE_ALIGN, info->ctor);
		if (!*(info->cache)) {
			tierfs_free_kmem_caches();
			tierfs_printk(KERN_WARNING, "%s: "
					"kmem_cache_create failed\n",
					info->name);
			return -ENOMEM;
		}
	}
	TRACE_EXIT();
	return 0;
}
static void tierfs_free_kmem_caches(void)
{
	int i;

	TRACE_ENTRY();
	/*
	 * Make sure all delayed rcu free inodes are flushed before we
	 * destroy cache.
	 */
	rcu_barrier();

	for (i = 0; i < ARRAY_SIZE(tierfs_cache_infos); i++) {
		struct tierfs_cache_info *info;

		info = &tierfs_cache_infos[i];
		if (*(info->cache))
			kmem_cache_destroy(*(info->cache));
	}
	TRACE_EXIT();
}

static int __init tierfs_init(void)
{
	int rc;

	TRACE_ENTRY();
	rc = tierfs_init_kmem_caches();
	if (rc) {
		printk(KERN_ERR
		       "Failed to allocate one or more kmem_cache objects\n");
		goto out;
	}

	rc = register_filesystem(&tierfs_fs_type);
	if (rc) {
		printk(KERN_ERR "Failed to register filesystem\n");
		goto out_destroy_kmem_cache;
	}

	goto out;

out_destroy_kmem_cache:
	tierfs_free_kmem_caches();
out:
	TRACE_EXIT();
	return rc;
}
static void __exit tierfs_exit(void)
{
	TRACE_ENTRY();
	unregister_filesystem(&tierfs_fs_type);
	tierfs_free_kmem_caches();
	TRACE_EXIT();
}

	



module_init(tierfs_init)
module_exit(tierfs_exit)
MODULE_LICENSE("GPL");
