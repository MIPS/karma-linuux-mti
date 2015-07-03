/*
 *  drivers/tty/serial/karma_ser.c
 *
 *  Serial driver for Linux running in a VM established by the Karma VMM.
 *
 *  (c) 2012 Steffen Liebergeld
 *
 *  Based on drivers/tty/serial/l4ser.c by Torsten Frenzel and Adam Lackorzynski.
 *
 */
#if defined(CONFIG_L4_SERIAL_CONSOLE) && defined(CONFIG_MAGIC_SYSRQ)
#define SUPPORT_SYSRQ
#endif

#include <linux/module.h>
#include <linux/ioport.h>
#include <linux/init.h>
#include <linux/irq.h>
#include <linux/console.h>
#include <linux/sysrq.h>
#include <linux/device.h>
#include <linux/tty.h>
#include <linux/tty_flip.h>
#include <linux/serial_core.h>
#include <linux/serial.h>
#include <asm/l4.h>

/* This is the same major as the sa1100 one */
#define SERIAL_L4SER_MAJOR	204
#define MINOR_START		5

#define PORT0_NAME              "log"
#define NR_ADDITIONAL_PORTS	3

static int ports_to_add_pos;

#define L4SER_REQUESTED		1
#define L4SER_NOECHO		0x8000

static char _shared_mem[1024] __attribute__((aligned(4096)));

static void karma_ser_write(unsigned long opcode, unsigned long val)
{
	karma_hypercall1(KARMA_MAKE_COMMAND(KARMA_DEVICE_ID(ser), opcode), &val);
}


static int karma_ser_read(unsigned long opcode)
{
	KARMA_READ_IMPL(ser, opcode);
}

struct l4ser_uart_port {
	struct uart_port port;
	char cap_name[20];
	int flags, inited;
};

static struct l4ser_uart_port l4ser_port[1 + NR_ADDITIONAL_PORTS];

static void l4ser_stop_tx(struct uart_port *port)
{
}

static void l4ser_stop_rx(struct uart_port *port)
{
}

static void l4ser_enable_ms(struct uart_port *port)
{
}

static int
l4ser_getchar(struct l4ser_uart_port *l4port)
{
	int c;

	c = (int)karma_ser_read(karma_ser_df_read);
	return c;
}

static void
l4ser_rx_chars(struct uart_port *port)
{
	struct l4ser_uart_port *l4port = (struct l4ser_uart_port *)port;
	struct tty_port *tty_port = &port->state->port;
	unsigned int flg;
	int ch;

	while (1)  {
		ch = l4ser_getchar(l4port);
		if (ch == -1) // -1 denotes end of line
			break;
		port->icount.rx++;

		flg = TTY_NORMAL;

		// ^ can be used as a sysrq starter
#if 0
			if (ch == '^') {
				if (port->sysrq)
					port->sysrq = 0;
				else {
					port->icount.brk++;
					uart_handle_break(port);
					continue;
				}
			}
#endif

		if (uart_handle_sysrq_char(port, ch))
			continue;

		tty_insert_flip_char(tty_port, ch, flg);
	}
	tty_flip_buffer_push(tty_port);
	return;
}

/*
 * Interrupts are disabled on entering
 */
static void
l4ser_console_write(struct console *co, const char *s, unsigned int count)
{
	if (count > sizeof(_shared_mem))
		count = sizeof(_shared_mem);

	memcpy(_shared_mem, s, count);
	karma_ser_write(karma_ser_df_writeln, count);
}

static void l4ser_tx_chars(struct uart_port *port)
{
	struct circ_buf *xmit = &port->state->xmit;
	int c;

	if (port->x_char) {
		l4ser_console_write(NULL, &port->x_char, 1);
		port->icount.tx++;
		port->x_char = 0;
		return;
	}

	while (!uart_circ_empty(xmit)) {
		c = CIRC_CNT_TO_END(xmit->head, xmit->tail, UART_XMIT_SIZE);
		l4ser_console_write(NULL, &xmit->buf[xmit->tail], c);
		xmit->tail = (xmit->tail + c) & (UART_XMIT_SIZE - 1);
		port->icount.tx += c;
	}
}

static void l4ser_start_tx(struct uart_port *port)
{
	l4ser_tx_chars(port);
}

static irqreturn_t l4ser_int(int irq, void *dev_id)
{
	struct uart_port *sport = dev_id;

	l4ser_rx_chars(sport);

	return IRQ_HANDLED;
}

static unsigned int l4ser_tx_empty(struct uart_port *port)
{
	return TIOCSER_TEMT;
}

static unsigned int l4ser_get_mctrl(struct uart_port *port)
{
	return 0;
}

static void l4ser_set_mctrl(struct uart_port *port, unsigned int mctrl)
{
}

static void l4ser_break_ctl(struct uart_port *port, int break_state)
{
}

static int l4ser_startup(struct uart_port *port)
{
	int ret;
#if 0 // XXX tty_set_termios is no longer available
	struct l4ser_uart_port *l4port = &l4ser_port[port->line];

	if (l4port->flags & L4SER_NOECHO) {
		struct ktermios new_termios;
		new_termios = l4port->port.state->port.tty->termios;
		new_termios.c_lflag &= ~ECHO;
		tty_set_termios(l4port->port.state->port.tty, &new_termios);
	}
#endif

	if (port->irq) {
		ret = request_irq(port->irq, l4ser_int, 0, "karma_uart", port);
		if (ret)
			return ret;

		l4ser_rx_chars(port);
	}

	return 0;
}

static void l4ser_shutdown(struct uart_port *port)
{
	if (port->irq)
		free_irq(port->irq, port);
}

static void l4ser_set_termios(struct uart_port *port, struct ktermios *termios,
                              struct ktermios *old)
{
}

static const char *l4ser_type(struct uart_port *port)
{
	return port->type == PORT_SA1100 ? "L4" : NULL;
}


static int l4ser_request_port(struct uart_port *port)
{
	return 0;
}

static void l4ser_release_port(struct uart_port *port)
{
}

static void l4ser_config_port(struct uart_port *port, int flags)
{
	if (flags & UART_CONFIG_TYPE)
		port->type = PORT_SA1100;
}

static int
l4ser_verify_port(struct uart_port *port, struct serial_struct *ser)
{
	return 0;
}

static struct uart_ops l4ser_pops = {
	.tx_empty	= l4ser_tx_empty,
	.set_mctrl	= l4ser_set_mctrl,
	.get_mctrl	= l4ser_get_mctrl,
	.stop_tx	= l4ser_stop_tx,
	.start_tx	= l4ser_start_tx,
	.stop_rx	= l4ser_stop_rx,
	.enable_ms	= l4ser_enable_ms,
	.break_ctl	= l4ser_break_ctl,
	.startup	= l4ser_startup,
	.shutdown	= l4ser_shutdown,
	.set_termios	= l4ser_set_termios,
	.type		= l4ser_type,
	.release_port	= l4ser_release_port,
	.request_port	= l4ser_request_port,
	.config_port	= l4ser_config_port,
	.verify_port	= l4ser_verify_port,
};

static int __init l4ser_init_port(int num, const char *name)
{
	static int first = 1;
	if (!first)
		return 0;
	first = 0;

	l4ser_port[num].port.uartclk   = 3686400;
	l4ser_port[num].port.ops       = &l4ser_pops;
	l4ser_port[num].port.fifosize  = 8;
	l4ser_port[num].port.line      = num;
	l4ser_port[num].port.iotype    = UPIO_MEM;
	l4ser_port[num].port.membase   = (void *)1;
	l4ser_port[num].port.mapbase   = 1;
	l4ser_port[num].port.flags     = UPF_BOOT_AUTOCONF;
#if CONFIG_MIPS_PARAVIRT
	l4ser_port[num].port.irq       = karma_irq_ser + MIPS_IRQ_PCID + 1; // fixed...
#else
	l4ser_port[num].port.irq       = karma_irq_ser; // fixed...
#endif

	karma_ser_write(karma_ser_df_init, (unsigned long)&_shared_mem);

	return 0;
}


static int __init
l4ser_console_setup(struct console *co, char *options)
{
	struct uart_port *up;

	if (co->index >= 1 + NR_ADDITIONAL_PORTS)
		co->index = 0;
	up = &l4ser_port[co->index].port;
	if (!up)
		return -ENODEV;

	return uart_set_options(up, co, 115200, 'n', 8, 'n');
}


static struct uart_driver l4ser_reg;
static struct console l4ser_console = {
	.name		= "ttyLv",
	.write		= l4ser_console_write,
	.device		= uart_console_device,
	.setup		= l4ser_console_setup,
	.flags		= CON_PRINTBUFFER,
	.index		= -1,
	.data		= &l4ser_reg,
};

static int __init l4ser_rs_console_init(void)
{
	if (l4ser_init_port(0, PORT0_NAME))
		return -ENODEV;
	register_console(&l4ser_console);
	return 0;
}
console_initcall(l4ser_rs_console_init);

#define L4SER_CONSOLE	&l4ser_console

static struct uart_driver l4ser_reg = {
	.owner			= THIS_MODULE,
	.driver_name		= "ttyLv",
	.dev_name		= "ttyLv",
	.major			= SERIAL_L4SER_MAJOR,
	.minor			= MINOR_START,
	.nr			= 1 + NR_ADDITIONAL_PORTS,
	.cons			= L4SER_CONSOLE,
};

static int __init l4ser_serial_init(void)
{
	int ret, i;

	printk(KERN_INFO "[KARMA SER] Karma serial driver\n");

	if (l4ser_init_port(0, PORT0_NAME))
		return -ENODEV;

	ret = uart_register_driver(&l4ser_reg);
	if (ret)
		return ret;

	ret = uart_add_one_port(&l4ser_reg, &l4ser_port[0].port);

	for (i = 1; i <= NR_ADDITIONAL_PORTS; ++i) {
		if (!(l4ser_port[i].flags & L4SER_REQUESTED)) {			// add dummy port
			continue;
		}
		if (l4ser_init_port(i, l4ser_port[i].cap_name)) {
			printk(KERN_WARNING "[KARMA SER] Failed to initialize additional port '%s'.\n", l4ser_port[i].cap_name);
			continue;
		}
		ret = uart_add_one_port(&l4ser_reg, &l4ser_port[i].port);
	}
	return 0;
}

static void __exit l4ser_serial_exit(void)
{
	int i;
	uart_remove_one_port(&l4ser_reg, &l4ser_port[0].port);
	for (i = 1; i <= NR_ADDITIONAL_PORTS; ++i)
		uart_remove_one_port(&l4ser_reg, &l4ser_port[i].port);
	uart_unregister_driver(&l4ser_reg);
}

module_init(l4ser_serial_init);
module_exit(l4ser_serial_exit);

/* This function is called much earlier than module_init, esp. there's
 * no kmalloc available */
static int l4ser_setup(const char *val, struct kernel_param *kp)
{
	struct l4ser_uart_port *next_port;
	const char *opts = strchr(val, ',');
	if (ports_to_add_pos >= NR_ADDITIONAL_PORTS) {
		printk(KERN_WARNING "[KARMA SER] Too many additional ports specified, ignoring \"%s\"\n", val);
		return 1;
	}
	next_port = &l4ser_port[ports_to_add_pos+1];
	next_port->flags |= L4SER_REQUESTED;
	if (opts && !strcmp(opts+1, "noecho")) {
		// really process options starting at opts[1]
		next_port->flags |= L4SER_NOECHO;
	}
	strlcpy(next_port->cap_name, val, sizeof(next_port->cap_name));
	if (opts && ((opts - val) < sizeof(next_port->cap_name)))
		next_port->cap_name[opts-val] = 0;
	ports_to_add_pos++;
	return 0;
}

module_param_call(add, l4ser_setup, NULL, NULL, 0200);
MODULE_PARM_DESC(add, "Use karma_ser.add=name to add an another port, name queried in cap environment");

MODULE_AUTHOR("Steffen Liebergeld <steffen@sec.t-labs,tu-berlin.de>");
MODULE_DESCRIPTION("Karma serial driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS_CHARDEV_MAJOR(SERIAL_L4SER_MAJOR);
