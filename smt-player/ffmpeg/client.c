/************************************************************************* 
 > File Name: client.c 
 > Author: Minghao LI
 > Purpose: the testing tool for the ffmpeg command server
 ************************************************************************/
#include<sys/types.h> 
#include<sys/socket.h> 
#include<unistd.h> 
#include<netinet/in.h> 
#include<arpa/inet.h> 
#include<stdio.h> 
#include<stdlib.h> 
#include<errno.h> 
#include<netdb.h> 
#include<stdarg.h> 
#include<string.h> 
  
#define SERVER_PORT 1234      //default server port
#define BUFFER_SIZE 1024 
#define COMMAND_MAX_LEN 1024 
  
int port = SERVER_PORT;
char* server;
int main (int argc, char **argv)	
{ 
 if(argc == 2 ) {
	 port = atoi(argv[1]);
	 server = "127.0.0.1";
 }
 else if(argc == 3) {
	 server = argv[1];
	 port = atoi(argv[2]);
 }
 else {
	 exit(0);
 }

 struct sockaddr_in server_addr; 
 bzero(&server_addr, sizeof(server_addr)); 
 server_addr.sin_family = AF_INET; 
 server_addr.sin_addr.s_addr = inet_addr("127.0.0.1"); 
 server_addr.sin_port = htons(port); 
  
 int client_socket_fd = socket(AF_INET, SOCK_DGRAM, 0); 
 if(client_socket_fd < 0) 
 { 
  perror("Create Socket Failed:"); 
  exit(1); 
 } 
  
 while (1)
{
 char command[COMMAND_MAX_LEN+1]; 
 bzero(command, COMMAND_MAX_LEN+1); 
 printf("Please Input the command to Server:\n>"); 

fgets(command, COMMAND_MAX_LEN, stdin);

 if(command[0] == '\n')  
	strcpy(command,"add smt://127.0.0.1:9090");
 char buffer[BUFFER_SIZE]; 
 bzero(buffer, BUFFER_SIZE); 
 if(command[0] == ':') {
	 strcpy(buffer, "add udp://127.0.0.1");
 }
 strncpy(buffer+strlen(buffer), command, strlen(command)>BUFFER_SIZE?BUFFER_SIZE:strlen(command)); 
  
 if(sendto(client_socket_fd, buffer, BUFFER_SIZE,0,(struct sockaddr*)&server_addr,sizeof(server_addr)) < 0) 
 { 
  perror("Send the command Failed, please retry\n"); 
  exit(1); 
 } 
}
  
 close(client_socket_fd); 
 return 0; 
} 
