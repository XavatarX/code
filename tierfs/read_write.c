#include <linux/fs.h>
#include <linux/pagemap.h>
#include "tierfs_kernel.h"

/**
 * tierfs_write_lower
 * @tierfs_inode: The eCryptfs inode
 * @data: Data to write
 * @offset: Byte offset in the lower file to which to write the data
 * @size: Number of bytes from @data to write at @offset in the lower
 *        file
 *
 * Write data to the lower file.
 *
 * Returns bytes written on success; less than zero on error
 */
int tierfs_write_lower(struct inode *tierfs_inode, char *data,
			 loff_t offset, size_t size)
{
	struct file *lower_file;
	ssize_t rc;

	TRACE_ENTRY();
	lower_file = tierfs_inode_to_private(tierfs_inode)->lower_file;
	if (!lower_file)
		return -EIO;
	rc = kernel_write(lower_file, data, size, offset);
	mark_inode_dirty_sync(tierfs_inode);
	TRACE_EXIT();
	return rc;
}

/**
 * tierfs_write_lower_page_segment
 * @tierfs_inode: The eCryptfs inode
 * @page_for_lower: The page containing the data to be written to the
 *                  lower file
 * @offset_in_page: The offset in the @page_for_lower from which to
 *                  start writing the data
 * @size: The amount of data from @page_for_lower to write to the
 *        lower file
 *
 * Determines the byte offset in the file for the given page and
 * offset within the page, maps the page, and makes the call to write
 * the contents of @page_for_lower to the lower inode.
 *
 * Returns zero on success; non-zero otherwise
 */
int tierfs_write_lower_page_segment(struct inode *tierfs_inode,
				      struct page *page_for_lower,
				      size_t offset_in_page, size_t size)
{
	char *virt;
	loff_t offset;
	int rc;

	TRACE_ENTRY();
	offset = ((((loff_t)page_for_lower->index) << PAGE_CACHE_SHIFT)
		  + offset_in_page);
	virt = kmap(page_for_lower);
	rc = tierfs_write_lower(tierfs_inode, virt, offset, size);
	if (rc > 0)
		rc = 0;
	kunmap(page_for_lower);
	TRACE_EXIT();
	return rc;
}

/**
 * tierfs_write
 * @tierfs_inode: The eCryptfs file into which to write
 * @data: Virtual address where data to write is located
 * @offset: Offset in the eCryptfs file at which to begin writing the
 *          data from @data
 * @size: The number of bytes to write from @data
 *
 * Write an arbitrary amount of data to an arbitrary location in the
 * eCryptfs inode page cache. This is done on a page-by-page, and then
 * by an extent-by-extent, basis; individual extents are encrypted and
 * written to the lower page cache (via VFS writes). This function
 * takes care of all the address translation to locations in the lower
 * filesystem; it also handles truncate events, writing out zeros
 * where necessary.
 *
 * Returns zero on success; non-zero otherwise
 */
int tierfs_write(struct inode *tierfs_inode, char *data, loff_t offset,
		   size_t size)
{
	struct page *tierfs_page;
	char *tierfs_page_virt;
	loff_t tierfs_file_size = i_size_read(tierfs_inode);
	loff_t data_offset = 0;
	loff_t pos;
	int rc = 0;

	TRACE_ENTRY();
	/*
	 * if we are writing beyond current size, then start pos
	 * at the current size - we'll fill in zeros from there.
	 */
	if (offset > tierfs_file_size)
		pos = tierfs_file_size;
	else
		pos = offset;
	while (pos < (offset + size)) {
		pgoff_t tierfs_page_idx = (pos >> PAGE_CACHE_SHIFT);
		size_t start_offset_in_page = (pos & ~PAGE_CACHE_MASK);
		size_t num_bytes = (PAGE_CACHE_SIZE - start_offset_in_page);
		loff_t total_remaining_bytes = ((offset + size) - pos);

		if (fatal_signal_pending(current)) {
			rc = -EINTR;
			break;
		}

		if (num_bytes > total_remaining_bytes)
			num_bytes = total_remaining_bytes;
		if (pos < offset) {
			/* remaining zeros to write, up to destination offset */
			loff_t total_remaining_zeros = (offset - pos);

			if (num_bytes > total_remaining_zeros)
				num_bytes = total_remaining_zeros;
		}
		tierfs_page = tierfs_get_locked_page(tierfs_inode,
							 tierfs_page_idx);
		if (IS_ERR(tierfs_page)) {
			rc = PTR_ERR(tierfs_page);
			printk(KERN_ERR "%s: Error getting page at "
			       "index [%ld] from eCryptfs inode "
			       "mapping; rc = [%d]\n", __func__,
			       tierfs_page_idx, rc);
			goto out;
		}
		tierfs_page_virt = kmap_atomic(tierfs_page);

		/*
		 * pos: where we're now writing, offset: where the request was
		 * If current pos is before request, we are filling zeros
		 * If we are at or beyond request, we are writing the *data*
		 * If we're in a fresh page beyond eof, zero it in either case
		 */
		if (pos < offset || !start_offset_in_page) {
			/* We are extending past the previous end of the file.
			 * Fill in zero values to the end of the page */
			memset(((char *)tierfs_page_virt
				+ start_offset_in_page), 0,
				PAGE_CACHE_SIZE - start_offset_in_page);
		}

		/* pos >= offset, we are now writing the data request */
		if (pos >= offset) {
			memcpy(((char *)tierfs_page_virt
				+ start_offset_in_page),
			       (data + data_offset), num_bytes);
			data_offset += num_bytes;
		}
		kunmap_atomic(tierfs_page_virt);
		flush_dcache_page(tierfs_page);
		SetPageUptodate(tierfs_page);
		unlock_page(tierfs_page);
		rc = tierfs_write_lower_page_segment(tierfs_inode,
						tierfs_page,
						start_offset_in_page,
						data_offset);
		page_cache_release(tierfs_page);
		if (rc) {
			printk(KERN_ERR "%s: Error encrypting "
			       "page; rc = [%d]\n", __func__, rc);
			goto out;
		}
		pos += num_bytes;
	}
	if (pos > tierfs_file_size) {
		i_size_write(tierfs_inode, pos);
	}
out:
	TRACE_EXIT();
	return rc;
}

/**
 * tierfs_read_lower
 * @data: The read data is stored here by this function
 * @offset: Byte offset in the lower file from which to read the data
 * @size: Number of bytes to read from @offset of the lower file and
 *        store into @data
 * @tierfs_inode: The eCryptfs inode
 *
 * Read @size bytes of data at byte offset @offset from the lower
 * inode into memory location @data.
 *
 * Returns bytes read on success; 0 on EOF; less than zero on error
 */
int tierfs_read_lower(char *data, loff_t offset, size_t size,
			struct inode *tierfs_inode)
{
	struct file *lower_file;
	TRACE_ENTRY();
	lower_file = tierfs_inode_to_private(tierfs_inode)->lower_file;
	if (!lower_file) { 
		return -EIO;
	}
	TRACE_EXIT();
	return kernel_read(lower_file, offset, data, size);
}

/**
 * tierfs_read_lower_page_segment
 * @page_for_tierfs: The page into which data for eCryptfs will be
 *                     written
 * @offset_in_page: Offset in @page_for_tierfs from which to start
 *                  writing
 * @size: The number of bytes to write into @page_for_tierfs
 * @tierfs_inode: The eCryptfs inode
 *
 * Determines the byte offset in the file for the given page and
 * offset within the page, maps the page, and makes the call to read
 * the contents of @page_for_tierfs from the lower inode.
 *
 * Returns zero on success; non-zero otherwise
 */
int tierfs_read_lower_page_segment(struct page *page_for_tierfs,
				     pgoff_t page_index,
				     size_t offset_in_page, size_t size,
				     struct inode *tierfs_inode)
{
	char *virt;
	loff_t offset;
	int rc;

	TRACE_ENTRY();
	offset = ((((loff_t)page_index) << PAGE_CACHE_SHIFT) + offset_in_page);
	virt = kmap(page_for_tierfs);
	rc = tierfs_read_lower(virt, offset, size, tierfs_inode);
	if (rc > 0)
		rc = 0;
	kunmap(page_for_tierfs);
	flush_dcache_page(page_for_tierfs);
	TRACE_EXIT();
	return rc;
}
