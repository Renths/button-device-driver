#说明：此文档建一个linux内核
#日期：$Sat Dec 10 17:20:52 PST 2016
KERNEL_DIR ?= /home/xmj/transplant/linux/linux-2.6.32.2
obj-m += button_driver.o
default:
	$(MAKE) -C $(KERNEL_DIR) M=$(PWD) modules
clean:
	@rm -f *.o *.ko *.order *.sy *.mod.* *.symvers*
