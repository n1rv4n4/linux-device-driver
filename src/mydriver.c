#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/kernel.h>	/* printk() */
#include <linux/slab.h>		/* kmalloc() */
#include <linux/fs.h>		/* everything... */
#include <linux/errno.h>	/* error codes */
#include <linux/types.h>	/* size_t */
#include <linux/proc_fs.h>
#include <linux/fcntl.h>	/* O_ACCMODE */
#include <linux/seq_file.h>
#include <linux/cdev.h>
#include <asm/switch_to.h>		/* cli(), *_flags */
#include <asm/uaccess.h>	/* copy_*_user */
#include <linux/uaccess.h>

#include "mydriver_ioctl.h"

#define DRIVER_MAJOR 0
#define DRIVER_NR_DEVS 4 ///< number of devices
#define CLASS_NAME "queue"        ///< /dev/CLASS_NAME/
#define  DEVICE_NAME "0"    ///< /dev/CLASS_NAME/DEVICE_NAME , might be redundant  

int driver_major = DRIVER_MAJOR;
int driver_minor = 0;
int driver_nr_devs = DRIVER_NR_DEVS;

module_param(driver_major, int, S_IRUGO);
module_param(driver_minor, int, S_IRUGO);
module_param(driver_nr_devs, int, S_IRUGO);

MODULE_AUTHOR("Furkan Cakir, Hakan Eroztekin, Orcun Ozdemir");
MODULE_LICENSE("GPL v3");
MODULE_DESCRIPTION("A simple character device which uses queue to read and write operations.");
MODULE_VERSION("1.0");

struct driver_dev {
    char *data;
    unsigned long size;
    struct semaphore sem;
    struct cdev cdev;
};

struct driver_dev *driver_devices;

int driver_open(struct inode *inode, struct file *filp)
{
	printk(KERN_INFO "My Driver: Device open is called.\n");

    struct driver_dev *dev;

    dev = container_of(inode->i_cdev, struct driver_dev, cdev);
    filp->private_data = dev;

    /* trim the device if open was write-only */
    if ((filp->f_flags & O_ACCMODE) == O_WRONLY) {
		printk(KERN_INFO "My Driver: Open request is write only.\n");
        if (down_interruptible(&dev->sem)) {
            printk(KERN_INFO "My Driver: Device is not interruptable.\n");
            return -ERESTARTSYS;
        }

    if (dev->data) {
        kfree(dev->data);
    }
    dev->data = NULL;
    dev->size = 0;
        up(&dev->sem);
    }
	printk(KERN_INFO "My Driver: Device opened successfully.\n");
    return 0;
}

int driver_release(struct inode *inode, struct file *filp)
{
	printk(KERN_INFO "My Driver: Device is released.\n");
    return 0;
}

/*
 *  @param filp A pointer to a file object
 *  @param buf The buffer to that contains the string to write to the device
 *  @param count The length of the array of data that is being passed in the const char buffer
 *  @param f_pos is the offset if required
 */

ssize_t driver_read(struct file *filp, char __user *buf, size_t count, 
                   loff_t *f_pos)
 {
    printk(KERN_INFO "My Driver: Device read is going to be executed.\n");
    struct driver_dev *dev = filp->private_data;
    int quantum = dev->quantum;
    int s_pos, q_pos;
    ssize_t retval = 0;

    if (down_interruptible(&dev->sem))
        printk(KERN_INFO "My Driver: Device is not interruptable.\n");
        return -ERESTARTSYS;
    if (*f_pos >= dev->size)
        goto out;
    if (*f_pos + count > dev->size)
        count = dev->size - *f_pos;

    s_pos = (long) *f_pos;
    q_pos = 0;

    if (dev->data == NULL)
        goto out;

    /* read only up to the end of this quantum */
    if (count > 1)
        count = quantum - q_pos;

    if (copy_to_user(buf, dev->data, count)) {
        retval = -EFAULT;
        goto out;
    }
    *f_pos += count;
    retval = count;

  out:
    up(&dev->sem);
    printk(KERN_INFO "My Driver: Returning from the read function.\n");
    return retval;
}


/*
 *  @param filp A pointer to a file object
 *  @param buf The buffer to that contains the string to write to the device
 *  @param count The length of the array of data that is being passed in the const char buffer
 *  @param f_pos is the offset if required
 */

ssize_t driver_write(struct file *filp, const char __user *buf, size_t count,
                    loff_t *f_pos) // Send data to the device
{
    struct driver_dev *dev = filp->private_data;
    printk(KERN_INFO "%d", count);
    printk(KERN_INFO "%s", buf);
    if (!dev->data) {
        dev->data = kmalloc(50 * sizeof(char *), GFP_KERNEL);
        if (!dev->data)
            printk(KERN_INFO "lel");
        memset(dev->data, 0, 50 * sizeof(char *));
    }
    copy_from_user(dev->data, buf, count);
    *f_pos += count;
    dev->size = *f_pos;
    printk(KERN_INFO "%d", *f_pos);
    up(&dev->sem);
    return count;
    /*
    struct driver_dev *dev = filp->private_data;
    int s;
    ssize_t retval = -ENOMEM;

    if (down_interruptible(&dev->sem)) {
        printk(KERN_INFO "My Driver: Device is not interruptable.\n");
        return -ERESTARTSYS;
        }

    if (*f_pos >= count) {
        printk(KERN_INFO "My Driver: f_pos >= count.\n");
        retval = 0;
        goto out;
    }

    if (copy_from_user(dev->data, buf, count)) {
        printk(KERN_INFO "My Driver: Device copied from user.\n");
        retval = -EFAULT;
        goto out;
    }

    *f_pos += count;
    retval = count;

    /* update the size 
    if (dev->size < *f_pos)
        dev->size = *f_pos;

  out:
    up(&dev->sem);
	printk(KERN_INFO "My Driver: Returning from the read function.\n");
    return retval;
    */
    
}

long driver_ioctl(struct file *filp, unsigned int cmd, unsigned long arg) // Called by the ioctl system call
{

	int err = 0, tmp;
	int retval = 0;

	/*
	 * extract the type and number bitfields, and don't decode
	 * wrong cmds: return ENOTTY (inappropriate ioctl) before access_ok()
	 */
	if (_IOC_TYPE(cmd) != DRIVER_IOC_MAGIC) return -ENOTTY;
	if (_IOC_NR(cmd) > DRIVER_IOC_MAXNR) return -ENOTTY;

	/*
	 * the direction is a bitmask, and VERIFY_WRITE catches R/W
	 * transfers. `Type' is user-oriented, while
	 * access_ok is kernel-oriented, so the concept of "read" and
	 * "write" is reversed
	 */
	if (_IOC_DIR(cmd) & _IOC_READ)
		err = !access_ok(VERIFY_WRITE, (void __user *)arg, _IOC_SIZE(cmd));
	else if (_IOC_DIR(cmd) & _IOC_WRITE)
		err =  !access_ok(VERIFY_READ, (void __user *)arg, _IOC_SIZE(cmd));
	if (err) return -EFAULT;

	switch(cmd) {
	  case DRIVER_IOCRESET:
		break;

	  default:  /* redundant, as cmd was checked against MAXNR */
		return -ENOTTY;
	}
	return retval;
}


loff_t driver_llseek(struct file *filp, loff_t off, int whence) // Change current read/write position in a file 
{
    struct driver_dev *dev = filp->private_data;
    loff_t newpos;

    switch(whence) {
        case 0: /* SEEK_SET */
            newpos = off;
            break;

        case 1: /* SEEK_CUR */
            newpos = filp->f_pos + off;
            break;

        case 2: /* SEEK_END */
            newpos = dev->size + off;
            break;

        default: /* can't happen */
            return -EINVAL;
    }
    if (newpos < 0)
        return -EINVAL;
    filp->f_pos = newpos;
    return newpos;
}


// location of the following part should not be changed. 
// the functions had to be defined before this part
struct file_operations driver_fops = { 
    .owner =    THIS_MODULE,
    .llseek =   driver_llseek,
    .read =     driver_read,
    .write =    driver_write,
    .unlocked_ioctl =  driver_ioctl,
    .open =     driver_open,
    .release =  driver_release,
};

void driver_cleanup_module(void)
{
	printk(KERN_INFO "My Driver: Cleaning up the LKM.\n");
    int i;
    dev_t devno = MKDEV(driver_major, driver_minor);

    if (driver_devices) {
        for (i = 0; i < driver_nr_devs; i++) {
            cdev_del(&driver_devices[i].cdev);
        }
    kfree(driver_devices);
    }

    unregister_chrdev_region(devno, driver_nr_devs);
    printk(KERN_INFO "My Driver: Goodbye from the LKM!\n");
}

int driver_init_module(void)
{
	printk(KERN_INFO "My Driver: Initializing the LKM...\n");
    
    int result, i;
    int err;
    dev_t devno = 0; // start naming from 0
    struct driver_dev *dev; // pointer to driver_dev struct

    if (driver_major) { // for the first device (!!)
        devno = MKDEV(driver_major, driver_minor);
        result = register_chrdev_region(devno, driver_nr_devs, "driver");
    } else {
        result = alloc_chrdev_region(&devno, driver_minor, driver_nr_devs,
                                     "driver");
        driver_major = MAJOR(devno);
    }
    if (result < 0) {
        printk(KERN_WARNING "My Driver: Failed to register a major number %d.\n", driver_major);
        return result;
    }

    printk(KERN_INFO "My Driver: Registered correctly with major number %d.\n", driver_major);
    
    driver_devices = kmalloc(driver_nr_devs * sizeof(struct driver_dev),
                            GFP_KERNEL); // allocate memory
    if (!driver_devices) {
        result = -ENOMEM;
        printk(KERN_ALERT "My Driver: Failed to create the device.\n");
        goto fail;
    }
    memset(driver_devices, 0, driver_nr_devs * sizeof(struct driver_dev));

    /* Initialize each device. */
    for (i = 0; i < driver_nr_devs; i++) {
        dev = &driver_devices[i];
        sema_init(&dev->sem,1);
        devno = MKDEV(driver_major, driver_minor + i);
        cdev_init(&dev->cdev, &driver_fops);
        dev->cdev.owner = THIS_MODULE;
        dev->cdev.ops = &driver_fops;
        err = cdev_add(&dev->cdev, devno, 1);
        if (err)
            printk(KERN_NOTICE "My Driver: Error %d adding%d.\n", err, i);
    }
    printk(KERN_INFO "My Driver: Device initialized correctly.\n"); // device is initialized
    return 0; /* succeed */

  fail:
    driver_cleanup_module();
    return result;
}

module_init(driver_init_module);
module_exit(driver_cleanup_module);
