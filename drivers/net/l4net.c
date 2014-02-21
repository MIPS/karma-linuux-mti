/*
 *
 * Interface to the Ankh network multiplexer. To be used in a VM established by the Karma VMM.
 *
 * (c) 2010 Steffen Liebergeld <steffen@sec.t-labs.tu-berlin.de>
 *
 * Based on the Ore network driver stub, written by Bjoern Doebel.
 * Ore network driver stub.
 */

#include <linux/etherdevice.h>

#include <asm/l4.h>

static void l4net_write(unsigned long addr, unsigned long val){
	KARMA_WRITE_IMPL(net, addr, val);
}

static unsigned long l4net_read(unsigned long addr){
	KARMA_READ_IMPL(net, addr);
}

MODULE_AUTHOR("Steffen Liebergeld <steffen@sec.t-labs.tu-berlin.de>");
MODULE_DESCRIPTION("L4net stub driver");
MODULE_LICENSE("GPL");

struct ankh_session
{
	unsigned char   mac[6];
	unsigned int    mtu;
	unsigned long   num_rx, rx_dropped, rx_bytes, num_tx, tx_dropped, tx_bytes;
};

char *_send_buf, *_recv_buf;

static int  l4x_net_numdevs = 1;
#define MAX_NETINST 6
static char *l4x_net_instances[MAX_NETINST] = { "Ankh:eth0", 0, 0, 0, 0, 0 };
static LIST_HEAD(l4x_net_netdevices);

struct l4x_net_priv {
	struct net_device_stats    net_stats;

	unsigned char              *pkt_buffer;
	unsigned long              pkt_size;

	struct irq_chip            *previous_interrupt_type;
};

struct l4x_net_netdev {
	struct list_head  list;
	struct net_device *dev;
};

static int l4x_net_xmit_frame(struct sk_buff *skb, struct net_device *netdev)
{
	struct l4x_net_priv *priv = netdev_priv(netdev);
	short length = skb->len;

	memcpy(_send_buf, skb->data, length);
	l4net_write(L4_NET_SEND_PACKET, length);

	dev_kfree_skb(skb);

	netdev->trans_start = jiffies;
	priv->net_stats.tx_packets++;
	priv->net_stats.tx_bytes += skb->len;

	return 0;
}

static struct net_device_stats *l4x_net_get_stats(struct net_device *netdev)
{
	struct l4x_net_priv *priv = netdev_priv(netdev);
	return &priv->net_stats;
}

static void l4x_net_tx_timeout(struct net_device *netdev)
{
	printk("L4net: %s\n", __func__);
}

/*
 * Interrupt.
 */
static irqreturn_t l4x_net_interrupt(int irq, void *dev_id)
{
	struct net_device *netdev = dev_id;
	struct l4x_net_priv *priv = netdev_priv(netdev);
	struct sk_buff *skb;
	long psize;

	psize = l4net_read(L4_NET_RECV_SIZE);
    
	skb = dev_alloc_skb(psize);
	if (likely(skb)) {
		skb->dev = netdev;
		memcpy(skb_put(skb, psize), _recv_buf, psize);
		memset(_recv_buf, 0, psize);

		skb->protocol = eth_type_trans(skb, netdev);
		netif_rx(skb);

		netdev->last_rx = jiffies;
		priv->net_stats.rx_bytes += skb->len;
		priv->net_stats.rx_packets++;
	} else {
		printk(KERN_WARNING "%s: dropping packet.\n", netdev->name);
		priv->net_stats.rx_dropped++;
	}

	return IRQ_HANDLED;
}

/* ----- */
static unsigned int l4x_net_irq_startup(unsigned int irq)
{
	printk("L4net: %s\n", __func__);
	return 1;
}

static void l4x_net_irq_dummy_void(unsigned int irq)
{
}

struct irq_chip l4x_net_irq_type = {
	.name		= "L4net IRQ",
	.startup	= l4x_net_irq_startup,
	.shutdown	= l4x_net_irq_dummy_void,
	.enable		= l4x_net_irq_dummy_void,
	.disable	= l4x_net_irq_dummy_void,
	.mask		= l4x_net_irq_dummy_void,
	.unmask		= l4x_net_irq_dummy_void,
	.ack		= l4x_net_irq_dummy_void,
	.end		= l4x_net_irq_dummy_void,
};
/* ----- */

static int l4x_net_open(struct net_device *netdev)
{
	struct l4x_net_priv *priv = netdev_priv(netdev);
	int err = -ENODEV;

	netif_carrier_off(netdev);

	if ((err = request_irq(netdev->irq, l4x_net_interrupt,
	                       IRQF_SAMPLE_RANDOM | IRQF_SHARED,
	                       netdev->name, netdev))) {
		printk("L4net: %s: request_irq(%d, ...) failed: %d\n",
		       netdev->name, netdev->irq, err);
		goto err_out_kfree;
	}

	//printk("L4net: DEBUG(%s) requested irq %d\n", __func__, netdev->irq);

	netif_carrier_on(netdev);
	netif_wake_queue(netdev);

	printk("L4net: %s interface up.\n", netdev->name);

	return 0;

err_out_kfree:
	kfree(priv->pkt_buffer);

//err_out_close:
	irq_desc[netdev->irq].chip = priv->previous_interrupt_type;
	return err;
}

static int l4x_net_close(struct net_device *netdev)
{
	struct l4x_net_priv *priv = netdev_priv(netdev);


	free_irq(netdev->irq, netdev);
	//irq_desc[netdev->irq].chip = priv->previous_interrupt_type;
	netif_stop_queue(netdev);
	netif_carrier_off(netdev);

	kfree(priv->pkt_buffer);

	return 0;
}

/*
 * Split 'inst:foo' into separate strings 'inst' and 'foo'
 */
static int l4x_net_parse_instance(int id, char *inst, int instsize,
                                          char *dev, int devsize)
{
	char *string = l4x_net_instances[id];
	char *s2;

	s2 = strsep(&string, ":");
	if (!s2 || !string)
		return -1;
	strcpy(inst, s2);
	strcpy(dev,  string);
	inst[instsize - 1] = 0;
	dev[devsize - 1] = 0;

	return 0;
}

static const struct net_device_ops l4xnet_netdev_ops = {
	.ndo_open = l4x_net_open,
	.ndo_stop = l4x_net_close,
	.ndo_start_xmit = l4x_net_xmit_frame,
	.ndo_get_stats  = l4x_net_get_stats,
	.ndo_tx_timeout = l4x_net_tx_timeout,
};

/* Initialize one virtual interface. */
static int __init l4x_net_init_device(char *oreinst, char *devname)
{
	struct l4x_net_priv *priv;
	struct net_device *dev = NULL;
	struct l4x_net_netdev *nd = NULL;
	//struct ankh_session *ankh;
	int size;
	int err = -ENODEV;
//	DECLARE_MAC_BUF(macstring);

	struct ankh_session session;

	session.mtu = 0;

	l4net_write(L4_NET_GET_INFO, (u32)virt_to_phys(&session));

	if(session.mtu == 0)
	{
		printk("L4net: Not supported by Karma. (May need to give it the --net command line parameter?\n");
		return 1;
	}

//	printk("L4net: got info from Karma: %s\n",
//	  print_mac(macstring, &(session.mac)));

	if (!(dev = alloc_etherdev(sizeof(struct l4x_net_priv))))
	{
		printk("L4net: alloc_etherdev failed\n");
		return -ENOMEM;
	}

	memcpy((void*)dev->dev_addr, &(session.mac), sizeof(unsigned char)*6);

	_send_buf = (char*)l4net_read(L4_NET_SEND_BUF_ADDR);
	printk("L4net: send buffer at address %p\n", _send_buf);
	_recv_buf = (char*)l4net_read(L4_NET_RECV_BUF_ADDR);
	printk("L4net: recv buffer at address %p\n", _recv_buf);
	size = l4net_read(L4_NET_GET_BUF_SIZE);
	printk("L4net: buffer size is %d bytes\n", size);

	request_mem_region((resource_size_t)_send_buf, size, "l4net send");
	_send_buf = ioremap((resource_size_t)_send_buf, size);
	printk("L4net: ioremap'ed _send_buf to %p\n", _send_buf); 

	request_mem_region((resource_size_t)_recv_buf, size, "l4net recv");
	_recv_buf = ioremap((resource_size_t)_recv_buf, size);
	printk("L4net: ioremap'ed _recv_buf to %p\n", _recv_buf); 

	dev->netdev_ops = &l4xnet_netdev_ops;
	priv = netdev_priv(dev);
	dev->irq = karma_irq_net;

	dev->base_addr = (resource_size_t)_recv_buf;

	if ((err = register_netdev(dev))) {
		printk("L4net: Cannot register net device, aborting.\n");
		goto err_out_free_dev;
	}

	nd = kmalloc(sizeof(struct l4x_net_netdev), GFP_KERNEL);
	if (!nd) {
		printk("Out of memory.\n");
		return -1;
	}
	nd->dev = dev;
	list_add(&nd->list, &l4x_net_netdevices);

	printk(KERN_INFO "L4Net: %s Ankh card found  IRQ %d\n",
	                 dev->name, //print_mac(macstring, dev->dev_addr),
	                 dev->irq);

	return 0;

err_out_free_dev:
	free_netdev(dev);

	return err;
}

static int __init l4x_net_init(void)
{
	int i = 0;
	int num_init = 0;

	printk("L4net: Initializing\n");

	printk("L4net: Creating %d Ankh device(s).\n", l4x_net_numdevs);

	for (i = 0; i < l4x_net_numdevs; i++) {
		char instbuf[16], devbuf[16];
		int ret = l4x_net_parse_instance(i, instbuf, sizeof(instbuf),
		                                    devbuf, sizeof(devbuf));
		if (!ret) {
			printk("L4net: Opening device %s at Ankh instance %s\n",
			           devbuf, instbuf);
			ret = l4x_net_init_device(instbuf, devbuf);
			if (!ret)
				num_init++;
		}
		else
			printk("L4net: Invalid device string: %s\n",
			           l4x_net_instances[i]);
	}

	return num_init > 0 ? 0 : -1;
}

static void __exit l4x_net_exit(void)
{
	struct list_head *p, *n;
	list_for_each_safe(p, n, &l4x_net_netdevices) {
		struct l4x_net_netdev *nd
		  = list_entry(p, struct l4x_net_netdev, list);
		struct net_device *dev = nd->dev;
		unregister_netdev(dev);
		free_netdev(dev);
		list_del(p);
		kfree(nd);
	}
}

module_init(l4x_net_init);
module_exit(l4x_net_exit);

module_param_array_named(instances, l4x_net_instances, charp, &l4x_net_numdevs, 0);
MODULE_PARM_DESC(netinstances, "Ankh instances to connect to");

