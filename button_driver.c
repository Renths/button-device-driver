#include<linux/module.h>
#include<linux/init.h>

#include<linux/cdev.h>
#include<linux/device.h>
#include<linux/fs.h>
#include<linux/semaphore.h>
#include<linux/sched.h>
MODULE_LICENSE("GPL");

#define BUTTON_COUNT 1 		//���豸
#define BUTTON_SEMA_COUNT 2   //ÿ���豸������ź���

/*�豸��*/
#define DEV_MAJOR 239
#define DEV_MINOR 0
dev_t dev_major = 0;
dev_t dev_minor = 0;
module_param(dev_major,uint,400); //ģ�鴫�ݲ����������ֶ������豸��


/*���尴���豸�����Ա*/
struct button_t 
{	
	struct device * dev_device	;
	struct cdev cdev;
	int num;
	struct semaphore sem;
};
struct button_t * button_p = NULL;//�ڴ����豸��ַ

int button_open (struct inode *inod, struct file *filp)
{
	struct button_t * cdevp = NULL;
	cdevp = container_of(inod->i_cdev,struct button_t,cdev);
	filp->private_data = cdevp;
	printk("geting semaphore...\n");
	down_interruptible(&cdevp->sem);//��ȡ�ź�����û��ȡ���������������ж�
	//{
	//	printk("singnal interrupt,exit...\n");
	//	return -EINTR;//�жϷ���
	//}
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

ssize_t button_read (struct file *filp, char __user * , size_t, loff_t *)
{
	
}

/*�жϴ�����*/
irqreturn_t button_irq_handler(int irq, void *pam)
{
	printk("a interrupt comeing\n");
	/*�ȼ��ײ�����*/
	

}


/*������������*/
struct file_operations fops =
{
	.owner = THIS_MODULE,
	.open = button_open,
	.release = button_release,
};
struct class * button_class = NULL;

static int __init button_init(void)
{

	int retval = -EINVAL;
	dev_t dev = 0;
	int i = 0;
	
	printk("--button driver star--.\n");
	
	/*ʵ���豸���ֶ����Զ���ȡ*/
	if(dev_major != 0)
	{/*�ֶ���ȡ*/
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
	{/*�Զ���ȡ*/
		retval = alloc_chrdev_region(&dev, 0, BUTTON_COUNT, "my_button");
		if (retval < 0)
		{
			printk("register_chrdev_region error!\n");
			goto failure_register_chrdev_region;
		}
		printk("auto alloc dev,major_dev = %d\tminor_dev = %d\n",MAJOR(dev),MINOR(dev));
		dev_major = MAJOR(dev);
		dev_minor = MINOR(dev);
	}


	/*�����ں˿ռ�*/
	button_p = kmalloc( sizeof(struct button_t)*BUTTON_COUNT, GFP_KERNEL);
	if( IS_ERR(button_p) )
	{
		printk("kmallloc error.\n");
		retval = PTR_ERR(button_p);
		goto failure_kamlloc;
	}
	memset(button_p, 0, sizeof(struct button_t)*BUTTON_COUNT);

	
	/*������*/
	button_class = class_create(THIS_MODULE, "button");
	if( IS_ERR(button_class) )
	{
		printk("class_create error!\n");
		retval = PTR_ERR(button_class);
		goto failure_class_create;
	}

	
	/*�����࣬ѭ�������豸*/
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

	/*��ʼ���ź���*/
	for(i=0; i<BUTTON_COUNT; i++)
	{
		sema_init(&button_p[i].sem, BUTTON_SEMA_COUNT);
	}

	/*ע���ж�*/
	request_irq(unsigned int irq, irq_handler_t handler, unsigned long irqflags, const char * devname, void * dev_id)

	/*��ȡ�����ַ*/
	request_mem_region(start, n, name)
	
	printk("\n--button driver finished--.\n");
	return 0;

	/*��������Ӱ��*/
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

	free_irq(unsigned int irq, void * dev_id);
	
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
	
