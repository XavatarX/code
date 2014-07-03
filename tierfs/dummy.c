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
int tierfs_verbosity = 1;
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

enum { tierfs_stier_path, tierfs_ptier_path };
static const match_table_t tokens = {
	{tierfs_stier_path, "stier=%s"},
	{tierfs_ptier_path, "ptier=%s"}
};

tfs_tier_list_t tfs_tier_list = {0};

int tierfs_add_tier_path(const char *tier_path, int tier_type)
{
	int rc = 0;

	if (!tier_path) {
		printk(KERN_ERR"ERROR: NULL pointer passed!\n");
		rc = -EINVAL;
		goto out;
	}

	if (tfs_tier_list.ntiers < MAX_SUPPORTED_TIER) {
		tfs_tier_list.tiers[tfs_tier_list.ntiers].tier_type = tier_type;
		memcpy(&tfs_tier_list.tiers[tfs_tier_list.ntiers++].tier_path,
			tier_path, TFS_MAX_PATH_LEN);
		printk(KERN_ERR"Adding hdd path: %s\n", tier_path);
	} else {
		printk(KERN_ERR"ERROR: Only %d no of tier paths are supported.\n", MAX_SUPPORTED_TIER);
		rc = -EINVAL;
	}

out:
	return rc;
}

struct kmem_cache *tierfs_sb_info_cache;
static int tierfs_parse_options(struct tierfs_sb_info *sbi, char *options)
{
	int token;
	char *p;
	substring_t args[MAX_OPT_ARGS];
	printk(KERN_ERR"Options: %s\n", options);
	while ((p = strsep(&options, ",")) != NULL) {
		token = match_token(p, tokens, args);
		switch (token) {
			case tierfs_stier_path:
				*(args[0].to + 1) = '\0';
				tierfs_add_tier_path(args[0].from, TFS_TIER_TYPE_SECONDARY);
			break;
			default:
				printk(KERN_ERR"Invalid options\n");
				return -1;
		}
	}

	return 0;
}

static void tierfs_put_kpath_all(void)
{
	int i;

	for (i = 0; i < tfs_tier_list.nkpaths; i++) {
		struct path *kpath = &tfs_tier_list.tiers[i].tier_kpath;
		path_put(kpath);
	}
}

static struct path * tierfs_get_ptier_kpath(void)
{
	int i;
	struct path *kpath = NULL;

	for (i = 0; i < tfs_tier_list.nkpaths; i++) {
		if (tfs_tier_list.tiers[i].tier_type == TFS_TIER_TYPE_PRIMARY) {
			kpath = &tfs_tier_list.tiers[i].tier_kpath;
			break;
		}
	}

	return kpath;
}

static int tierfs_set_superblock_lower_all(struct super_block *tsb)
{
	int rc, i;
	struct path *kpath;

	for (i = 0; i < tfs_tier_list.ntiers; i++) {
		char *tier_path = tfs_tier_list.tiers[i].tier_path;
		kpath = &tfs_tier_list.tiers[i].tier_kpath;

		rc = kern_path(tier_path, LOOKUP_FOLLOW | LOOKUP_DIRECTORY, kpath);
		if (rc) {
			tierfs_printk(KERN_WARNING,
				"kern_path() failed for path %s\n", tier_path);
			goto out;
		}

		tfs_tier_list.nkpaths++;
		if (kpath->dentry->d_sb->s_type == &tierfs_fs_type) {
			rc = -EINVAL;
			printk(KERN_ERR "Mount on filesystem of type "
				"tierfs explicitly disallowed due to "
				"known incompatibilities\n");
			goto out;
		}

		tierfs_set_superblock_lower(tsb, kpath->dentry->d_sb);
	}

	return 0;
out:
	tierfs_put_kpath_all();
	return rc;
}

static struct dentry *tierfs_mount(struct file_system_type *fs_type, int flags,
			const char *dev_name, void *raw_data)
{
	struct super_block *tfs_sb;
	struct tierfs_sb_info *tfs_sbi;
	struct tierfs_dentry_info *tfs_root_info;
	const char *err = "Getting sb failed";
	struct path *kpath;
	struct inode *tfs_inode;
	int rc;

	TRACE_ENTRY();

	tfs_sbi = kmem_cache_zalloc(tierfs_sb_info_cache, GFP_KERNEL);
	if (!tfs_sbi) {
		rc = -ENOMEM;
		goto out;
	}

	rc = tierfs_parse_options(tfs_sbi, raw_data);
	if (rc) {
		err = "Error parsing options";
		goto out;
	}

	rc = tierfs_add_tier_path(dev_name, TFS_TIER_TYPE_PRIMARY);
	if (rc) {
		err = "Error durring adding path";
		goto out;
	}

	tfs_sb = sget(fs_type, NULL, set_anon_super, flags, NULL);
	if (IS_ERR(tfs_sb)) {
		rc = PTR_ERR(tfs_sb);
		goto out;
	}

	rc = bdi_setup_and_register(&tfs_sbi->bdi, "tierfs", BDI_CAP_MAP_COPY);
	if (rc)
		goto out1;

	tierfs_set_superblock_private(tfs_sb, tfs_sbi);
	tfs_sb->s_bdi = &tfs_sbi->bdi;

	/* ->kill_sb() will take care of sbi after that point */
	tfs_sbi = NULL;
	tfs_sb->s_op = &tierfs_sops;
	tfs_sb->s_d_op = &tierfs_dops;

	rc = tierfs_set_superblock_lower_all(tfs_sb);
	if (rc) {
		goto out;
	}

	kpath = tierfs_get_ptier_kpath();
	if (!kpath) {
		goto out_free;
	}

	/**
	 * Set the POSIX ACL flag based on whether they're enabled in the lower
	 * mount. Force a read-only tierfs mount if the lower mount is ro.
	 * Allow a ro tierfs mount even when the lower mount is rw.
	 */
	tfs_sb->s_flags = flags & ~MS_POSIXACL;
	tfs_sb->s_flags |= kpath->dentry->d_sb->s_flags & (MS_RDONLY | MS_POSIXACL);

	tfs_sb->s_maxbytes = kpath->dentry->d_sb->s_maxbytes;
	tfs_sb->s_blocksize = kpath->dentry->d_sb->s_blocksize;
	tfs_sb->s_magic = TIERFS_SUPER_MAGIC;

	tfs_inode = tierfs_get_inode(kpath->dentry->d_inode, tfs_sb);
	rc = PTR_ERR(tfs_inode);
	if (IS_ERR(tfs_inode))
		goto out_free;

	tfs_sb->s_root = d_make_root(tfs_inode);
	if (!tfs_sb->s_root) {
		rc = -ENOMEM;
		goto out_free;
	}

	rc = -ENOMEM;
	tfs_root_info = kmem_cache_zalloc(tierfs_dentry_info_cache, GFP_KERNEL);
	if (!tfs_root_info)
		goto out_free;

	/* ->kill_sb() will take care of root_info */
	tierfs_set_dentry_private(tfs_sb->s_root, tfs_root_info);
	tierfs_set_dentry_lower_path(tfs_sb->s_root, kpath);

	tfs_sb->s_flags |= MS_ACTIVE;
	TRACE_EXIT();
	return dget(tfs_sb->s_root);

out_free:
	tierfs_put_kpath_all();
	tfs_tier_list.ntiers = 0;

out1:
	deactivate_locked_super(tfs_sb);
out:
	if (tfs_sbi) {
		kmem_cache_free(tierfs_sb_info_cache, tfs_sbi);
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

