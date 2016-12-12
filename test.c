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
	while(1);
	return 0;
}
