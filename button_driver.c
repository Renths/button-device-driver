#include<linux/module.h>
#include<linux/init.h>

#include<linux/cdev.h>
#include<linux/device.h>
#include<linux/fs.h>
#include<linux/semaphore.h>
#include<linux/sched.h>
//#include<linux/irqreturn.h>
#include<linux/io.h>
#include<linux/interrupt.h>
MODULE_LICENSE("GPL");

#define BUTTON_COUNT 1 		//多设备
#define BUTTON_SEMA_COUNT 2   //每个设备允许的信号量
#define BUFF_SIZE 1 			//内核缓存大小(字节)

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
	int kbuff;			//内核缓存
	struct device * dev_device	;
	struct cdev cdev;
	struct semaphore sem;	
	
};
struct button_t * button_p = NULL;//内存中设备地址

volatile unsigned int * vir_add = NULL; //映射的io虚拟内存

int button_open (struct inode *inod, struct file *filp)
{
	struct button_t * cdevp = NULL;
	cdevp = container_of(inod->i_cdev,struct button_t,cdev);
	filp->private_data = cdevp;
	printk("geting semaphore...\n");
	down_interruptible(&cdevp->sem);//获取信号量，没获取到将会阻塞，可中断
	//{//ctrl + z 也会中断
	//	printk("singnal interrupt,exit...\n");
	//	return -EINTR;//中断返回
	//}不知道为什么，该函数一直返回一个负数
	printk("geted semaphore,continu...\n");
	
	return 0;
}


int button_release (struct inode * inode, struct file * filp)
{
	
	struct button_t *cdevp = filp->private_data;
	up(&cdevp->sem);
	printk("semaphore has release\n");
	return 0;
}

ssize_t button_read (struct file *filp, char __user * buff, 
							size_t num, loff_t *offt)
{
	struct button_t * cdevp = filp->private_data;
	
	printk("entry read\n");
	printk("filp->loff_t = %d\n",(int)filp->f_pos);//
	printk("buff: %d\n",(int)cdevp->kbuff);
	return 0;
}

/*中断处理函数*/
irqreturn_t button_irq_handler(int irq, void *pam)
{
	printk("key %d down\n",(int)pam);
	button_p->kbuff = (int)pam; //这儿直接访问了，button_t的内存地址
	
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
	request_mem_region(0x56000060, 4*3, "button");
	vir_add = ioremap(0x56000060, 4*3);
	if( vir_add < 0)
	{
		printk("ioremap error.\n");
		goto failure_ioremap;
	}
	
	printk("\n--button driver finished--.\n");
	return 0;

	/*逆向消除影响*/
failure_ioremap:
	release_mem_region(0x56000060, 4*3);
	
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

	iounmap(vir_add);

	release_mem_region(0x56000060, 4*3);

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
	
