/*
 * Framebuffer driver for Linux running inside a VM established by the Karma VMM.
 *
 * 2009 Steffen Liebergeld
 *
 * Based on:
 * Framebuffer driver
 *
 * based on vesafb.c
 *
 * Adam Lackorzynski <adam@os.inf.tu-dresden.de>
 *
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/tty.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/fb.h>
#include <linux/init.h>
#include <linux/proc_fs.h>
#include <linux/input.h>
#include <linux/platform_device.h>
#include <linux/screen_info.h>
#include <linux/interrupt.h>
#include <linux/spinlock.h>
#ifdef CONFIG_L4_FB_DRIVER_DBG
#include <linux/debugfs.h>
#endif

#define l4_addr_t unsigned int

//spinlock_t l4fb_lock = SPIN_LOCK_UNLOCKED;

#include <asm/l4.h>

/* Default values */
enum {
	SCR_DFL_WIDTH  = 640,
	SCR_DFL_HEIGHT = 480,
	SCR_DFL_DEPTH  = 16,
};

static unsigned int xres = SCR_DFL_WIDTH, yres = SCR_DFL_HEIGHT;
static unsigned int depth;

static bool disable, touchscreen, abs2rel=1;
static bool singledev;
static const unsigned int unmaps_per_refresh = 1;

static int redraw_pending;

static unsigned int l4fb_refresh_sleep = 100;
//static int          l4fb_refresh_enabled = 1;

static long irqnum = karma_irq_fb;
/* Mouse and keyboard are split so that mouse button events are not
 * treated as keyboard events in the Linux console. */
struct input_dev *l4input_dev_key;
struct input_dev *l4input_dev_mouse;

#ifdef CONFIG_L4_FB_DRIVER_DBG
static struct dentry *debugfs_dir, *debugfs_unmaps, *debugfs_updates;
static unsigned int stats_unmaps, stats_updates;
#endif

static void *ev_ptr;
static unsigned long ev_size;
static int ev_elems = 0;

// include/l4/re/c/event.h
/**
 * \brief Event structure used in buffer.
 */
typedef struct
{
  long long time;         /**< Time stamp of the event */
  unsigned short type;    /**< Type of the event */
  unsigned short code;    /**< Code of the event */
  int value;              /**< Value of the event */
  u32 stream_id;  /**< Stream ID */
} l4re_event_t;


// include/l4/l4con/l4con_ev.h
/** Event type */
#define EV_CON          0x10

/* EV_CON event codes */
#define EV_CON_RESERVED    0            /**< invalid request */
#define EV_CON_REDRAW      1            /**< requests client redraw */
#define EV_CON_BACKGROUND  2            /**< tells client that it looses */

// include/l4/re/c/fb.h
/**
 * \brief Framebuffer information structure
 * \ingroup api_l4re_c_fb
 */
typedef struct
{
    unsigned long base_offset;    ///< Offset to first pixel in data space
    unsigned long mem_total;      ///< Size of the frame buffer
    unsigned long map_size;       ///< Do not use, use fb_ds.size() instead
    unsigned x_res;               ///< Width of the frame buffer
    unsigned y_res;               ///< Height of the frame buffer
    unsigned bytes_per_scan_line; ///< Bytes per line
    unsigned flags;               ///< Flags of the framebuffer
    char bits_per_pixel;          ///< Bits per pixel
    char bytes_per_pixel;         ///< Bytes per pixel
    char red_size;                ///< Size of red color component in a pixel
    char red_shift;               ///< Shift of the red color component in a pixel
    char green_size;              ///< Size of green color component in a pixel
    char green_shift;             ///< Shift of the green color component in a pixel
    char blue_size;               ///< Size of blue color component in a pixel
    char blue_shift;              ///< Shift of the blue color component in a pixel
} l4re_fb_info_t;

/* -- module parameter variables ---------------------------------------- */
static int refreshsleep = -1;

/* -- framebuffer variables/structures --------------------------------- */
static struct fb_var_screeninfo l4fb_defined = {
	.activate	= FB_ACTIVATE_NOW,
	.height		= -1,
	.width		= -1,
	.right_margin	= 32,
	.upper_margin	= 16,
	.lower_margin	= 4,
	.vsync_len	= 4,
	.vmode		= FB_VMODE_NONINTERLACED,
};

static struct fb_fix_screeninfo l4fb_fix = {
	.id	= "l4fb",
	.type	= FB_TYPE_PACKED_PIXELS,
	.accel	= FB_ACCEL_NONE,
};

static u32 pseudo_palette[17];

/* -- implementations -------------------------------------------------- */

static void vesa_setpalette(int regno, unsigned red, unsigned green,
			    unsigned blue)
{
}

static int l4fb_setcolreg(unsigned regno, unsigned red, unsigned green,
			  unsigned blue, unsigned transp,
			  struct fb_info *info)
{
	/*
	 *  Set a single color register. The values supplied are
	 *  already rounded down to the hardware's capabilities
	 *  (according to the entries in the `var' structure). Return
	 *  != 0 for invalid regno.
	 */
	
	if (regno >= info->cmap.len)
		return 1;

	if (info->var.bits_per_pixel == 8)
		vesa_setpalette(regno,red,green,blue);
	else if (regno < 16) {
		switch (info->var.bits_per_pixel) {
		case 16:
			((u32*) (info->pseudo_palette))[regno] =
				((red   >> (16 -   info->var.red.length)) <<   info->var.red.offset) |
				((green >> (16 - info->var.green.length)) << info->var.green.offset) |
				((blue  >> (16 -  info->var.blue.length)) <<  info->var.blue.offset);
			break;
		case 24:
		case 32:
			red   >>= 8;
			green >>= 8;
			blue  >>= 8;
			((u32 *)(info->pseudo_palette))[regno] =
				(red   << info->var.red.offset)   |
				(green << info->var.green.offset) |
				(blue  << info->var.blue.offset);
			break;
		}
	}

	return 0;
}

static int l4fb_pan_display(struct fb_var_screeninfo *var,
                            struct fb_info *info)
{
	return 0;
}

static void l4fb_copyarea(struct fb_info *info, const struct fb_copyarea *region)
{
	unsigned long phys_region = 0;
	cfb_copyarea(info, region);
	phys_region = (u32)__pa((volatile void *)region);
    karma_hypercall1(KARMA_MAKE_COMMAND(KARMA_DEVICE_ID(fb), karma_fb_df_update_rect), &phys_region);
    karma_hypercall0(KARMA_MAKE_COMMAND(KARMA_DEVICE_ID(fb), karma_fb_df_completion));
}

static void l4fb_fillrect(struct fb_info *info, const struct fb_fillrect *rect)
{
    unsigned long phys_region = 0;
	cfb_fillrect(info, rect);
    phys_region = (u32)__pa((volatile void *)rect);
    karma_hypercall1(KARMA_MAKE_COMMAND(KARMA_DEVICE_ID(fb), karma_fb_df_update_rect), &phys_region);
    karma_hypercall0(KARMA_MAKE_COMMAND(KARMA_DEVICE_ID(fb), karma_fb_df_completion));
}

static void l4fb_imageblit(struct fb_info *info, const struct fb_image *image)
{
    unsigned long phys_region = 0;
	cfb_imageblit(info, image);
    phys_region = (u32)__pa((volatile void *)image);
    karma_hypercall1(KARMA_MAKE_COMMAND(KARMA_DEVICE_ID(fb), karma_fb_df_update_rect), &phys_region);
    karma_hypercall0(KARMA_MAKE_COMMAND(KARMA_DEVICE_ID(fb), karma_fb_df_completion));
}

static int l4fb_open(struct fb_info *info, int user)
{
	return 0;
}

static int l4fb_release(struct fb_info *info, int user)
{
	return 0;
}

static struct fb_ops l4fb_ops = {
	.owner		    = THIS_MODULE,
	.fb_open        = l4fb_open,
	.fb_release     = l4fb_release,
	.fb_setcolreg	= l4fb_setcolreg,
	.fb_pan_display	= l4fb_pan_display,
	.fb_fillrect	= l4fb_fillrect,
	.fb_copyarea	= l4fb_copyarea,
	.fb_imageblit	= l4fb_imageblit,
};

static void __init l4fb_setup(char *options)
{
	char *this_opt, *c;

	if (!options || !*options)
		return;

	while ((this_opt = strsep(&options, ",")) != NULL) {
		if (!*this_opt) continue;

		if (!strncmp(this_opt, "refreshsleep:", 13))
			l4fb_refresh_sleep = simple_strtoul(this_opt + 13, NULL, 0);
		else if (!strncmp(this_opt, "xres:", 5))
			xres = simple_strtoul(this_opt + 5, NULL, 0);
		else if (!strncmp(this_opt, "yres:", 5))
			yres = simple_strtoul(this_opt + 5, NULL, 0);
		else if (!strncmp(this_opt, "depth:", 6))
			depth = simple_strtoul(this_opt + 6, NULL, 0);
		else if ((c = strchr(this_opt, 'x'))) {
			xres = simple_strtoul(this_opt, NULL, 0);
			yres = simple_strtoul(c + 1, NULL, 0);
			if ((c = strchr(c, '@')))
				depth = simple_strtoul(c + 1, NULL, 0);
		}
	}

	/* sanitize if needed */
	if (!xres || !yres) {
		xres = SCR_DFL_WIDTH;
		yres = SCR_DFL_HEIGHT;
	}
	if (!depth)
		depth = SCR_DFL_DEPTH;
}

/* ============================================ */

static unsigned last_rel_x, last_rel_y;

static void input_event_put(l4re_event_t *event)
{
	struct input_event *e = (struct input_event *)event;
	unsigned long phys_region = 0;

	/* Prevent input events before system is up, see comment in
	 * DOpE input function for more. */
	if (system_state != SYSTEM_RUNNING) {
		/* Serve pending redraw requests later */
		if (e->type == EV_CON && e->code == EV_CON_REDRAW)
			redraw_pending = 1;
		return;
	}

	/* console sent redraw event -- update whole screen */
	if (e->type == EV_CON && e->code == EV_CON_REDRAW) {
        struct fb_fillrect rect;
        rect.dx = 0;
        rect.dy = 0;
        rect.height = yres;
        rect.width = xres;
        phys_region = (u32)__pa((volatile void *)&rect);
        karma_hypercall1(KARMA_MAKE_COMMAND(KARMA_DEVICE_ID(fb), karma_fb_df_update_rect), &phys_region);
        karma_hypercall0(KARMA_MAKE_COMMAND(KARMA_DEVICE_ID(fb), karma_fb_df_completion));
		return;
	}

    if (abs2rel && e->type == EV_ABS) {                                                              
        unsigned tmp;
        // x and y are enough?
        if (e->code == ABS_X) {
            e->type = EV_REL;
            e->code = REL_X;
            tmp = e->value;
            e->value = e->value - last_rel_x;
            last_rel_x = tmp;
        } else if (e->code == ABS_Y) {
            e->type = EV_REL;
            e->code = REL_Y;
            tmp = e->value;
            e->value = e->value - last_rel_y;
            last_rel_y = tmp;
        }   
    }  

	if (touchscreen && e->type == EV_KEY && e->code == BTN_LEFT)
		e->code = BTN_TOUCH;

	/* The l4input library is based on Linux-2.6, so we're lucky here */
	if (e->type == EV_KEY && e->code < BTN_MISC) {
		input_event(l4input_dev_key, e->type, e->code, e->value);
		input_sync(l4input_dev_key);
	} else {
		input_event(l4input_dev_mouse, e->type, e->code, e->value);
		input_sync(l4input_dev_mouse);
	}
}

static irqreturn_t event_interrupt(int irq, void *data)
{
    //spin_lock_irqsave(&l4fb_lock, flags);
    l4re_event_t *ptr = (l4re_event_t*)ev_ptr;
    u32 counter = 0;
    while(counter < ev_elems)
    {
        if(!ptr)
            break;
        if(ptr->time)
        {
            input_event_put(ptr);
            ptr->time=0;
        }
        ptr++;
        counter++;
    }
    //spin_unlock_irqrestore(&l4fb_lock, flags);
    return IRQ_HANDLED;
}

static int l4fb_input_setup(void)
{
	unsigned int i;
	int err;

    // registriert den irq irqnum in linux und legt den handler fest
	if ((err = request_irq(irqnum, event_interrupt,
	                       0, "L4fbev", NULL))) {
		printk("%s: request_irq failed: %d\n", __func__, err);
		return err;
	}

	l4input_dev_key   = input_allocate_device();
	if (singledev)
		l4input_dev_mouse = l4input_dev_key;
	else
	  l4input_dev_mouse = input_allocate_device();
	if (!l4input_dev_key || !l4input_dev_mouse)
		return -ENOMEM;
	/* Keyboard */
	l4input_dev_key->name = singledev ? "L4input" : "L4input key";
	l4input_dev_key->phys = "l4re key";
	l4input_dev_key->uniq = singledev ? "L4input" : "L4input key";
	l4input_dev_key->id.bustype = 0;
	l4input_dev_key->id.vendor  = 0;
	l4input_dev_key->id.product = 0;
	l4input_dev_key->id.version = 0;

	/* We generate key events */
	set_bit(EV_KEY, l4input_dev_key->evbit);
	set_bit(EV_REP, l4input_dev_key->evbit);
	/* We can generate every key, do not use KEY_MAX as apps compiled
	 * against older linux/input.h might have lower values and segfault.
	 * Fun. */
	for (i = 0; i < 0x1ff; i++)
		set_bit(i, l4input_dev_key->keybit);

	if (!singledev) {
	  i = input_register_device(l4input_dev_key);
	  if (i)
		return i;
	}

	/* Mouse */
	if (!singledev) {
	  l4input_dev_mouse->name = "l4input mouse";
	  l4input_dev_mouse->phys = "l4re mouse";
	  l4input_dev_mouse->id.bustype = 0;
	  l4input_dev_mouse->id.vendor  = 0;
	  l4input_dev_mouse->id.product = 0;
	  l4input_dev_mouse->id.version = 0;
	}

	/* We generate key and relative mouse events */
	set_bit(EV_KEY, l4input_dev_mouse->evbit);
	set_bit(EV_REP, l4input_dev_mouse->evbit);
	if (!touchscreen)
	  set_bit(EV_REL, l4input_dev_mouse->evbit);
	set_bit(EV_ABS, l4input_dev_mouse->evbit);
	set_bit(EV_SYN, l4input_dev_mouse->evbit);

	/* Buttons */
	if (touchscreen)
		set_bit(BTN_TOUCH,   l4input_dev_mouse->keybit);
	else {
		set_bit(BTN_LEFT,   l4input_dev_mouse->keybit);
		set_bit(BTN_RIGHT,  l4input_dev_mouse->keybit);
		set_bit(BTN_MIDDLE, l4input_dev_mouse->keybit);
		set_bit(BTN_0,      l4input_dev_mouse->keybit);
		set_bit(BTN_1,      l4input_dev_mouse->keybit);
		set_bit(BTN_2,      l4input_dev_mouse->keybit);
		set_bit(BTN_3,      l4input_dev_mouse->keybit);
		set_bit(BTN_4,      l4input_dev_mouse->keybit);
	}

	/* Movements */
	if (!touchscreen) {
	  set_bit(REL_X,      l4input_dev_mouse->relbit);
	  set_bit(REL_Y,      l4input_dev_mouse->relbit);
	}
	set_bit(ABS_X,      l4input_dev_mouse->absbit);
	set_bit(ABS_Y,      l4input_dev_mouse->absbit);
#if 0
	/* Coordinates are 1:1 pixel in frame buffer */
	l4input_dev_mouse->absmin[ABS_X] = 0;
	l4input_dev_mouse->absmin[ABS_Y] = 0;
	l4input_dev_mouse->absmax[ABS_X] = xres;
	l4input_dev_mouse->absmax[ABS_Y] = yres;
	/* We are precise */
	l4input_dev_mouse->absfuzz[ABS_X] = 0;
	l4input_dev_mouse->absfuzz[ABS_Y] = 0;
	l4input_dev_mouse->absflat[ABS_X] = 0;
	l4input_dev_mouse->absflat[ABS_Y] = 0;
#endif
	i = input_register_device(l4input_dev_mouse);
	if (i)
		return i;

	return 0;
}

static int l4fb_con_init(struct fb_var_screeninfo *var,
                         struct fb_fix_screeninfo *fix)
{
	l4re_fb_info_t *fb_info;
    void *info_ptr, *fb_ptr;
    unsigned long size;
    unsigned long phys_addr = 0;

	printk("LXFB: Starting\n");
    size = 0;

    info_ptr = 0;
    info_ptr = (void*)__get_free_pages(GFP_ATOMIC, 0); // 1 seite
    if(!info_ptr)
    {
        printk("L4FB: allocation of 1 page failed.\n");
        return -1;
    }

    // let the vmm write the l4re_fb_info_t struct into this page...
    phys_addr = (u32)__pa(info_ptr);
    karma_hypercall1(KARMA_MAKE_COMMAND(KARMA_DEVICE_ID(fb), karma_fb_df_get_info), &phys_addr);

    fb_info = (l4re_fb_info_t*)info_ptr;

    printk("LXFB: Got info from vmm. x_res = %d, y_res = %d\n",
        fb_info->x_res, fb_info->y_res);

    // get to know size of framebuffer from vmm
    karma_hypercall1(KARMA_MAKE_COMMAND(KARMA_DEVICE_ID(fb), karma_fb_df_get_ds_size), &size);
    karma_hypercall1(KARMA_MAKE_COMMAND(KARMA_DEVICE_ID(fb), karma_fb_df_get_ds_addr), (unsigned long *)&fb_ptr);
    printk("L4FB: got framebuffer from vmm: size = %ldbytes addr=%p\n",
        size, fb_ptr);

    request_mem_region((int)fb_ptr, size, "l4fb");
    
	fix->smem_start = (int)fb_ptr;
    fb_ptr = ioremap((int)fb_ptr, size);
    if(!fb_ptr)
    {
        printk("L4FB: Error ioremap\n");
        return -1;
    }
    printk("L4FB: fb memory at v: %p p: %p\n",
        fb_ptr, (void*)__pa(fb_ptr));
    fix->mmio_start = (int)fb_ptr;
    fix->mmio_len = size;

    // get event buffer!
    barrier();
    karma_hypercall1(KARMA_MAKE_COMMAND(KARMA_DEVICE_ID(fb), karma_fb_df_get_ev_ds_size), &ev_size);
    barrier();
    karma_hypercall1(KARMA_MAKE_COMMAND(KARMA_DEVICE_ID(fb), karma_fb_df_get_ev_ds_addr), (unsigned long *)&ev_ptr);
    printk("L4FB: got event buffer from vmm: size = %ldbytes, addr=%p\n",
        ev_size, ev_ptr);
    ev_elems = ev_size/sizeof(l4re_event_t);
    printk("L4FB: event buffer has %d possible entries (entry=%dbytes).\n",
        ev_elems, sizeof(l4re_event_t));

    request_mem_region((int)ev_ptr, ev_size, "l4fb_event");
    ev_ptr = ioremap((int)ev_ptr, ev_size);
    if(!ev_ptr)
    {
        printk("L4FB: Error ioremap event buffer(size=%ld,addr=%p.\n",
            ev_size, ev_ptr);
        return -1;
    }
    printk("L4FB: event buffer at v: %p p: %p\n",
        ev_ptr, (void*)__pa(ev_ptr));

	var->xres           = fb_info->x_res;
	var->yres           = fb_info->y_res;
	var->bits_per_pixel = fb_info->bits_per_pixel;

	/* The console expects the real screen resolution, not the virtual */
	fix->line_length    = var->xres * fb_info->bytes_per_pixel;
    fix->smem_len       = size;
	fix->visual         = FB_VISUAL_TRUECOLOR;

	/* We cannot really set (smaller would work) screen parameters
	 * when using con */
	xres  = var->xres;
	yres  = var->yres;
	if (var->bits_per_pixel == 15)
		var->bits_per_pixel = 16;
	depth = var->bits_per_pixel;

	var->red.offset = fb_info->red_shift;
	var->red.length = fb_info->red_size;
	var->green.offset = fb_info->green_shift;
	var->green.length = fb_info->green_size;
	var->blue.offset = fb_info->blue_shift;
	var->blue.length = fb_info->blue_size;

	printk("l4fb:con: %dx%d@%d %dbypp, size: %d\n",
	           var->xres, var->yres, var->bits_per_pixel,
	           fb_info->bytes_per_pixel, fix->smem_len);
	printk("l4fb:con %d:%d:%d %d:%d:%d linelen=%d visual=%d\n",
	           var->red.length, var->green.length, var->blue.length,
	           var->red.offset, var->green.offset, var->blue.offset,
	           fix->line_length, fix->visual);

    __free_page(info_ptr);

	return l4fb_input_setup();
}

/* ============================================ */

static void l4fb_shutdown(struct fb_fix_screeninfo *fix)
{
}

static int __init l4fb_probe(struct platform_device *dev)
{
	struct fb_info *info;
	int video_cmap_len;
	int ret = -ENOMEM;

	if (disable)
		return -ENODEV;

	/* Process module parameters */
	if (refreshsleep >= 0)
		l4fb_refresh_sleep = refreshsleep;

//	l4fb_update_rect = l4fb_l4re_update_rect;

	ret = l4fb_con_init(&l4fb_defined, &l4fb_fix);
	if (ret) {
		//LOG_printf("init error %d\n", ret);
		return ret;
	}

	info = framebuffer_alloc(0, &dev->dev);
	if (!info)
		goto failed_framebuffer_alloc;

	info->screen_base = (void *)l4fb_fix.mmio_start;
	if (!info->screen_base) {
		printk(KERN_ERR "l4fb: abort, graphic system could not be initialized.\n");
		ret = -EIO;
		goto failed_after_framebuffer_alloc;
	}

	printk(KERN_INFO "l4fb: Framebuffer at 0x%p, size %dk\n",
	       info->screen_base, l4fb_fix.smem_len >> 10);
	printk(KERN_INFO "l4fb: mode is %dx%dx%d, linelength=%d, pages=%d\n",
	       l4fb_defined.xres, l4fb_defined.yres, l4fb_defined.bits_per_pixel, l4fb_fix.line_length, screen_info.pages);

	l4fb_defined.xres_virtual = l4fb_defined.xres;
	l4fb_defined.yres_virtual = l4fb_defined.yres;

	/* some dummy values for timing to make fbset happy */
	l4fb_defined.pixclock     = 10000000 / l4fb_defined.xres * 1000 / l4fb_defined.yres;
	l4fb_defined.left_margin  = (l4fb_defined.xres / 8) & 0xf8;
	l4fb_defined.hsync_len    = (l4fb_defined.xres / 8) & 0xf8;

	l4fb_defined.transp.length = 0;
	l4fb_defined.transp.offset = 0;

	printk(KERN_INFO "l4fb: directcolor: "
	       "size=%d:%d:%d:%d, shift=%d:%d:%d:%d\n",
	       0,
	       l4fb_defined.red.length,
	       l4fb_defined.green.length,
	       l4fb_defined.blue.length,
	       0,
	       l4fb_defined.red.offset,
	       l4fb_defined.green.offset,
	       l4fb_defined.blue.offset);
	video_cmap_len = 16;

	l4fb_fix.ypanstep  = 0;
	l4fb_fix.ywrapstep = 0;

	info->fbops = &l4fb_ops;
	info->var   = l4fb_defined;
	info->fix   = l4fb_fix;
	info->pseudo_palette = pseudo_palette;
	info->flags = FBINFO_FLAG_DEFAULT;

	ret = fb_alloc_cmap(&info->cmap, video_cmap_len, 0);
	if (ret < 0)
		goto failed_after_framebuffer_alloc;

	if (register_framebuffer(info) < 0) {
		ret = -EINVAL;
		goto failed_after_fb_alloc_cmap;
	}
	dev_set_drvdata(&dev->dev, info);

	//atexit(l4fb_shutdown_atexit);

	printk(KERN_INFO "l4fb%d: %s L4 frame buffer device (refresh: %ums)\n",
	       info->node, info->fix.id, l4fb_refresh_sleep);

	return 0;

failed_after_fb_alloc_cmap:
	fb_dealloc_cmap(&info->cmap);

failed_after_framebuffer_alloc:
	framebuffer_release(info);

failed_framebuffer_alloc:
	//deinit_con();
    printk("L4FB: something went wrong in %s\n", __func__);
	return ret;
}

static int l4fb_remove(struct platform_device *device)
{
	struct fb_info *info = platform_get_drvdata(device);

	if (info) {
		unregister_framebuffer(info);
		fb_dealloc_cmap(&info->cmap);
		framebuffer_release(info);

		l4fb_shutdown(&info->fix);
	}
	return 0;
}

static struct platform_driver l4fb_driver = {
	.probe   = l4fb_probe,
	.remove  = l4fb_remove,
	.driver  = {
		.name = "l4fb",
	},
};

static struct platform_device l4fb_device = {
	.name = "l4fb",
};

static int __init l4fb_init(void)
{
	int ret;
	char *option = NULL;

	/* Parse option string */
	fb_get_options("l4fb", &option);
	l4fb_setup(option);

	ret = platform_driver_register(&l4fb_driver);
	if (!ret) {
		ret = platform_device_register(&l4fb_device);
		if (ret)
			platform_driver_unregister(&l4fb_driver);
	}
#ifdef CONFIG_L4_FB_DRIVER_DBG
	debugfs_dir = debugfs_create_dir("l4fb", NULL);
	if (!IS_ERR(debugfs_dir)) {
		debugfs_unmaps  = debugfs_create_u32("unmaps", S_IRUGO,
		                                     debugfs_dir, &stats_unmaps);
		debugfs_updates = debugfs_create_u32("updates", S_IRUGO,
		                                     debugfs_dir, &stats_updates);
	}
#endif
	return ret;
}
module_init(l4fb_init);

static void __exit l4fb_exit(void)
{
#ifdef CONFIG_L4_FB_DRIVER_DBG
	debugfs_remove(debugfs_unmaps);
	debugfs_remove(debugfs_updates);
	debugfs_remove(debugfs_dir);
#endif

	//kfree(l4fb_unmap_info.flexpages);
	input_unregister_device(l4input_dev_key);
	if (!singledev)
		input_unregister_device(l4input_dev_mouse);
	platform_device_unregister(&l4fb_device);
	platform_driver_unregister(&l4fb_driver);
}
module_exit(l4fb_exit);

MODULE_AUTHOR("Adam Lackorzynski <adam@os.inf.tu-dresden.de>");
MODULE_DESCRIPTION("Frame buffer driver for L4 con and DOpE");
MODULE_LICENSE("GPL");


module_param(refreshsleep, int, 0444);
MODULE_PARM_DESC(refreshsleep, "Sleep between frame buffer refreshs in ms");
module_param(disable, bool, 0);
MODULE_PARM_DESC(disable, "Disable driver");
module_param(touchscreen, bool, 0);
MODULE_PARM_DESC(touchscreen, "Be a touchscreen");
module_param(singledev, bool, 0);
MODULE_PARM_DESC(singledev, "Expose only one input device");
module_param(abs2rel, bool, 0);
MODULE_PARM_DESC(abs2rel, "Convert absolute events to relative ones");
