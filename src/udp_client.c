// Client side implementation of UDP client-server model
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
	
#define TRANS_ACK_SUCCESS	0x55AA00
#define TRANS_ACK_FAILURE	0x55AA01
#define TRANS_RETRY 0x55AA02

#define CMD_NEXT_KEEPGOING	1
#define CMD_NEXT_FINISHED	2

typedef struct __TRANS_INFO__
{
	int sockfd;
	struct sockaddr_in *sockaddr;
	socklen_t addrlen;
}tTransInfo,*ptTransInfo;

unsigned int checksum(unsigned char *buf, int length)
{
	unsigned int sum = 0;
	
	for(int i=0; i<length; i++)
	{
		sum += (unsigned int)(*(buf+i));
	}
	
	return sum;
}

int sendPackage(ptTransInfo sockInfo, void *buf, int send_size)
{
	int byteLeft;
	int ack = TRANS_ACK_SUCCESS;
	int retval;
	
	byteLeft = send_size;
	
	int len = sizeof(struct sockaddr_in);
	int index = 0;
	while( index < send_size )
	{
		retval = sendto(sockInfo->sockfd, buf+index, byteLeft, MSG_CONFIRM, (struct sockaddr *) sockInfo->sockaddr, len);
		
		if( retval == -1 )
		{
			printf("Send package error error\r\n");
			ack = TRANS_ACK_FAILURE;	
			break;
		}
		
		index += retval;
		byteLeft -= retval;
	}
	
	// waiting for ack
	byteLeft = sizeof(int);
	retval = recvfrom(sockInfo->sockfd, &ack, byteLeft, MSG_WAITALL, (struct sockaddr *) sockInfo->sockaddr, &len);
	if( retval != byteLeft || ack != TRANS_ACK_SUCCESS)
	{
		printf("%d@%d, %X\r\n", retval, byteLeft, ack);
		printf("receive ack error\r\n");
	}
	
	return (ack == TRANS_ACK_SUCCESS) ? send_size : -1;	
}

int sendFile(ptTransInfo sockInfo, int section_size, const char *filename)
{
	int file_size;
	unsigned char *file_ptr;
	int retval;
	int transferSize;
	int ack;
	int ret = 0;
	int index = 0;
	
	// get file size
	FILE *fp = fopen(filename, "rb");
	fseek(fp, 0L, SEEK_END);
	file_size = ftell(fp);
	fseek(fp, 0L, SEEK_SET);

	// read file data
	file_ptr = (unsigned char*)malloc(file_size);
	fread(file_ptr, file_size, 1, fp);
	fclose(fp);
	
	// 1. send file size
	printf("File size %d bytes\r\n", file_size);
	retval = sendPackage(sockInfo, (void*)&file_size, sizeof(file_size));
	if( retval != sizeof(file_size) )
	{
		printf("Send file size error\r\n");
		ret = -1;
		goto exit;
	}
	
	// 2. send section size	
	printf("Section size %d bytes\r\n", section_size);
	retval = sendPackage(sockInfo, (void*)&section_size, sizeof(section_size));
	if( retval != sizeof(section_size) )
	{
		printf("Send section size error\r\n");
		ret = -1;
		goto exit;
	}
	
	// 3. send file data
	printf("checksum = %08X\r\n", checksum(file_ptr, file_size));
	do
	{
		int len = ((index+section_size) > file_size) ? (file_size-index) : section_size;
		retval = sendPackage(sockInfo, (void*)(file_ptr+index), len);
		printf("Sent %d bytes\r\n", retval);
		if( retval != len )
		{
			printf("Receive data error\r\n");
			ret = -1;
			goto exit;
		}
		
		index += section_size;
	}while(index < file_size);

exit:	
	free(file_ptr);
	
	return ret;
}

int sendNextCommand(ptTransInfo sockInfo, int next_command)
{
	int transferSize = 0;
	int retval = 0;
	int ret = 0;
	int ack = 0;
	
	printf("Next command=%X\r\n", next_command);
	// 1. send next command
	retval = sendPackage(sockInfo, (void*)(&next_command), sizeof(next_command));
	if( retval != sizeof(next_command) )
	{
		printf("Send ack error\r\n");
		return -1;
	}

	return ret;
}

void sendFiles(ptTransInfo sockInfo, int section_size, const char *listname)
{
	unsigned char *file_ptr;
	int file_size;
	int next_command;
	unsigned int ack = TRANS_ACK_SUCCESS;
	// debug
	char filename[80];
	int ret;

	FILE *fp;
	
	if( (fp = fopen(listname, "r")) == NULL )
	{
		printf("Read list file error\r\n");
		return;
	}
	
	while( fgets(filename, 80, fp) )
	{
		int len = strlen(filename);
		for(int i=len-1; i>0; i--)
		{
			if( filename[i] <= 0x20 )
				filename[i] = 0;
		}
		
		printf("Reading [%s]\r\n", filename);
		
		if( strcmp(filename, "EOF") == 0 )
		{
			file_size = -1;
			sendPackage(sockInfo, (void*)&file_size, sizeof(file_size));
			
			// end of file
			next_command = CMD_NEXT_FINISHED;
			
			printf("Transfer finished\r\n");	
		}
		else
		{
			ret = sendFile(sockInfo, section_size, filename);
			if( ret < 0 )
			{
				break;
			}
			
			next_command = CMD_NEXT_KEEPGOING;
			// send next com
			sendNextCommand(sockInfo, next_command);
		}	
	}
	
	fclose(fp);
}

int main(int argc, char **argv) 
{
	int sockfd;
	char *hello = "Hello from client";
	struct sockaddr_in	 servaddr;
	tTransInfo transInfo;

	if( argc != 5 )
	{
		printf("USAGE: client SERVER_IP PORT SECTION_SIZE(KB) LIST_NAME\r\n");
		return EXIT_FAILURE;
	}
	
	int section_size = atoi(argv[3]);
	int port = atoi(argv[2]);
		
	// Creating socket file descriptor
	if ( (sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0 ) {
		perror("socket creation failed");
		exit(EXIT_FAILURE);
	}
	
	memset(&servaddr, 0, sizeof(servaddr));
		
	// Filling server information
	servaddr.sin_family = AF_INET;
	servaddr.sin_port = htons(atoi(argv[2]));
	servaddr.sin_addr.s_addr = inet_addr(argv[1]);//INADDR_ANY;
	
	transInfo.sockfd = sockfd;
	transInfo.sockaddr = (struct sockaddr_in *) &servaddr;
	transInfo.addrlen = sizeof(servaddr);
	
	sendFiles(&transInfo, section_size*1024, argv[4]);
		
	close(sockfd);
	return 0;
}
