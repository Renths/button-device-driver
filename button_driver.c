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

/*������Ϣ����*/
#define DEBUG_ABLE 
#ifdef DEBUG_ABLE
#define DEBUG(msg,...) printk("----debug line:%d\t",__LINE__);printk(msg,##__VA_ARGS__)
#else 
#define DEBUG(msg,...) 
#endif

/*�豸��������*/
#define BUTTON_COUNT 1 		//���豸
#define BUTTON_SEMA_COUNT 2   //ÿ���豸������ź���
#define DATE_SIZE 10 			//�ں˻����С(�ֽ�)
#define PHY_LED_ADR 	0x56000010	///*������led�����ַ������*/										
#define PHY_BUTTON_ADR 0x56000060		///*requst_mem_region����ʹ�ã����ü�volatile*/*/

/*�豸io*/
/*led*/
#define LED_SET_OUT (0x01)		//01���
#define LED1_SET_OUT (LED_SET_OUT << 5)
#define LED2_SET_OUT (LED_SET_OUT << 6)
#define LED3_SET_OUT (LED_SET_OUT << 7)
#define LED4_SET_OUT (LED_SET_OUT << 8)
#define LED_ALL_OUT (LED1_SET_OUT | LED2_SET_OUT | LED3_SET_OUT | LED4_SET_OUT)
/*button for set in*/
#define BUTTON_SET_IN (0x3) //00 ���� ���ǵ�ȡ��
#define BUTTON1_SET_IN ~(BUTTON_SET_IN << 0) 	//... 1111 1111 1111 1100
#define BUTTON2_SET_IN ~(BUTTON_SET_IN << 3*2)	//... 1111 1111 0011 1111
#define BUTTON3_SET_IN ~(BUTTON_SET_IN << 5*2)	 
#define BUTTON4_SET_IN ~(BUTTON_SET_IN << 6*2)
#define BUTTON5_SET_IN ~(BUTTON_SET_IN << 7*2)
#define BUTTON6_SET_IN ~(BUTTON_SET_IN << 11*2)
/*button for set eint*/
#define BUTTON_SET_EINT (0x2) //10 �ж� 
#define BUTTON1_SET_EINT (BUTTON_SET_EINT << 0)
#define BUTTON2_SET_EINT (BUTTON_SET_EINT << 3*2)
#define BUTTON3_SET_EINT (BUTTON_SET_EINT << 5*2)
#define BUTTON4_SET_EINT (BUTTON_SET_EINT << 6*2)
#define BUTTON5_SET_EINT (BUTTON_SET_EINT << 7*2)
#define BUTTON6_SET_EINT (BUTTON_SET_EINT << 11*2)


/*�豸��*/
#define DEV_MAJOR 239
#define DEV_MINOR 0
dev_t dev_major = 0;
dev_t dev_minor = 0;
module_param(dev_major,uint,400); //ģ�鴫�ݲ����������ֶ������豸��


/*���尴���豸�����Ա*/
struct button_t 
{	//�Ż�����:���Ƕ��뷽ʽ����ռ��С�ĳ�Ա����ǰ��(δ����)
	int num;						//�豸���
	u8 date[DATE_SIZE];			//�ں˻���
	u32 date_len;		//���ݳ���
	struct device * dev_device	;
	struct cdev cdev;
	struct semaphore sem;		
};
struct button_t * button_p = NULL;	//�ڴ����豸��ַ
wait_queue_head_t button_que;		 //�ȴ�����

/*�����Ĵ��������ַ --��������ֲ��ʱ��Ӧ��ʹ���ں��ṩ����һ��*/
//const ��ַ������ı䣬������������ı�
volatile unsigned int * button_vir_adr = NULL; //ӳ���io�����ڴ�
volatile unsigned int * GPG_CON = NULL;
volatile unsigned int * GPG_DAT = NULL;
volatile unsigned int * GPG_UP =NULL;
/*led��*/
volatile unsigned int * led_vir_adr = NULL; //ӳ���io�����ڴ�
volatile unsigned int * GPB_CON = NULL;
volatile unsigned int * GPB_DAT = NULL;
volatile unsigned int * GPB_UP =NULL;

int button_open (struct inode *inod, struct file *filp)
{
	struct button_t * cdevp = NULL;
	cdevp = container_of(inod->i_cdev,struct button_t,cdev);
	filp->private_data = cdevp;
	DEBUG("geting semaphore...\n");
	
	if (down_interruptible(&cdevp->sem))//��ȡ�ź�����û��ȡ���������������ж�
	{//ctrl + z Ҳ���ж�
		printk("singnal interrupt,exit...\n");
		return -EINTR;//�жϷ���
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
	loff_t pos = *f_ops;//��ǰ�ļ�λ��

	DEBUG("entry read cnt= %d\n",(int)count);
	DEBUG("pos = %d,date_len = %d\n",(int)pos,(int)cdevp->date_len);
	

	while (pos > cdevp->date_len )
	{
		printk("pos great than date_len, go sleep~~~ \n");
		up(&cdevp->sem);//���ͷ��ź��������Ͻ���˯��
		if ( wait_event_interruptible(button_que, !(pos > cdevp->date_len)) )
			return -EINTR;
		down_interruptible(&cdevp->sem);
		
		printk("wake up\n");	
	}

	if( cnt < (cdevp->date_len - pos) )
	{//���ݲ����� ���ǿ��Զ�ȡС��count������
		cnt = cdevp->date_len -pos;
	}

	// ���ݿ���
	retval = copy_to_user(ubuff, &(cdevp->date[0]), cnt);
	if(retval)
	{
		printk("copy_to_user error! retval %d\n",retval);
		return - EFAULT;
	}

	//��������	
	*f_ops += cnt;
	DEBUG("filp->loff_t = %d\n",(int)filp->f_pos);//
	DEBUG("buff: %d\n",(int)cdevp->date[0]);
	DEBUG("f_ops = %d\n",(int)filp->f_pos);
	
	return cnt;
}

/*�жϴ�����*/
irqreturn_t button_irq_handler(int irq, void *pam)
{
	int event = -1;//��¼�¼�
	struct button_t * cdevp = &button_p[0]; //ֻ������һ���豸
	printk("entry interrupt\n");
	if(BUTTON_COUNT > 1) //���жϺ����У��޷��ҵ����ĸ��豸�������жϣ����Բ�֧�ֶ��豸
	{
		printk("warning: in irq_handler just support one device,there is %d device\n",BUTTON_COUNT);
	}
	
	#if 0
	/*�ж��������ػ����½���*/
	{//��������led��ʵ��
		DEBUG("*GPB_CON = %lx\t*GPB_DAT =%lx\n",*GPB_CON,*GPB_DAT);
		*GPB_CON =  0x55<<10;//����Ϊ���
		*GPB_DAT = ~*GPB_DAT;//���
		DEBUG("*GPB_CON = %lx\t*GPB_DAT =%lx\n",*GPB_CON,*GPB_DAT);
	}
	#endif
	switch ((int)pam)
	{
		case 1:
			*GPG_CON &= (BUTTON1_SET_IN);//��ʱ����Ϊ����
			event = (*GPG_DAT) &0x1;;
			*GPG_CON = (BUTTON1_SET_EINT);//��ȡ���ݺ���������Ϊ�ж�
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
	cdevp->date[0] = (int)pam; //���ֱ�ӷ����ˣ�button_t���ڴ��ַ
	
	DEBUG("date = %d\n",(int)pam);
	wake_up_interruptible(&button_que);
	/*�Ǽǵײ�����  ����չ*/
	//schedule_work(&);
	return 0;

}




/*������������*/
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
		printk("auto alloc dev.\n");
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
		
		//������Ա��ʼ����
		button_p[i].num = i;
		button_p[i].date_len = 0;
		
		printk("success creat a cdev,major:%d\tminor:%d\n",MAJOR(dev),MINOR(dev));
	}

	/*��ʼ���ź���*/
	for(i=0; i<BUTTON_COUNT; i++)
	{
		sema_init(&button_p[i].sem, BUTTON_SEMA_COUNT);
	}

	/*ע���ж� һ��6������*/
	request_irq(IRQ_EINT8, button_irq_handler, IRQF_TRIGGER_FALLING|IRQF_TRIGGER_RISING, "button_interrupt", (void*)1);
	request_irq(IRQ_EINT11, button_irq_handler, IRQF_TRIGGER_FALLING|IRQF_TRIGGER_RISING, "button_interrupt", (void*)2);
	request_irq(IRQ_EINT13, button_irq_handler, IRQF_TRIGGER_FALLING|IRQF_TRIGGER_RISING, "button_interrupt", (void*)3);
	request_irq(IRQ_EINT14, button_irq_handler, IRQF_TRIGGER_FALLING|IRQF_TRIGGER_RISING, "button_interrupt", (void*)4);
	request_irq(IRQ_EINT15, button_irq_handler, IRQF_TRIGGER_FALLING|IRQF_TRIGGER_RISING, "button_interrupt", (void*)5);
	request_irq(IRQ_EINT19, button_irq_handler, IRQF_TRIGGER_FALLING|IRQF_TRIGGER_RISING, "button_interrupt", (void*)6);


	/*��ȡ�����ַ*/
	request_mem_region(PHY_BUTTON_ADR, 4*3, "button");
	button_vir_adr = ioremap(PHY_BUTTON_ADR, 4*3);
	if( button_vir_adr < 0)
	{
		printk("ioremap1 error.\n");
		goto failure_ioremap;
	}
	GPG_CON 	= button_vir_adr;		//GPIO�����õ�ַ
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
	GPB_CON 	= led_vir_adr;		//GPIO�����õ�ַ
	GPB_DAT 	= led_vir_adr +1;	
	GPB_UP		= led_vir_adr +2;
	DEBUG("GPB_CON = %lx\tGPB_DAT = %lx\tGPB_UP = %lx\n",led_vir_adr,
											led_vir_adr+1,led_vir_adr+2);

	/* ��ʼ��һ���ȴ�����ͷ*/
	init_waitqueue_head(&button_que);
	
	printk("\n--button driver finished--.\n");
	return 0;

	/*��������Ӱ��*/
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
//���ǽ���������open��relase��
