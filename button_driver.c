#include<linux/module.h>
#include<linux/init.h>

#include<linux/cdev.h>
#include<linux/device.h>
#include<linux/fs.h>
#include<linux/semaphore.h>
#include<linux/sched.h>
#include<linux/io.h>
#include<linux/interrupt.h>
#include<linux/uaccess.h>
#include<linux/wait.h>
MODULE_LICENSE("GPL");

/*调试信息开关*/
#define DEBUG_ABLE 
#ifdef DEBUG_ABLE
#define DEBUG(msg,...) printk("----debug line:%d\t",__LINE__);printk(msg,##__VA_ARGS__)
#else 
#define DEBUG(msg,...) 
#endif

/*设备参数控制*/
#define BUTTON_COUNT 1 		//多设备
#define BUTTON_SEMA_COUNT 2   //每个设备允许的信号量
#define DATE_SIZE 10 			//内核缓存大小(字节)
#define PHY_LED_ADR 	0x56000010	///*按键和led物理地址，当做*/										
#define PHY_BUTTON_ADR 0x56000060		///*requst_mem_region参数使用，不用加volatile*/*/

/*设备io*/
/*led*/
#define LED_SET_OUT (0x01)		//01输出
#define LED1_SET_OUT (LED_SET_OUT << 5)
#define LED2_SET_OUT (LED_SET_OUT << 6)
#define LED3_SET_OUT (LED_SET_OUT << 7)
#define LED4_SET_OUT (LED_SET_OUT << 8)
#define LED_ALL_OUT (LED1_SET_OUT | LED2_SET_OUT | LED3_SET_OUT | LED4_SET_OUT)
/*button for set in*/
#define BUTTON_SET_IN (0x3) //00 输入 最后记得取反
#define BUTTON1_SET_IN ~(BUTTON_SET_IN << 0) 	//... 1111 1111 1111 1100
#define BUTTON2_SET_IN ~(BUTTON_SET_IN << 3*2)	//... 1111 1111 0011 1111
#define BUTTON3_SET_IN ~(BUTTON_SET_IN << 5*2)	 
#define BUTTON4_SET_IN ~(BUTTON_SET_IN << 6*2)
#define BUTTON5_SET_IN ~(BUTTON_SET_IN << 7*2)
#define BUTTON6_SET_IN ~(BUTTON_SET_IN << 11*2)
/*button for set eint*/
#define BUTTON_SET_EINT (0x2) //10 中断 
#define BUTTON1_SET_EINT (BUTTON_SET_EINT << 0)
#define BUTTON2_SET_EINT (BUTTON_SET_EINT << 3*2)
#define BUTTON3_SET_EINT (BUTTON_SET_EINT << 5*2)
#define BUTTON4_SET_EINT (BUTTON_SET_EINT << 6*2)
#define BUTTON5_SET_EINT (BUTTON_SET_EINT << 7*2)
#define BUTTON6_SET_EINT (BUTTON_SET_EINT << 11*2)


/*设备号*/
#define DEV_MAJOR 239
#define DEV_MINOR 0
dev_t dev_major = 0;
dev_t dev_minor = 0;
module_param(dev_major,uint,400); //模块传递参数，用作手动创建设备号


/*定义按键设备对象成员*/
struct button_t 
{	//优化问题:考虑对齐方式，将占用小的成员放在前面(未处理)
	int num;						//设备编号
	u8 date[DATE_SIZE];			//内核缓存
	u32 date_len;		//数据长度
	struct device * dev_device	;
	struct cdev cdev;
	struct semaphore sem;		
};
struct button_t * button_p = NULL;	//内存中设备地址
wait_queue_head_t button_que;		 //等待队列

/*按键寄存器虚拟地址 --当考虑移植性时，应该使用内核提供的那一套*/
//const 地址不允许改变，但是内容允许改变
volatile unsigned int * button_vir_adr = NULL; //映射的io虚拟内存
volatile unsigned int * GPG_CON = NULL;
volatile unsigned int * GPG_DAT = NULL;
volatile unsigned int * GPG_UP =NULL;
/*led灯*/
volatile unsigned int * led_vir_adr = NULL; //映射的io虚拟内存
volatile unsigned int * GPB_CON = NULL;
volatile unsigned int * GPB_DAT = NULL;
volatile unsigned int * GPB_UP =NULL;

int button_open (struct inode *inod, struct file *filp)
{
	struct button_t * cdevp = NULL;
	cdevp = container_of(inod->i_cdev,struct button_t,cdev);
	filp->private_data = cdevp;
	DEBUG("geting semaphore...\n");
	
	if (down_interruptible(&cdevp->sem))//获取信号量，没获取到将会阻塞，可中断
	{//ctrl + z 也会中断
		printk("singnal interrupt,exit...\n");
		return -EINTR;//中断返回
	}
	DEBUG("geted semaphore,continu...\n");
	
	return 0;
}


int button_release (struct inode * inode, struct file * filp)
{
	struct button_t *cdevp = filp->private_data;
	up(&cdevp->sem);
	wake_up_interruptible(&button_que);
	printk("semaphore has release\n");
	return 0;
}

ssize_t button_read (struct file *filp, char __user * ubuff, 
							size_t count, loff_t *f_ops)
{
	int retval = 0;
	struct button_t * cdevp = filp->private_data;
	size_t cnt = count;
	loff_t pos = *f_ops;//当前文件位置

	DEBUG("entry read cnt= %d\n",(int)count);
	DEBUG("pos = %d,date_len = %d\n",(int)pos,(int)cdevp->date_len);
	

	while (pos > cdevp->date_len )
	{
		printk("pos great than date_len, go sleep~~~ \n");
		up(&cdevp->sem);//先释放信号量，马上进入睡眠
		if ( wait_event_interruptible(button_que, !(pos > cdevp->date_len)) )
			return -EINTR;
		down_interruptible(&cdevp->sem);
		
		printk("wake up\n");	
	}

	if( cnt < (cdevp->date_len - pos) )
	{//数据不够读 但是可以读取小于count的数据
		cnt = cdevp->date_len -pos;
	}

	// 数据拷贝
	retval = copy_to_user(ubuff, &(cdevp->date[0]), cnt);
	if(retval)
	{
		printk("copy_to_user error! retval %d\n",retval);
		return - EFAULT;
	}

	//更新数据	
	*f_ops += cnt;
	DEBUG("filp->loff_t = %d\n",(int)filp->f_pos);//
	DEBUG("buff: %d\n",(int)cdevp->date[0]);
	DEBUG("f_ops = %d\n",(int)filp->f_pos);
	
	return cnt;
}

/*中断处理函数*/
irqreturn_t button_irq_handler(int irq, void *pam)
{
	int event = -1;//记录事件
	struct button_t * cdevp = &button_p[0]; //只操作第一个设备
	printk("entry interrupt\n");
	if(BUTTON_COUNT > 1) //在中断函数中，无法找到是哪个设备产生的中断，所以不支持多设备
	{
		printk("warning: in irq_handler just support one device,there is %d device\n",BUTTON_COUNT);
	}
	
	#if 0
	/*判断是上升沿还是下降沿*/
	{//做个控制led的实验
		DEBUG("*GPB_CON = %lx\t*GPB_DAT =%lx\n",*GPB_CON,*GPB_DAT);
		*GPB_CON =  0x55<<10;//设置为输出
		*GPB_DAT = ~*GPB_DAT;//输高
		DEBUG("*GPB_CON = %lx\t*GPB_DAT =%lx\n",*GPB_CON,*GPB_DAT);
	}
	#endif
	switch ((int)pam)
	{
		case 1:
			*GPG_CON &= (BUTTON1_SET_IN);//临时设置为输入
			event = (*GPG_DAT) &0x1;;
			*GPG_CON = (BUTTON1_SET_EINT);//读取数据后重新设置为中断
			break;
		case 2:

			break;

		case 3:

			break;
		case 4:

			break;
		case 5:

			break;
		case 6:

			break;
	}
	DEBUG("event = %d\n",event);
	if(0 == event)
	{
		printk("%d button down\n",(int)pam);
	}
	else if(1 == event)
	{
		printk("%d button up\n",(int)pam);
	}
	
	cdevp->date_len++; 
	cdevp->date[0] = (int)pam; //这儿直接访问了，button_t的内存地址
	
	DEBUG("date = %d\n",(int)pam);
	wake_up_interruptible(&button_que);
	/*登记底部函数  待拓展*/
	//schedule_work(&);
	return 0;

}




/*函数操作集合*/
struct file_operations fops =
{
	.owner = THIS_MODULE,
	.open  = button_open,
	.release = button_release,
	.read 	 = button_read,
};
struct class * button_class = NULL;

static int __init button_init(void)
{

	int retval = -EINVAL;
	dev_t dev = 0;
	int i = 0;
	
	printk("--button driver star--.\n");
	
	/*实现设备号手动与自动获取*/
	if(dev_major != 0)
	{/*手动获取*/
		dev = MKDEV(dev_major,dev_minor);
		printk("creat dev by hand,major_dev = %d\tminor_dev = %d\n",MAJOR(dev),MINOR(dev));
		retval = register_chrdev_region(dev, BUTTON_COUNT, "my_button");
		if (retval < 0)
		{
			printk("register_chrdev_region error!\n");
			goto failure_register_chrdev_region;
		}	
	}
	else
	{/*自动获取*/
		retval = alloc_chrdev_region(&dev, 0, BUTTON_COUNT, "my_button");
		if (retval < 0)
		{
			printk("register_chrdev_region error!\n");
			goto failure_register_chrdev_region;
		}
		printk("auto alloc dev.\n");
		dev_major = MAJOR(dev);
		dev_minor = MINOR(dev);
	}


	/*申请内核空间*/
	button_p = kmalloc( sizeof(struct button_t)*BUTTON_COUNT, GFP_KERNEL);
	if( IS_ERR(button_p) )
	{
		printk("kmallloc error.\n");
		retval = PTR_ERR(button_p);
		goto failure_kamlloc;
	}
	memset(button_p, 0, sizeof(struct button_t)*BUTTON_COUNT);

	
	/*创建类*/
	button_class = class_create(THIS_MODULE, "button");
	if( IS_ERR(button_class) )
	{
		printk("class_create error!\n");
		retval = PTR_ERR(button_class);
		goto failure_class_create;
	}

	
	/*根据类，循环创建设备*/
	for(i=0; i<BUTTON_COUNT; i++)
	{
		dev = MKDEV(dev_major,dev_minor+i);
		cdev_init(&button_p[i].cdev, &fops);
		retval = cdev_add(&button_p[i].cdev, dev, 1);
		if (retval < 0)
		{
			printk("cdev_add error.\n");
			goto failure_cdev_add;
		}
		
		button_p[i].dev_device = device_create(button_class, NULL, dev, NULL, "button%d", dev_minor+i);
		if ( IS_ERR(button_p[i].dev_device) )
		{
			printk("device_reate error.\n");
			retval = PTR_ERR(button_p[i].dev_device);
			goto failure_device_create;
		}
		
		//其他成员初始化。
		button_p[i].num = i;
		button_p[i].date_len = 0;
		
		printk("success creat a cdev,major:%d\tminor:%d\n",MAJOR(dev),MINOR(dev));
	}

	/*初始化信号量*/
	for(i=0; i<BUTTON_COUNT; i++)
	{
		sema_init(&button_p[i].sem, BUTTON_SEMA_COUNT);
	}

	/*注册中断 一共6个按键*/
	request_irq(IRQ_EINT8, button_irq_handler, IRQF_TRIGGER_FALLING|IRQF_TRIGGER_RISING, "button_interrupt", (void*)1);
	request_irq(IRQ_EINT11, button_irq_handler, IRQF_TRIGGER_FALLING|IRQF_TRIGGER_RISING, "button_interrupt", (void*)2);
	request_irq(IRQ_EINT13, button_irq_handler, IRQF_TRIGGER_FALLING|IRQF_TRIGGER_RISING, "button_interrupt", (void*)3);
	request_irq(IRQ_EINT14, button_irq_handler, IRQF_TRIGGER_FALLING|IRQF_TRIGGER_RISING, "button_interrupt", (void*)4);
	request_irq(IRQ_EINT15, button_irq_handler, IRQF_TRIGGER_FALLING|IRQF_TRIGGER_RISING, "button_interrupt", (void*)5);
	request_irq(IRQ_EINT19, button_irq_handler, IRQF_TRIGGER_FALLING|IRQF_TRIGGER_RISING, "button_interrupt", (void*)6);


	/*获取物理地址*/
	request_mem_region(PHY_BUTTON_ADR, 4*3, "button");
	button_vir_adr = ioremap(PHY_BUTTON_ADR, 4*3);
	if( button_vir_adr < 0)
	{
		printk("ioremap1 error.\n");
		goto failure_ioremap;
	}
	GPG_CON 	= button_vir_adr;		//GPIO的配置地址
	GPG_DAT 	= button_vir_adr +1;	
	GPG_UP		= button_vir_adr +2;
	DEBUG("GPG_CON = %lx\tGP_DAT = %lx\tGPG_UP = %lx\n",button_vir_adr,
											button_vir_adr+1,button_vir_adr+2);
	/*led*/
	request_mem_region(PHY_LED_ADR, 4*3, "led");
	led_vir_adr = ioremap(PHY_LED_ADR, 4*3);
	if( led_vir_adr < 0)
	{
		printk("ioremap2 error.\n");
		goto failure_ioremap;
	}
	GPB_CON 	= led_vir_adr;		//GPIO的配置地址
	GPB_DAT 	= led_vir_adr +1;	
	GPB_UP		= led_vir_adr +2;
	DEBUG("GPB_CON = %lx\tGPB_DAT = %lx\tGPB_UP = %lx\n",led_vir_adr,
											led_vir_adr+1,led_vir_adr+2);

	/* 初始化一个等待队列头*/
	init_waitqueue_head(&button_que);
	
	printk("\n--button driver finished--.\n");
	return 0;

	/*逆向消除影响*/
failure_ioremap:
	release_mem_region(PHY_LED_ADR, 4*3);
	release_mem_region(PHY_BUTTON_ADR, 4*3);
	
failure_device_create:
for(i=0; i< BUTTON_COUNT; i++)
{
	cdev_del(&button_p[i].cdev);
}

failure_cdev_add:
	class_destroy(button_class);
	
failure_class_create:
	kfree(button_p);

failure_kamlloc:
	unregister_chrdev_region(dev, BUTTON_COUNT);
	
failure_register_chrdev_region:
	printk("------error,retval = %d -----\n",retval);
	return retval;
}//end module_init


void __exit button_exit(void)
{
	int i=0;

	iounmap(button_vir_adr);
	iounmap(led_vir_adr);

	release_mem_region(PHY_LED_ADR, 4*3);
	release_mem_region(PHY_BUTTON_ADR, 4*3);

	free_irq(IRQ_EINT8, (void *)1);
	free_irq(IRQ_EINT11, (void *)2);
	free_irq(IRQ_EINT13, (void *)3);
	free_irq(IRQ_EINT14, (void *)4);
	free_irq(IRQ_EINT15, (void *)5);
	free_irq(IRQ_EINT19, (void *)6);


	for(i=0;i<BUTTON_COUNT;i++)
	{
		device_destroy(button_class,MKDEV(dev_major,i));
	}
	
	for(i=0; i< BUTTON_COUNT; i++)
	{
		cdev_del(&button_p[i].cdev);
	}
	
	class_destroy(button_class);
	
	kfree(button_p);

	unregister_chrdev_region(MKDEV(dev_major,dev_minor), BUTTON_COUNT);

	printk("button driver remove.\n");
}

module_init(button_init);
module_exit(button_exit);
//考虑将阻塞放在open和relase中
