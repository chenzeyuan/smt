#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <unistd.h>

#define MTU 1500
int main(int argc, char** argv)
{
	FILE *f;
	int len, size = MTU;
	int fd;
        int count = 0;
	struct sockaddr_in addr;
	
	if(argc < 3)
		return -1;
	f = fopen(argv[1], "rb");
	unsigned char buf[MTU];
	
	fd = socket(AF_INET, SOCK_DGRAM, 0);

	bzero(&addr, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = inet_addr(argv[2]);
	addr.sin_port = htons(5050);
        
	while(1){
                if(count%2){
			size = MTU;
		}else{
			size = MTU/3;
		}	
		len = fread(buf, sizeof(unsigned char), size, f);
		if(len <= 0)
			return 0;
		if(0 >= sendto(fd, buf, len, 0, (struct sockaddr *)&addr, sizeof(addr)))
			return 0;
		usleep(1000*10);
                count++;
	}
	close(fd);
	fclose(f);	
}
