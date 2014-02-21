/*
 * Block driver working on dataspaces.
 * (c) 2012 Steffen Liebergeld (steffen@sec.t-labs.tu-berlin.de)
 * (c) 2011 Matthias Lange (mlange@sec.t-labs.tu-berlin.de)
 * Licenced under the terms of the GPL version 2.0.
 * Based on l4bdds.c by Adam Lackorzynski (adam@os.inf.tu-dresden.de)
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>

#include <linux/blkdev.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/genhd.h>
#include <linux/hdreg.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/vmalloc.h>
#include <asm/l4.h>

MODULE_AUTHOR("Matthias Lange <mlange@sec.t-labs.tu-berlin.de");
MODULE_DESCRIPTION("Block driver for virtualized block devices");
MODULE_LICENSE("GPL");

enum { NR_DEVS = 4 };

static int devs_pos;
static int major_num = 0;        /* kernel chooses */
module_param(major_num, int, 0);

#define KERNEL_SECTOR_SIZE 512

static void karma_bdds_write(unsigned long addr, unsigned long val){
	karma_hypercall1(KARMA_MAKE_COMMAND(KARMA_DEVICE_ID(bdds), addr | 1), &val);
}

static unsigned long karma_bdds_read(unsigned long addr){
	unsigned long val = 0;
	karma_hypercall1(KARMA_MAKE_COMMAND(KARMA_DEVICE_ID(bdds), addr), &val);
	return val;
}

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

static struct karma_bdds_device device[NR_DEVS];

void *read_buf;
void *write_buf;

static void transfer(struct karma_bdds_device *dev,
		     char *addr, unsigned long size,
		     char *buffer, int write)
{
	if (write)
	{

	}
	else
	{
		karma_bdds_write(karma_bdds_df_transfer_read, (unsigned long)addr);
		memcpy(buffer, read_buf, size);
	}
}

static void request(struct request_queue *q)
{
	struct request *req;
	char *addr = 0;
	
	while ((req = blk_peek_request(q)) != NULL) {
		struct req_iterator iter;
		struct bio_vec *bvec;

		blk_start_request(req);

#define blk_fs_request(rq)      ((rq)->cmd_type == REQ_TYPE_FS)
		if (!blk_fs_request(req)) {
			printk(KERN_NOTICE "Skip non-CMD request\n");
			__blk_end_request_all(req, -EIO);
			continue;
		}

		addr += blk_rq_pos(req) * KERNEL_SECTOR_SIZE;

		rq_for_each_segment(bvec, req, iter) {
			transfer(req->rq_disk->private_data, addr,
			         bvec->bv_len,
			         page_address(bvec->bv_page)
			           + bvec->bv_offset,
			         rq_data_dir(req) == WRITE);
			addr += bvec->bv_len;
		}
		__blk_end_request_all(req, 0);
	}
}



static int getgeo(struct block_device *bdev, struct hd_geometry *geo)
{
	struct karma_bdds_device *d = bdev->bd_disk->private_data;
	geo->cylinders = d->size << 5;
	geo->heads     = 4;
	geo->sectors   = 32;
	return 0;
}


/*
 * The device operations structure.
 */
static struct block_device_operations ops = {
	.owner  = THIS_MODULE,
	.getgeo = getgeo,
};

static int __init karma_bdds_init_one(int nr)
{
	int ret;
	unsigned long bd_size = 0;
	unsigned long size = 0;
	// TODO: init stuff in karma, return size

	if(device[nr].read_write)
	{
		karma_bdds_write(karma_bdds_df_mode, 1);
	}
	else
	{
		karma_bdds_write(karma_bdds_df_mode, 0);
	}

	bd_size = karma_bdds_read(karma_bdds_df_disk_size);

	// remap shared read/write buffers
	size = karma_bdds_read(karma_bdds_df_transfer_ds_size);
	printk(KERN_INFO "[KARMA_BDDS] shared buffer size 0x%lx bytes\n", size);

	read_buf = (void*)karma_bdds_read(karma_bdds_df_transfer_read_ds);
	read_buf = ioremap((int)read_buf, size);

	write_buf = (void*)karma_bdds_read(karma_bdds_df_transfer_write_ds);
	write_buf = ioremap((int)write_buf, size);

	ret = -ENODEV;
	/* device[nr].size is unsigned and in KBytes */
	device[nr].size = ALIGN(bd_size, 1 << 10) >> 10;

	spin_lock_init(&device[nr].lock);

	printk("[KARMA_BDDS] Disk '%s' size = %lu KB (%lu MB)\n",
	       device[nr].name, device[nr].size, device[nr].size >> 10);

	/* Get a request queue. */
	device[nr].queue = blk_init_queue(request, &device[nr].lock);
	if (device[nr].queue == NULL)
		goto out1;

	/* gendisk structure. */
	device[nr].gd = alloc_disk(16);
	if (!device[nr].gd)
		goto out2;
	device[nr].gd->major        = major_num;
	device[nr].gd->first_minor  = nr * 16;
	device[nr].gd->fops         = &ops;
	device[nr].gd->private_data = &device[nr];
	snprintf(device[nr].gd->disk_name, sizeof(device[nr].gd->disk_name),
	         "karma_bdds%d", nr);
	set_capacity(device[nr].gd, device[nr].size * 2 /* 2 * kb = 512b-sectors */);

	if (!(device[nr].read_write))
		set_disk_ro(device[nr].gd, 1);

	device[nr].gd->queue = device[nr].queue;
	add_disk(device[nr].gd);

	return 0;

out2:
	blk_cleanup_queue(device[nr].queue);
out1:
	// TODO: cleanup stuff in karma
	if(device[nr].read_write)
	{
	}
	else
	{
	}
	return ret;
}

static int __init karma_bdds_init(void)
{
	int i;

	printk("[KARMA_BDDS] %s\n", __func__);

	if (!devs_pos) {
		printk("[KARMA_BDDS] No name given, not starting.\n");
		return 0;
	}

	/* Register device */
	major_num = register_blkdev(major_num, "karma_bdds");
	if (major_num <= 0) {
		printk(KERN_WARNING "[KARMA_BDDS] unable to get major number\n");
		return -ENODEV;
	}

	for (i = 0; i < devs_pos; ++i)
		karma_bdds_init_one(i);

	return 0;
}

static void __exit karma_bdds_exit_one(int nr)
{
	del_gendisk(device[nr].gd);
	put_disk(device[nr].gd);
	blk_cleanup_queue(device[nr].queue);

	// TODO: cleanup stuff in karma
	if(device[nr].read_write)
	{
	}
	else
	{
	}
}

static void __exit karma_bdds_exit(void)
{
	int i;

	for (i = 0; i < devs_pos; i++)
		karma_bdds_exit_one(i);

	unregister_blkdev(major_num, "karma_bdds");
}

module_init(karma_bdds_init);
module_exit(karma_bdds_exit);

static int l4bdds_setup(const char *val, struct kernel_param *kp)
{
	char *p;
	unsigned s;

	if (devs_pos >= NR_DEVS) {
		printk("l4bdds: Too many block devices specified\n");
		return 1;
	}
	if ((p = strstr(val, ",rw")))
		device[devs_pos].read_write = 1;
	s = p ? p - val : (sizeof(device[devs_pos].name) - 1);
	strlcpy(device[devs_pos].name, val, s + 1);
	devs_pos++;
	return 0;
}

module_param_call(add, l4bdds_setup, NULL, NULL, 0200);
MODULE_PARM_DESC(add, "Use one l4bdds.add=name[,rw] for each block device to add, name queried in namespace");
