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
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/device/class.h>
#include <linux/mutex.h>
#include <linux/minmax.h>
#include "aesd-circular-buffer.h"
#include "aesdchar.h"

#define DRV_NAME "aesdchar"

static int aesd_major = 0; // use dynamic major
static int aesd_minor = 0;
static struct class *aesd_class;
static struct device *aesd_device;
static struct aesd_dev aesd_dev;
static struct aesd_wrt_buf wrtbuf;

MODULE_AUTHOR("Yousef Abbas");
MODULE_LICENSE("Dual BSD/GPL");

static int aesd_open(struct inode *inode, struct file *filp)
{
	PDEBUG("open");
	return 0;
}

static int aesd_release(struct inode *inode, struct file *filp)
{
	PDEBUG("release");
	return 0;
}

static ssize_t aesd_read(struct file *filp, char __user *buf, size_t count, loff_t *f_pos)
{
	ssize_t retval = 0;
	int ret;
	loff_t pos = *f_pos;
	size_t pos_in_entry;
	struct aesd_buffer_entry *entry;

	PDEBUG("read %zu bytes with offset %lld", count, *f_pos);

	ret = mutex_lock_interruptible(&aesd_dev.mutex);
	if (ret != 0) {
		return ret;
	}

	entry = aesd_circular_buffer_find_entry_offset_for_fpos(&aesd_dev.circ_buf, pos, &pos_in_entry);
	if (entry != NULL) {
		size_t bytes_left_in_entry = entry->size - pos_in_entry;
		ssize_t not_copied;
		size_t bytes_to_read;

		bytes_to_read = min(bytes_left_in_entry, count);
		not_copied = copy_to_user(buf, entry->buffptr + pos_in_entry, bytes_to_read);
		if (not_copied == bytes_to_read) {
			return -EFAULT;
		}
		bytes_to_read -= not_copied;
		pos += bytes_to_read;
		retval = bytes_to_read;
	}

	*f_pos = pos;

	mutex_unlock(&aesd_dev.mutex);

	return retval;
}

static ssize_t aesd_write(struct file *filp, const char __user *buf, size_t count, loff_t *f_pos)
{
	ssize_t retval;
	int ret;
	ssize_t not_copied;

	PDEBUG("write %zu bytes with offset %lld", count, *f_pos);

	ret = mutex_lock_interruptible(&aesd_dev.mutex);
	if (ret != 0) {
		return ret;
	}

	wrtbuf.buffptr = (wrtbuf.buffptr == NULL) ? kmalloc(count, GFP_KERNEL) :
						    krealloc(wrtbuf.buffptr, wrtbuf.size + count, GFP_KERNEL);
	if (wrtbuf.buffptr == NULL) {
		return -ENOMEM;
	}

	not_copied = copy_from_user(wrtbuf.buffptr + wrtbuf.size, buf, count);
	if (not_copied == count) {
		return -EFAULT;
	}
	count -= not_copied;
	wrtbuf.size += count;
	retval = count;

	if (wrtbuf.buffptr[wrtbuf.size - 1] == '\n') {
		struct aesd_buffer_entry entry = { .buffptr = wrtbuf.buffptr, .size = wrtbuf.size };
		if (aesd_circular_buffer_is_full(&aesd_dev.circ_buf)) {
			struct aesd_buffer_entry *popped_entry = aesd_circular_buffer_pop_entry(&aesd_dev.circ_buf);
			kfree(popped_entry->buffptr);
		}
		aesd_circular_buffer_add_entry(&aesd_dev.circ_buf, &entry);
		wrtbuf.size = 0;
		wrtbuf.buffptr = NULL;
	}

	mutex_unlock(&aesd_dev.mutex);

	return retval;
}

struct file_operations aesd_fops = {
	.owner = THIS_MODULE,
	.read = aesd_read,
	.write = aesd_write,
	.open = aesd_open,
	.release = aesd_release,
	.llseek = default_llseek,
};

static int aesd_dev_uevent(struct device *dev, struct kobj_uevent_env *env)
{
	add_uevent_var(env, "DEVMODE=0666");
	return 0;
}

static int __init aesd_init_module(void)
{
	int status = 0;

	aesd_major = register_chrdev(0, DRV_NAME, &aesd_fops);
	if (aesd_major < 0) {
		printk(KERN_ERR "Error %d registering aesd char driver", aesd_major);
		return aesd_major;
	}
	PDEBUG("aesd char driver registered with major %d", aesd_major);

	aesd_class = class_create(THIS_MODULE, DRV_NAME);
	if (!aesd_class) {
		status = -ENOMEM;
		goto free_chrdev;
	}
	aesd_class->dev_uevent = aesd_dev_uevent;

	aesd_device = device_create(aesd_class, NULL, MKDEV(aesd_major, aesd_minor), NULL, DRV_NAME);
	if (!aesd_device) {
		status = -ENOMEM;
		goto free_class;
	}

	aesd_circular_buffer_init(&aesd_dev.circ_buf);
	wrtbuf.buffptr = NULL;
	wrtbuf.size = 0;

	mutex_init(&aesd_dev.mutex);

	goto exit;

free_class:
	class_unregister(aesd_class);
	class_destroy(aesd_class);
free_chrdev:
	unregister_chrdev(aesd_major, DRV_NAME);
exit:
	return status;
}

static void __exit aesd_cleanup_module(void)
{
	uint8_t index;
	struct aesd_buffer_entry *entry;
	AESD_CIRCULAR_BUFFER_FOREACH(entry, &aesd_dev.circ_buf, index)
	{
		kfree(entry->buffptr);
	}

	mutex_destroy(&aesd_dev.mutex);
	device_destroy(aesd_class, MKDEV(aesd_major, aesd_minor));
	class_unregister(aesd_class);
	class_destroy(aesd_class);
	unregister_chrdev(aesd_major, DRV_NAME);
}

module_init(aesd_init_module);
module_exit(aesd_cleanup_module);
