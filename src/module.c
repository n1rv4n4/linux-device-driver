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

#include "module_ioctl.h"

#define SCULL_MAJOR 0
#define SCULL_NR_DEVS 4 ///< number of devices
#define SCULL_QUANTUM 4000
#define SCULL_QSET 1000
/// #define  CLASS_NAME  "ebb"        ///< /dev/CLASS_NAME/
/// #define  DEVICE_NAME "queue"    ///< /dev/CLASS_NAME/DEVICE_NAME , might be redundant  

int scull_major = SCULL_MAJOR;
int scull_minor = 0;
int scull_nr_devs = SCULL_NR_DEVS;
int scull_quantum = SCULL_QUANTUM;
int scull_qset = SCULL_QSET;

module_param(scull_major, int, S_IRUGO);
module_param(scull_minor, int, S_IRUGO);
module_param(scull_nr_devs, int, S_IRUGO);
module_param(scull_quantum, int, S_IRUGO);
module_param(scull_qset, int, S_IRUGO);

MODULE_AUTHOR("Alessandro Rubini, Jonathan Corbet");
MODULE_LICENSE("Dual BSD/GPL");
/*Changes 1
char **data ---> char *data //birden çok yerine tek char arrayi tutulacak
dev->data[s_pos] ve dev->data[i]---->dev->data//tek olan char arrayi gösteriyor
s_pos//kullanılmadığı için silindi
q_pos = (long) *f_pos%quantum --->q_pos = (long) *f_pos//tek array olduğundan arraydeki yeri = dosyadaki yeri
*/

/*
    scull_open(): Called each time the device is opened from user space.
    scull_read(): Called when data is sent from the device to user space.
    scull_write(): Called when data is sent from user space to the device.
    scull_release(): Called when the device is closed in user space.
*/


/* Changes 2
Added comments & explanations for all the functions.
Namely; scull_init, scull_exit, scull_open, scull_release, scull_trim, scull_read, scull_write
*/

/* Remaining Changes
*Queue Operations
* Modify the "write" function: Writing to a queue device will insert the written text to the end of the queue. 
* Modify the "read" function: When reading from the queue, entries in the queue will behave as concatenated strings.
* Implement "pop" function: ioctl command named "pop" will return the entry at the front of the queue and remove it from the queue
*
*Integrate "pop" with queue0
* /dev/queue0 will be a special queue which will only be used through the ioctl command described above (no read or write).
* It will pop the front element from queue1 if the queue is not empty.
* Otherwise it will try the remaining queues in order and pop from the first queue that is not empty. 
*
*Additional
* Device names should be "queue", not "scull".
* The number of queue devices will be a module parameter.
* Quantum operations should be removed
*/

struct scull_dev {
    char *data;
    int quantum;
    int qset;
    unsigned long size;
    struct semaphore sem;
    struct cdev cdev;
};

struct scull_dev *scull_devices;


int scull_trim(struct scull_dev *dev)
{
	printk(KERN_INFO "scull: trim function is going to be executed\n");
    int i;

    if (dev->data) {
        for (i = 0; i < dev->qset; i++) {
            if (dev->data)
                kfree(dev->data);
        }
        kfree(dev->data);
    }
    dev->data = NULL;
    dev->quantum = scull_quantum;
    dev->qset = scull_qset;
    dev->size = 0;
	printk(KERN_INFO "scull: trimming is done\n");
    return 0;
}


int scull_open(struct inode *inode, struct file *filp) // First operation performed in the device file
{
	printk(KERN_INFO "scull: device open is called\n");
    struct scull_dev *dev;

    dev = container_of(inode->i_cdev, struct scull_dev, cdev);
    filp->private_data = dev;

    /* trim the device if open was write-only */
    if ((filp->f_flags & O_ACCMODE) == O_WRONLY) {
		printk(KERN_INFO "scull: device will be trimmed\n");
        if (down_interruptible(&dev->sem))
            return -ERESTARTSYS;
        scull_trim(dev);
        up(&dev->sem);
    }
	printk(KERN_INFO "scull: device opened successfully\n");
    return 0;
}


int scull_release(struct inode *inode, struct file *filp) // Release the file structure
{
	printk(KERN_INFO "scull: device is released\n");
    return 0;
}

/*
 *  @param filp A pointer to a file object
 *  @param buf The buffer to that contains the string to write to the device
 *  @param count The length of the array of data that is being passed in the const char buffer
 *  @param f_pos is the offset if required
 */
ssize_t scull_read(struct file *filp, char __user *buf, size_t count, 
                   loff_t *f_pos) // Read data from the device
 {
    printk(KERN_INFO "scull: device read is going to be executed\n");
    struct scull_dev *dev = filp->private_data;
    int quantum = dev->quantum;
    int q_pos;
    ssize_t retval = 0;

    if (down_interruptible(&dev->sem))
        return -ERESTARTSYS;
    if (*f_pos >= dev->size)
        goto out;

    q_pos = (long) *f_pos ;

    if (dev->data == NULL || ! dev->data)
        goto out;

    if (copy_to_user(buf, dev->data, dev->size)) {
		printk(KERN_INFO "scull: failed to send data to the user/application\n");
        retval = -EFAULT;
        goto out;
    }
    retval = dev->size;

  out:
    up(&dev->sem);
	printk(KERN_INFO "scull: returning from the read function\n");
    return retval;
}


/*
 *  @param filp A pointer to a file object
 *  @param buf The buffer to that contains the string to write to the device
 *  @param count The length of the array of data that is being passed in the const char buffer
 *  @param f_pos is the offset if required
 */
ssize_t scull_write(struct file *filp, const char __user *buf, size_t count,
                    loff_t *f_pos) // Send data to the device
{
	printk(KERN_INFO "scull: device write is going to be executed\n");
    struct scull_dev *dev = filp->private_data;
    int quantum = dev->quantum, qset = dev->qset;
    int s, q_pos;
    ssize_t retval = -ENOMEM;

    if (down_interruptible(&dev->sem))
        return -ERESTARTSYS;

    if (*f_pos >= quantum * qset) {
        retval = 0;
        goto out;
    }

    q_pos = (long) *f_pos;

    if (!dev->data) {
        dev->data = kmalloc(qset * sizeof(char *), GFP_KERNEL);
        if (!dev->data)
            goto out;
        memset(dev->data, 0, qset * sizeof(char *));
    }
    if (!dev->data) {
        dev->data= kmalloc(quantum, GFP_KERNEL);
        if (!dev->data)
            goto out;
    }

    if (copy_from_user(dev->data + q_pos, buf, count)) {
        retval = -EFAULT;
        goto out;
    }
    *f_pos += count;
    retval = count;

    /* update the size */
    if (dev->size < *f_pos)
        dev->size = *f_pos;

  out:
    up(&dev->sem);
	printk(KERN_INFO "scull: returning from the read function\n");
    return retval;
}

long scull_ioctl(struct file *filp, unsigned int cmd, unsigned long arg) // Called by the ioctl system call
{

	int err = 0, tmp;
	int retval = 0;

	/*
	 * extract the type and number bitfields, and don't decode
	 * wrong cmds: return ENOTTY (inappropriate ioctl) before access_ok()
	 */
	if (_IOC_TYPE(cmd) != SCULL_IOC_MAGIC) return -ENOTTY;
	if (_IOC_NR(cmd) > SCULL_IOC_MAXNR) return -ENOTTY;

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
	  case SCULL_IOCRESET:
		scull_quantum = SCULL_QUANTUM;
		scull_qset = SCULL_QSET;
		break;

	  case SCULL_IOCSQUANTUM: /* Set: arg points to the value */
		if (! capable (CAP_SYS_ADMIN))
			return -EPERM;
		retval = __get_user(scull_quantum, (int __user *)arg);
		break;

	  case SCULL_IOCTQUANTUM: /* Tell: arg is the value */
		if (! capable (CAP_SYS_ADMIN))
			return -EPERM;
		scull_quantum = arg;
		break;

	  case SCULL_IOCGQUANTUM: /* Get: arg is pointer to result */
		retval = __put_user(scull_quantum, (int __user *)arg);
		break;

	  case SCULL_IOCQQUANTUM: /* Query: return it (it's positive) */
		return scull_quantum;

	  case SCULL_IOCXQUANTUM: /* eXchange: use arg as pointer */
		if (! capable (CAP_SYS_ADMIN))
			return -EPERM;
		tmp = scull_quantum;
		retval = __get_user(scull_quantum, (int __user *)arg);
		if (retval == 0)
			retval = __put_user(tmp, (int __user *)arg);
		break;

	  case SCULL_IOCHQUANTUM: /* sHift: like Tell + Query */
		if (! capable (CAP_SYS_ADMIN))
			return -EPERM;
		tmp = scull_quantum;
		scull_quantum = arg;
		return tmp;

	  case SCULL_IOCSQSET:
		if (! capable (CAP_SYS_ADMIN))
			return -EPERM;
		retval = __get_user(scull_qset, (int __user *)arg);
		break;

	  case SCULL_IOCTQSET:
		if (! capable (CAP_SYS_ADMIN))
			return -EPERM;
		scull_qset = arg;
		break;

	  case SCULL_IOCGQSET:
		retval = __put_user(scull_qset, (int __user *)arg);
		break;

	  case SCULL_IOCQQSET:
		return scull_qset;

	  case SCULL_IOCXQSET:
		if (! capable (CAP_SYS_ADMIN))
			return -EPERM;
		tmp = scull_qset;
		retval = __get_user(scull_qset, (int __user *)arg);
		if (retval == 0)
			retval = put_user(tmp, (int __user *)arg);
		break;

	  case SCULL_IOCHQSET:
		if (! capable (CAP_SYS_ADMIN))
			return -EPERM;
		tmp = scull_qset;
		scull_qset = arg;
		return tmp;

	  default:  /* redundant, as cmd was checked against MAXNR */
		return -ENOTTY;
	}
	return retval;
}


loff_t scull_llseek(struct file *filp, loff_t off, int whence) // Change current read/write position in a file 
{
    struct scull_dev *dev = filp->private_data;
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
struct file_operations scull_fops = { 
    .owner =    THIS_MODULE,
    .llseek =   scull_llseek,
    .read =     scull_read,
    .write =    scull_write,
    .unlocked_ioctl =  scull_ioctl,
    .open =     scull_open,
    .release =  scull_release,
};


void scull_cleanup_module(void)
{
	printk(KERN_INFO "scull: cleaning up the LKM\n");
    int i;
    dev_t devno = MKDEV(scull_major, scull_minor);

    if (scull_devices) {
        for (i = 0; i < scull_nr_devs; i++) {
            scull_trim(scull_devices + i);
            cdev_del(&scull_devices[i].cdev);
        }
    kfree(scull_devices);
    }

    unregister_chrdev_region(devno, scull_nr_devs);
    printk(KERN_INFO "scull: Goodbye from the LKM!\n");
}


int scull_init_module(void)
{
	printk(KERN_INFO "scull: initializing the LKM\n");
    
    int result, i;
    int err;
    dev_t devno = 0; // start naming from 0
    struct scull_dev *dev; // pointer to scull_dev struct

    if (scull_major) { // for the first device (!!)
        devno = MKDEV(scull_major, scull_minor);
        result = register_chrdev_region(devno, scull_nr_devs, "scull");
    } else {
        result = alloc_chrdev_region(&devno, scull_minor, scull_nr_devs,
                                     "scull");
        scull_major = MAJOR(devno);
    }
    if (result < 0) {
        printk(KERN_WARNING "scull: failed to register a major number %d\n", scull_major);
        return result;
    }

    printk(KERN_INFO "scull: registered correctly with major number %d\n", scull_major);
    
    scull_devices = kmalloc(scull_nr_devs * sizeof(struct scull_dev),
                            GFP_KERNEL); // allocate memory
    if (!scull_devices) {
        result = -ENOMEM;
        printk(KERN_ALERT "Failed to create the device\n");
        goto fail;
    }
    memset(scull_devices, 0, scull_nr_devs * sizeof(struct scull_dev));

    /* Initialize each device. */
    for (i = 0; i < scull_nr_devs; i++) {
        dev = &scull_devices[i];
        dev->quantum = scull_quantum;
        dev->qset = scull_qset;
        sema_init(&dev->sem,1);
        devno = MKDEV(scull_major, scull_minor + i);
        cdev_init(&dev->cdev, &scull_fops);
        dev->cdev.owner = THIS_MODULE;
        dev->cdev.ops = &scull_fops;
        err = cdev_add(&dev->cdev, devno, 1);
        if (err)
            printk(KERN_NOTICE "scull: Error %d adding scull%d\n", err, i);
    }
    printk(KERN_INFO "scull: device initialized correctly\n"); // device is initialized
    return 0; /* succeed */

  fail:
    scull_cleanup_module();
    return result;
}

module_init(scull_init_module);
module_exit(scull_cleanup_module);
