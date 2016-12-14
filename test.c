#include"stdio.h"
#include"fcntl.h"
int main(void)
{
	int fd = 0;
	fd = open("/dev/button0",O_RDWR);
	if(fd < 0)
	{
		printf("open file fial!\n");
		perror("open");
	}
	printf("open seccess fd = %d\n",fd);
	char flag =  0;
	int buff[1];
	char tmp[20];
	while(1)
	{	
		flag = getchar();
		gets(tmp);
		switch(flag)
		{
			case 'r' :
				read(fd,buff,1);
				printf("user buff:%d",*buff);
				break;
			case 'w' :
				write(fd,buff,1);
				break;
			default :
				printf("unkonw cmd\n");
				break;
		}	
	}
	return 0;
}
