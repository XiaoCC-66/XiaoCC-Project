#include <linux/module.h>
#include <linux/version.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/sched.h>
#include <linux/pm.h>
#include <linux/sysctl.h>
#include <linux/proc_fs.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/input.h>
#include <linux/irq.h>


#include <asm/gpio.h>
#include <asm/io.h>

#include <asm/uaccess.h>	

/*硬件连接:
 *EINT0 :GPF0	EINT2 :GPF2
 *EINT11:GPG3	EINT19:GPG11
 */

static struct class *buttonsdrv_class;
static struct class_device	*buttonsdrv_class_dev;

static struct timer_list buttons_timer;

volatile unsigned long *gpfcon = NULL;	
volatile unsigned long *gpfdat = NULL;
volatile unsigned long *gpgcon = NULL;
volatile unsigned long *gpgdat = NULL;


static DECLARE_WAIT_QUEUE_HEAD(button_waitq);

static int major;
static volatile int ev_press = 0;
static unsigned int key_val;

struct pin_desc{
	unsigned int pin;
	unsigned int key_val;
};

static struct pin_desc pins_desc[4] =
{
	{S3C2410_GPF(0), 0x01},
	{S3C2410_GPF(2), 0x02},
	{S3C2410_GPG(3), 0x03},
	{S3C2410_GPG(11), 0x04},
};	

static struct pin_desc *irq_pd;

static irqreturn_t buttons_irq(int irq, void * dev_id)
{
	struct pin_desc *pindesc = (struct pin_desc *)dev_id;
	
	irq_pd = (struct pin_desc *)dev_id;
	mod_timer(&buttons_timer, jiffies+HZ/100);
	
         
	return IRQ_RETVAL(IRQ_HANDLED);
}


static int buttons_read(struct file *file, char __user *buf, size_t size, loff_t *ppos)	
{
	if (size != 1)
	return -EINVAL;

	wait_event_interruptible(button_waitq, ev_press);

	copy_to_user(buf, &key_val, 1);
	ev_press = 0;
	
	return 1;
}

int buttons_close(struct inode *inode, struct file *file)
{
	free_irq(IRQ_EINT0,  &pins_desc[0]);
	free_irq(IRQ_EINT2,  &pins_desc[1]);
	free_irq(IRQ_EINT11, &pins_desc[2]);
	free_irq(IRQ_EINT19, &pins_desc[3]);

	return 0;
}

static int buttons_open(struct inode *inode, struct file *file)
{
	request_irq(IRQ_EINT0,  buttons_irq, IRQF_TRIGGER_FALLING, "KEY0", &pins_desc[0]);	
	request_irq(IRQ_EINT2,  buttons_irq, IRQF_TRIGGER_FALLING, "KEY1", &pins_desc[1]);
	request_irq(IRQ_EINT11, buttons_irq, IRQF_TRIGGER_FALLING, "KEY2", &pins_desc[2]);
	request_irq(IRQ_EINT19, buttons_irq, IRQF_TRIGGER_FALLING, "KEY3", &pins_desc[3]);

	return 0;
}

static struct file_operations buttons_fops={
	.owner   = THIS_MODULE,
	.open    = buttons_open,
	.read    = buttons_read,
	.release = buttons_close,
};

static void buttons_timer_function(unsigned long data)
{
	struct pin_desc * pindesc = irq_pd;
	unsigned int pinval;

	if (!pindesc)
		return;
	
	pinval = s3c2410_gpio_getpin(pindesc->pin);

	if (pinval)
	{
		/* 松开 */
		key_val = 0x80 | pindesc->key_val;
	}
	else
	{
		/* 按下 */
		key_val = pindesc->key_val;
	}

    ev_press = 1;                  /* 表示中断发生了 */
    wake_up_interruptible(&button_waitq);   /* 唤醒休眠的进程 */
}

static int buttons_init(void)
{
	init_timer(&buttons_timer);
	buttons_timer.function = buttons_timer_function;
	add_timer(&buttons_timer);

	major = register_chrdev(0, "buttons", &buttons_fops);
	buttonsdrv_class = class_create(THIS_MODULE, "buttons");
	buttonsdrv_class_dev = device_create(buttonsdrv_class, NULL, MKDEV(major, 0), NULL, "buttons");	

	gpfcon = (volatile unsigned long *)ioremap(0x56000050, 16);
	gpfdat = gpfcon + 1;

	gpgcon = (volatile unsigned long *)ioremap(0x56000060, 16);
	gpgdat = gpgcon + 1;

	return 0;
}

static void buttons_exit(void)
{
	unregister_chrdev(major, "buttons");
	device_destroy(buttonsdrv_class,MKDEV(major, 0));	
	class_destroy(buttonsdrv_class);
	iounmap(gpfcon);
	iounmap(gpgcon);
	
}

module_init(buttons_init);
module_exit(buttons_exit);

MODULE_LICENSE("GPL");

