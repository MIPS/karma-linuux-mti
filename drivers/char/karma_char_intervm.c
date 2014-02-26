/**
 *
 * Copyright  (c) 2015 Elliptic Technologies
 *
 * \author  Jason Butler jbutler@elliptictech.com
 *
 * This file is part of TUD:OS and distributed under the terms of the
 * GNU General Public License 2.
 * Please see the COPYING-GPL-2 file for details.
 *
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/kobject.h>
#include <linux/interrupt.h>

#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/hdreg.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/vmalloc.h>
#include <asm/l4.h>
#include <linux/wait.h>
#include <linux/syscalls.h>
#include <linux/cdev.h>

MODULE_AUTHOR("Jason Butler <jbutler@elliptictech.com>");
MODULE_DESCRIPTION("Char driver for inter-virtual machine data passing");
MODULE_LICENSE("GPL");

#define INTERVM_ATTR_RO(_name) \
        static struct kobj_attribute _name##_attr = __ATTR_RO(_name)
#define INTERVM_ATTR(_name) \
        static struct kobj_attribute _name##_attr = \
               __ATTR(_name, 0644, _name##_show, _name##_store)

struct intervm_dev_t {
    void **data;
    struct mutex mutex;
    struct cdev cdev;
};

struct intervm_dev_t intervm_dev;

struct kobject *intervm_kobj;
enum { NR_DEVS = 4 };

static int intervm_devs = 1;
static int major_num = 0;        /* kernel chooses */
static unsigned long intervm_buffer_size = 1;
static u32 intervm_irq_data;         /* our output to the world  */
static DEFINE_SPINLOCK(intervm_lock);
static DEFINE_MUTEX(i_mutex);    //mutex to protect the write_buf_ptr

module_param(major_num, int, 0);
module_param(intervm_devs, int, 0);

//#define DEBUG

#define BUF_LEN 16384
#define READ_BUF_LEN 16384
static char _shared_mem[BUF_LEN] __attribute__((aligned(4096)));
static char _shared_mem_read[READ_BUF_LEN] __attribute__((aligned(4096)));

static unsigned long write_buf_ptr;

//static uint8_t msg[BUF_LEN];
//static char *msg_Ptr;

static DECLARE_WAIT_QUEUE_HEAD(intervm_wait);

#define KERNEL_SECTOR_SIZE 512

#define SUCCESS 0

#define INTERVM_IRQ 3

#define DRIVER_PROD_CONS

#ifdef DRIVER_PROD
static void karma_intervm_write(unsigned long opcode, unsigned long val){
	printk("[KARMA_CHR_INTERVM_PROD] %s\n", __func__);
        karma_hypercall1(KARMA_MAKE_COMMAND(KARMA_DEVICE_ID(shm_prod), opcode), &val);
}

static void karma_intervm_write2(unsigned long opcode, unsigned long val, unsigned long val2){
       printk("[KARMA_CHR_INTERVM_PROD] %s\n", __func__);
       karma_hypercall2(KARMA_MAKE_COMMAND(KARMA_DEVICE_ID(shm_prod), opcode), &val, &val2);
}


static unsigned long karma_intervm_read(unsigned long opcode){
        unsigned long ret = 0; 
        printk("[KARMA_CHR_INTERVM_PROD] %s\n", __func__);
        karma_hypercall1(KARMA_MAKE_COMMAND(KARMA_DEVICE_ID(shm_prod), opcode), &ret); 
        return ret;
}

#else
static void karma_intervm_write(unsigned long opcode, unsigned long val){
#ifdef DEBUG	
        printk("[KARMA_CHR_INTERVM] %s\n", __func__);
#endif
        karma_hypercall1(KARMA_MAKE_COMMAND(KARMA_DEVICE_ID(shm_cons), opcode), &val);
}

static void karma_intervm_write2(unsigned long opcode, unsigned long val, unsigned long val2){
#ifdef DEBUG
       printk("[KARMA_CHR_INTERVM_CONS] %s\n", __func__);
#endif
       karma_hypercall2(KARMA_MAKE_COMMAND(KARMA_DEVICE_ID(shm_cons), opcode), &val, &val2);
}

static unsigned long karma_intervm_read(unsigned long opcode){
        unsigned long ret = 0; 
        karma_hypercall1(KARMA_MAKE_COMMAND(KARMA_DEVICE_ID(shm_cons), opcode), &ret); 
        return ret;
}

static unsigned long karma_intervm_write_read(unsigned long opcode, unsigned long val){
    karma_hypercall1(KARMA_MAKE_COMMAND(KARMA_DEVICE_ID(shm_cons), opcode), &val);
    return val;
}
#endif


/*
 * The internal representation of our device.
 */
struct karma_bdds_device {
	unsigned long size; /* Size in Kbytes */
	spinlock_t lock;
	struct gendisk *gd;
	void *addr;
	struct request_queue *queue;
	int read_write;
	char name[40];
};

void *read_buf;
void *write_buf;

static int intervm_open(struct inode *inode, struct file *filp)
{
    struct intervm_dev_t *dev;

    dev = container_of(inode->i_cdev, struct intervm_dev_t, cdev);
    filp->private_data = dev; 

    return SUCCESS;

}

static ssize_t intervm_write(struct file *filp, const char *bugg, size_t len, loff_t *off)
{
    struct intervm_dev_t *dev = filp->private_data; 
   
    int err;
  
    if(!*off)
        printk(KERN_ALERT "[KARMA_CHR_INTERVM] %s line: %d off not zero: %lld\n", __func__, __LINE__, *off);
 
    if(mutex_lock_interruptible(&dev->mutex))
        return -ERESTARTSYS;

    if(len > BUF_LEN)
       len = BUF_LEN;
    if(copy_from_user(_shared_mem, bugg, len))
    {
        err = -EFAULT;
        goto fail_unlock;
    }

    *off += len;
#ifdef DEBUG   
    printk(KERN_ALERT "[KARMA_CHR_INTERVM] %s len: %d\n", __func__, len);
    printk(KERN_ALERT "%.*s\n", 10, bugg);
#endif
    karma_intervm_write(karma_shmem_ds_write, len);

    mutex_unlock(&dev->mutex);

    return len;

fail_unlock:
    mutex_unlock(&dev->mutex);
    return err;

} 


static int get_bytes_available(void)
{
    return karma_intervm_read(karma_shmem_get_bytes_available);
}

#if 0
static int set_bytes_to_read(unsigned long bytes_to_read)
{
    karma_intervm_write(karma_shmem_ds_set_read_size, bytes_to_read);
}
#endif

static int set_read_bytes_get_ready_bytes(unsigned long bytes_to_read)
{
   return karma_intervm_write_read(karma_shmem_ds_set_read_size, bytes_to_read); 
}


static ssize_t intervm_blocking_read(struct file *filp,
                            char *buffer,
                            size_t length,
                            loff_t *f_pos)
{
#ifdef DEBUG    
    printk("[KARMA_CHR_INTERVM] 7 %s\n", __func__);
#endif

    struct intervm_dev_t *dev = filp->private_data;
    unsigned long bytes_to_copy;
    ssize_t retval;
    int err;
  
    if(!*f_pos)
        printk(KERN_ALERT "[KARMA_CHR_INTERVM] %s line: %d offset not zero: %lld\n", __func__, __LINE__, *f_pos);

    if(!length)
        return 0; 

    if (mutex_lock_interruptible(&dev->mutex))
        return -ERESTARTSYS;

     if(!(filp->f_flags & O_NONBLOCK) && length > set_read_bytes_get_ready_bytes(length))
     {
         do{
             if (wait_event_interruptible(intervm_wait, length <= get_bytes_available()))
             {
                 err = -ERESTARTSYS;
                 set_read_bytes_get_ready_bytes(0);
                 goto fail_unlock;
             }
             else
                break;
         }while(length >  get_bytes_available());
     }
     karma_intervm_write(karma_shmem_ds_shared_mem_read, length);
     bytes_to_copy = length;
#ifdef DEBUG
    printk("[KARMA_CHR_INTERVM] line: %d bytes_to_copy: %d blocking_read: %.*s\n", __LINE__, bytes_to_copy, 10, _shared_mem_read);
#endif
    if(copy_to_user( buffer, _shared_mem_read, bytes_to_copy) )
    {
        err = -EFAULT;
        goto fail_unlock;
    }

#ifdef DEBUG
    printk("[KARMA_CHR_INTERVM line: %d %s returning\n", __LINE__, __func__);
#endif

    mutex_unlock(&dev->mutex);

    *f_pos += bytes_to_copy;
    retval = bytes_to_copy;

    return retval;

fail_unlock:
    mutex_unlock(&dev->mutex);
    return err;
/*
out:
    __set_current_state(TASK_RUNNING);
    remove_wait_queue(&intervm_wait, &wait);
 
    return retval;
*/
}

static int intervm_release(struct inode *inode, struct file *filp)
{
/*
    struct intervm_dev_t *dev = filp->private_data;

    if(mutex_lock_interruptible(&dev->mutex))
        return -ERESTARTSYS;

    Device_Open--;
   
    module_put(THIS_MODULE);

    mutex_unlock(&dev->mutex);
*/
    return 0;
}


static int intervm_fsync(struct file *file, loff_t start, loff_t end, int datasync)
{
    mutex_lock(&i_mutex);
    karma_intervm_write(karma_shmem_ds_write, write_buf_ptr);
    write_buf_ptr = 0;
    mutex_unlock(&i_mutex);
    
    return SUCCESS;
}
/*
 * The device operations structure.
 */
static struct file_operations fops = {
    .open = intervm_open,
    .write = intervm_write,    //needs to be Ross intervm_write_no_sync()
    .fsync = intervm_fsync,
#ifdef DRIVER_PROD
   .read = intervm_blocking_read,                       // intervm_read,
#else
   .read = intervm_blocking_read,
#endif
    .release = intervm_release
};



static ssize_t buffer_size_show(struct kobject *kobj,
                                struct kobj_attribute *attr, char *buf)
{
    return sprintf(buf, "%lu\n", intervm_buffer_size);
}

static ssize_t buffer_size_store(struct kobject *kobj,
                                 struct kobj_attribute *attr,
                                 const char *buf, size_t count)
{
    unsigned long new_buffer_size;
    int err;

    err = strict_strtoul(buf, 10, &new_buffer_size);
    if (err || new_buffer_size > UINT_MAX)
        return -EINVAL;

    intervm_buffer_size = new_buffer_size;
    karma_intervm_write(karma_shmem_set_ds_size, intervm_buffer_size);
    
    return count;
}
INTERVM_ATTR(buffer_size);

static struct attribute *intervm_properties_attrs[] = {
      &buffer_size_attr.attr,
      NULL
};

static const struct attribute_group intervm_properties_group = {
    .name = "properties",
    .attrs = intervm_properties_attrs,
};

static int __init intervm_properties_init(void)
{
    return sysfs_create_group(intervm_kobj, &intervm_properties_group);
}

static void intervm_properties_destroy(void)
{
    sysfs_remove_group(intervm_kobj, &intervm_properties_group);
}



static irqreturn_t intervm_interrupt(int irq, void *dev_id)
{
    spin_lock(&intervm_lock);
    if(intervm_irq_data < 0xFFFFFFFF)
        intervm_irq_data++;
    spin_unlock(&intervm_lock);
    wake_up_interruptible(&intervm_wait);
 
   return IRQ_HANDLED;

}

static void intervm_setup_cdev(struct intervm_dev_t *dev, int index)
{
    int err, devno = MKDEV(major_num, index);
 
    cdev_init(&dev->cdev, &fops);
    dev->cdev.owner = THIS_MODULE;
    dev->cdev.ops = &fops;
    err = cdev_add(&dev->cdev, devno, 1);

    if (err)
        printk(KERN_NOTICE "Error %d adding karma_chr_intervm%d", err, index);
}

static int __init karma_bdds_init(void)
{
	int result;
        dev_t dev = MKDEV(major_num, 0);
#if 0
	printk("[KARMA_BDDS_INTERVM] %s\n", __func__);
        devs_pos = 1;
	if (!devs_pos) {
		printk("[KARMA_BDDS_INTERVM] No name given, not starting.\n");
		return 0;
	}
#endif
	/* Register device */

        printk("[KARMA_CHR_INTERVM] init......\n");

        if (major_num)
        {
            result = register_chrdev_region(dev, intervm_devs, "karma_chr_intervm");
        } 
        else {
            result = alloc_chrdev_region(&dev, 0, intervm_devs, "karma_chr_intervm");
            major_num = MAJOR(dev);
        }
        if (result < 0)
            return result;

        memset(&intervm_dev, 0, sizeof(struct intervm_dev_t));
        mutex_init(&intervm_dev.mutex);
        intervm_setup_cdev(&intervm_dev, 0);       

	//major_num = register_chrdev(0, "karma_chr_intervm", &fops);
	if (major_num <= 0) {
		printk(KERN_WARNING "[KARMA_CHR_INTERVM] unable to get major number\n");
		return -ENODEV;
	}
   
       printk("[KARMA_CHR_INTERVM] init: major number is %d\n", major_num);
    
       intervm_kobj = kobject_create_and_add("elliptic_intervm_driver", NULL);
       if (!intervm_kobj)
           return -ENOMEM;
       
       if (intervm_properties_init())
           return -ENODEV;      
 
       
//#ifdef DRIVER_PROD

       
       karma_intervm_write2(karma_shmem_ds_init, (unsigned long)&_shared_mem, (unsigned long)&_shared_mem_read);
       if (request_irq(INTERVM_IRQ, intervm_interrupt, 0, "intervm", NULL))
       {
           printk(KERN_ERR "intervm: cannot register IRQ %d\n", INTERVM_IRQ);

       }
//#else
//       karma_intervm_write(karma_shmem_ds_init, (unsigned long)&_shared_mem);
//#endif
	return 0;
}


static void __exit karma_bdds_exit(void)
{
        cdev_del(&intervm_dev.cdev);
#if 0
	for (i = 0; i < devs_pos; i++)
		karma_bdds_exit_one(i);
#endif
        intervm_properties_destroy();
	unregister_chrdev_region(MKDEV(major_num, 0), intervm_devs);
}

module_init(karma_bdds_init);
module_exit(karma_bdds_exit);

