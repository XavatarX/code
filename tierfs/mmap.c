#include <linux/pagemap.h>
#include <linux/writeback.h>
#include <linux/page-flags.h>
#include <linux/mount.h>
#include <linux/file.h>
#include <linux/crypto.h>
#include <linux/scatterlist.h>
#include <linux/slab.h>
#include <asm/unaligned.h>
#include "tierfs_kernel.h"

/**
 * tierfs_get_locked_page
 *
 * Get one page from cache or lower f/s, return error otherwise.
 *
 * Returns locked and up-to-date page (if ok), with increased
 * refcnt.
 */
struct page *tierfs_get_locked_page(struct inode *inode, loff_t index)
{
	struct page *page = read_mapping_page(inode->i_mapping, index, NULL);
	if (!IS_ERR(page))
		lock_page(page);
	return page;
}

/**
 * tierfs_writepage
 * @page: Page that is locked before this call is made
 *
 * Returns zero on success; non-zero otherwise
 *
 * This is where we encrypt the data and pass the encrypted data to
 * the lower filesystem.  In OpenPGP-compatible mode, we operate on
 * entire underlying packets.
 */
static int tierfs_writepage(struct page *page, struct writeback_control *wbc)
{
	int rc;

	TRACE_ENTRY();
	rc = tierfs_write_lower(page->mapping->host, kmap(page),
			 page->index << PAGE_CACHE_SHIFT, PAGE_CACHE_SIZE);
	kunmap(page);

	if (rc) {
		tierfs_printk(KERN_WARNING, "Error encrypting "
				"page (upper index [0x%.16lx])\n", page->index);
		ClearPageUptodate(page);
		goto out;
	}
	SetPageUptodate(page);
out:
	unlock_page(page);
	TRACE_EXIT();
	return rc;
}


/**
 * tierfs_readpage
 * @file: An eCryptfs file
 * @page: Page from eCryptfs inode mapping into which to stick the read data
 *
 * Read in a page, dtiering if necessary.
 *
 * Returns zero on success; non-zero on error.
 */
static int tierfs_readpage(struct file *file, struct page *page)
{
	int rc = 0;

	TRACE_ENTRY();
	rc = tierfs_read_lower_page_segment(page, page->index, 0,
						      PAGE_CACHE_SIZE,
						      page->mapping->host);
	if (rc)
		ClearPageUptodate(page);
	else
		SetPageUptodate(page);
	tierfs_printk(KERN_DEBUG, "Unlocking page with index = [0x%.16lx]\n",
			page->index);
	unlock_page(page);
	TRACE_EXIT();
	return rc;
}
#if 0
/**
 * Called with lower inode mutex held.
 */
static int fill_zeros_to_end_of_page(struct page *page, unsigned int to)
{
	struct inode *inode = page->mapping->host;
	int end_byte_in_page;

	if ((i_size_read(inode) / PAGE_CACHE_SIZE) != page->index)
		goto out;
	end_byte_in_page = i_size_read(inode) % PAGE_CACHE_SIZE;
	if (to > end_byte_in_page)
		end_byte_in_page = to;
	zero_user_segment(page, end_byte_in_page, PAGE_CACHE_SIZE);
out:
	return 0;
}
#endif
/**
 * tierfs_write_begin
 * @file: The eCryptfs file
 * @mapping: The eCryptfs object
 * @pos: The file offset at which to start writing
 * @len: Length of the write
 * @flags: Various flags
 * @pagep: Pointer to return the page
 * @fsdata: Pointer to return fs data (unused)
 *
 * This function must zero any hole we create
 *
 * Returns zero on success; non-zero otherwise
 */
static int tierfs_write_begin(struct file *file,
			struct address_space *mapping,
			loff_t pos, unsigned len, unsigned flags,
			struct page **pagep, void **fsdata)
{
	pgoff_t index = pos >> PAGE_CACHE_SHIFT;
	struct page *page;
	loff_t prev_page_end_size;
	int rc = 0;

	TRACE_ENTRY();
	page = grab_cache_page_write_begin(mapping, index, flags);
	if (!page)
		return -ENOMEM;
	*pagep = page;

	prev_page_end_size = ((loff_t)index << PAGE_CACHE_SHIFT);
	if (!PageUptodate(page)) {
		rc = tierfs_read_lower_page_segment(
			page, index, 0, PAGE_CACHE_SIZE, mapping->host);
		if (rc) {
			printk(KERN_ERR "%s: Error attemping to read "
			       "lower page segment; rc = [%d]\n",
			       __func__, rc);
			ClearPageUptodate(page);
			goto out;
		} else
			SetPageUptodate(page);
	}
	/* If creating a page or more of holes, zero them out via truncate.
	 * Note, this will increase i_size. */
	if (index != 0) {
		if (prev_page_end_size > i_size_read(page->mapping->host)) {
			rc = tierfs_truncate(file->f_path.dentry,
					       prev_page_end_size);
			if (rc) {
				printk(KERN_ERR "%s: Error on attempt to "
				       "truncate to (higher) offset [%lld];"
				       " rc = [%d]\n", __func__,
				       prev_page_end_size, rc);
				goto out;
			}
		}
	}
	/* Writing to a new page, and creating a small hole from start
	 * of page?  Zero it out. */
	if ((i_size_read(mapping->host) == prev_page_end_size)
	    && (pos != 0))
		zero_user(page, 0, PAGE_CACHE_SIZE);
out:
	if (unlikely(rc)) {
		unlock_page(page);
		page_cache_release(page);
		*pagep = NULL;
	}
	TRACE_EXIT();
	return rc;
}

struct kmem_cache *tierfs_xattr_cache;
/**
 * tierfs_write_end
 * @file: The eCryptfs file object
 * @mapping: The eCryptfs object
 * @pos: The file position
 * @len: The length of the data (unused)
 * @copied: The amount of data copied
 * @page: The eCryptfs page
 * @fsdata: The fsdata (unused)
 */
static int tierfs_write_end(struct file *file,
			struct address_space *mapping,
			loff_t pos, unsigned len, unsigned copied,
			struct page *page, void *fsdata)
{
	pgoff_t index = pos >> PAGE_CACHE_SHIFT;
	unsigned from = pos & (PAGE_CACHE_SIZE - 1);
	unsigned to = from + copied;
	struct inode *tierfs_inode = mapping->host;
	int rc;

	TRACE_ENTRY();
	tierfs_printk(KERN_DEBUG, "Calling fill_zeros_to_end_of_page"
			"(page w/ index = [0x%.16lx], to = [%d])\n", index, to);
	rc = tierfs_write_lower_page_segment(tierfs_inode, page, 0,
					       to);
	if (!rc) {
		rc = copied;
		fsstack_copy_inode_size(tierfs_inode,
			tierfs_inode_to_lower(tierfs_inode));
	}
	unlock_page(page);
	page_cache_release(page);
	TRACE_EXIT();
	return rc;
}

static sector_t tierfs_bmap(struct address_space *mapping, sector_t block)
{
	int rc = 0;
	struct inode *inode;
	struct inode *lower_inode;

	TRACE_ENTRY();
	inode = (struct inode *)mapping->host;
	lower_inode = tierfs_inode_to_lower(inode);
	if (lower_inode->i_mapping->a_ops->bmap)
		rc = lower_inode->i_mapping->a_ops->bmap(lower_inode->i_mapping,
							 block);
	TRACE_EXIT();
	return rc;
}

const struct address_space_operations tierfs_aops = {
	.writepage = tierfs_writepage,
	.readpage = tierfs_readpage,
	.write_begin = tierfs_write_begin,
	.write_end = tierfs_write_end,
	.bmap = tierfs_bmap,
};
