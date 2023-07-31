/**
 * @file aesdchar.c
 * @brief Functions and data related to the AESD char driver implementation
 *
 * Based on the implementation of the "scull" device driver, found in
 * Linux Device Drivers example code.
 *
 * @author Dan Walkes
 * @date 2019-10-22
 * @copyright Copyright (c) 2019
 *
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/printk.h>
#include <linux/types.h>
#include <linux/cdev.h>
#include <linux/fs.h> // file_operations
#include <linux/slab.h> // kmalloc/free
#include "aesdchar.h"
int aesd_major =   0; // use dynamic major
int aesd_minor =   0;

MODULE_AUTHOR("Thomas Ames"); /** TODO: fill in your name **/
MODULE_LICENSE("Dual BSD/GPL");

struct aesd_dev aesd_device;

int aesd_open(struct inode *inode, struct file *filp)
{
    PDEBUG("open");
    /**
     * TODO: handle open
     */

    // Use container_of macro to get pointer to the cdev.  First field
    // of aesd_dev is cdev, so p_cdev == p_aesd_dev
    filp->private_data = container_of(inode->i_cdev, struct aesd_dev, cdev);
    PDEBUG("filp->private_data = %p, inode->i_cdev = %p, &aesd_device = %p",
	   filp->private_data, inode->i_cdev, &aesd_device);
    return 0;
}

int aesd_release(struct inode *inode, struct file *filp)
{
    PDEBUG("release");
    /**
     * TODO: handle release
     */
    // Nothing to do - release is the opposite of open, but we don't
    // do anything in open that needs to be undone.
    return 0;
}

ssize_t aesd_read(struct file *filp, char __user *buf, size_t count,
                loff_t *f_pos)
{
    // Probably safe to assume the kernel doesn't pass a null filp
    struct aesd_dev *p_aesd_dev = (struct aesd_dev *) filp->private_data;
    struct aesd_buffer_entry *p_cir_buf_entry;
    size_t cir_buf_entry_offset;
    const void * from_buf;
    size_t avail_bytes_in_buf, bytes_to_copy;
    ssize_t retval = 0;

    PDEBUG("read %zu bytes with offset %lld",count,*f_pos);
    /**
     * TODO: handle read
     */
    // Returns 0 if lock aquired, -EINTR if interrupted. Probably don't
    // need to lock the ENTIRE function body.
    if (mutex_lock_interruptible(&aesd_device.lock)) {
	return -EINTR;
    }

    p_cir_buf_entry =
	aesd_circular_buffer_find_entry_offset_for_fpos(&(p_aesd_dev->circ_buf),
							*f_pos,
							&cir_buf_entry_offset);
    // If NULL, gone past end of circular buffer, no more data to read.
    // Return 0 to indicate EOF.  Also reset f_pos to 0 so next reads
    // starts at begining of the circular buffer; ie, each new read should
    // return to start of buffer - implicit seek(fd,0,seek_set)
    // May need to revisit this when we add seek support in assignment 9?
    if (!p_cir_buf_entry) {
	*f_pos = 0;
	mutex_unlock(&aesd_device.lock);
	return 0;
    }

    // p_cir_buf_entry holds a valid pointer to a buffer entry.
    // p_cir_buf_entry->buffptr is the START of the buffer.  Desired
    // data starts at p_cir_buf_entry->buffptr + cir_buf_entry_offset,
    // available bytes is p_cir_buf_entry->size - cir_buf_entry_offset.
    from_buf = p_cir_buf_entry->buffptr + cir_buf_entry_offset;
    avail_bytes_in_buf = p_cir_buf_entry->size - cir_buf_entry_offset;
    bytes_to_copy = min(avail_bytes_in_buf, count);

    // bytes_to_copy returns number NOT copied, 0 on success.
    // subtract # failed from number desired to get # copied
    retval = bytes_to_copy - copy_to_user(buf, from_buf, bytes_to_copy);
    *f_pos += retval;
    PDEBUG("read update offset to %lld, retval %ld",*f_pos,retval);
    mutex_unlock(&aesd_device.lock);
    return retval;
}

ssize_t aesd_write(struct file *filp, const char __user *buf, size_t count,
                loff_t *f_pos)
{
    // Probably safe to assume the kernel doesn't pass a null filp
    struct aesd_dev *p_aesd_dev = (struct aesd_dev *) filp->private_data;
    void * kmem_buf;
    const char * add_entry_retval;
    ssize_t retval = -ENOMEM;

    PDEBUG("write %zu bytes with offset %lld",count,*f_pos);
    /**
     * TODO: handle write
     */

    // Returns 0 if lock aquired, -EINTR if interrupted. Probably don't
    // need to lock the ENTIRE function body.
    if (mutex_lock_interruptible(&aesd_device.lock)) {
	return -EINTR;
    }

    // Partial write in progress?
    if (p_aesd_dev->partial_write.buffptr) {
	// Partial write in progress, expand current buffer by count
	if (!(kmem_buf = kmalloc(count + p_aesd_dev->partial_write.size,
				 GFP_KERNEL))) {
	    printk("write: kmalloc(%ld, GFP_KERNEL) returned NULL", count);
	    mutex_unlock(&aesd_device.lock);
	    return -ENOMEM;
	} else {
	    // Copy the previous partial write to the new, larger buffer
	    memcpy(kmem_buf, p_aesd_dev->partial_write.buffptr,
		   p_aesd_dev->partial_write.size);
	    // Deallocate old buffer, save new.
	    kfree(p_aesd_dev->partial_write.buffptr);
	    p_aesd_dev->partial_write.buffptr = kmem_buf;
	    // Increment kmem_buf to first byte after previous write
	    kmem_buf += p_aesd_dev->partial_write.size;
	}
    } else {
	// No previous partial write, but this one may be partial...
	if (!(kmem_buf = kmalloc(count, GFP_KERNEL))) {
	    printk("write: kmalloc(%ld, GFP_KERNEL) returned NULL", count);
	    mutex_unlock(&aesd_device.lock);
	    return -ENOMEM;
	} else {
	    p_aesd_dev->partial_write.buffptr = kmem_buf;
	    p_aesd_dev->partial_write.size = 0;
	}
    }

    // At this point, we are either in the middle of a partial write, or
    // assuming we are starting a new partial write.  The following are
    // valid:
    //   kmem_buf is the pointer in the new buffer to write the data
    //   p_aesd_dev->partial_write.buffptr holds the address of the first
    //     partial write (possibly this one)
    //   p_aesd_dev->partial_write.size is the size of the prior partial
    //     write(s). 0 if this is a new one
    // Remaining steps:
    //  - Copy from user buf to kmem_buf
    //  - Add count to p_aesd_dev->partial_write.size
    //  - If newline terminated, add to circular buffer and NULL out
    //    partial_write

    // copy_from_user returns number of bytes NOT copied, 0 on success
    retval = count - copy_from_user(kmem_buf, buf, count);
    p_aesd_dev->partial_write.size += retval;

    if ('\n' == *((char *)kmem_buf+(retval-1))) {
	if ((add_entry_retval =
	     aesd_circular_buffer_add_entry(&(p_aesd_dev->circ_buf),
					    &(p_aesd_dev->partial_write)))) {
	    kfree(add_entry_retval);
	}
	p_aesd_dev->partial_write.buffptr = NULL;
	p_aesd_dev->partial_write.size = 0;
	PDEBUG("write: add_entry_retval = %p", add_entry_retval);
    }
    PDEBUG("write: user buf = %p, kmem_buf = %p, retval = %ld", buf,
	   kmem_buf, retval);

    mutex_unlock(&aesd_device.lock);
    return retval;
}
struct file_operations aesd_fops = {
    .owner =    THIS_MODULE,
    .read =     aesd_read,
    .write =    aesd_write,
    .open =     aesd_open,
    .release =  aesd_release,
};

static int aesd_setup_cdev(struct aesd_dev *dev)
{
    int err, devno = MKDEV(aesd_major, aesd_minor);

    cdev_init(&dev->cdev, &aesd_fops);
    dev->cdev.owner = THIS_MODULE;
    dev->cdev.ops = &aesd_fops;
    err = cdev_add (&dev->cdev, devno, 1);
    if (err) {
        printk(KERN_ERR "Error %d adding aesd cdev", err);
    }
    return err;
}



int aesd_init_module(void)
{
    dev_t dev = 0;
    int result;
    result = alloc_chrdev_region(&dev, aesd_minor, 1,
            "aesdchar");
    aesd_major = MAJOR(dev);
    if (result < 0) {
        printk(KERN_WARNING "Can't get major %d\n", aesd_major);
        return result;
    }
    memset(&aesd_device,0,sizeof(struct aesd_dev));

    /**
     * TODO: initialize the AESD specific portion of the device
     */
    // memset above 0'ed out aesd_device.circ_buf, so call to
    // aesd_circular_buffer_init and setting partial_write isn't really
    // necessary, but here for completeness.
    aesd_circular_buffer_init(&aesd_device.circ_buf);
    aesd_device.partial_write.buffptr = NULL;
    aesd_device.partial_write.size = 0;
    mutex_init(&aesd_device.lock);

    result = aesd_setup_cdev(&aesd_device);

    if( result ) {
        unregister_chrdev_region(dev, 1);
    }
    PDEBUG("aesd_init_module(), result=%d, &aesd_device = %p", result,
	   &aesd_device);

    return result;

}

void aesd_cleanup_module(void)
{
    dev_t devno = MKDEV(aesd_major, aesd_minor);
    uint8_t index;
    struct aesd_buffer_entry *entry;

    cdev_del(&aesd_device.cdev);

    /**
     * TODO: cleanup AESD specific poritions here as necessary
     */
    // Returns 0 if lock aquired, -EINTR if interrupted. Maybe
    // we should continue anyway?
    if (mutex_lock_interruptible(&aesd_device.lock)) {
	return;
    }

    AESD_CIRCULAR_BUFFER_FOREACH(entry,&aesd_device.circ_buf,index) {
	PDEBUG("aesd_cleanup_module, index = %d, entry->buffptr = %p",
	       index, entry->buffptr);
	if (entry->buffptr) {
	    kfree(entry->buffptr);
	}
    }

    // Last PDEBUG seems to get lost, so use a dummy one here...
    PDEBUG("");

    // Don't need to check for bufptr == NULL, kfree(NULL) is nop
    if (aesd_device.partial_write.buffptr) {
	kfree(aesd_device.partial_write.buffptr);
    }

    mutex_unlock(&aesd_device.lock);

    unregister_chrdev_region(devno, 1);
}



module_init(aesd_init_module);
module_exit(aesd_cleanup_module);
