// Server side implementation of UDP client-server model
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
	
#define DEBUG_TIME

#ifdef DEBUG_TIME
#include <sys/time.h>
#include <unistd.h>
#endif
	
#define TRANSMISSION_PACKAGE_SIZE	1024
#define TRANS_ACK_SUCCESS	0x55AA00
#define TRANS_ACK_FAILURE	0x55AA01

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

int recvPackage(ptTransInfo sockInfo, void *buf, int recv_size)
{
	int ack = TRANS_ACK_SUCCESS;
	int byteLeft;
	int retval;
	
	int len = sizeof(struct sockaddr_in);
	byteLeft = recv_size;
	int index = 0;
	while (index < recv_size)
	{
		retval = recvfrom(sockInfo->sockfd, buf+index, byteLeft, MSG_WAITALL, (struct sockaddr *) sockInfo->sockaddr, &len);
		
		if( retval == -1 )
		{
			printf("receive packet error\r\n");
			ack = TRANS_ACK_FAILURE;
			break;
		}
		
		index += retval;
		byteLeft -= retval;
	}
	
	// send ack
	byteLeft = sizeof(unsigned int);
	retval = sendto(sockInfo->sockfd, &ack, byteLeft, MSG_CONFIRM, (struct sockaddr *) sockInfo->sockaddr, len);
	
	if( retval != byteLeft )
	{
		printf("send ack error\r\n");
		ack = TRANS_ACK_FAILURE;
	}
	
	return (ack == TRANS_ACK_SUCCESS) ? recv_size : -1;
}
	
int getFile(ptTransInfo sockInfo)
{
    unsigned char *file_ptr;
    int file_size;
    int next_command;
    unsigned int ack = TRANS_ACK_SUCCESS;
    int count=0;
    char filename[80];

	int section_size;
	int ret = 0;
	int retval;
	int transferSize;

	printf("Start receving file\r\n");
	
	// 1. receive file size
	retval = recvPackage(sockInfo, (void*)&file_size, sizeof(file_size));
	if( retval < 0 )
	{
		printf("receive file size error\r\n");
		ret = -1;
		goto exit;
	}
	
	printf("File size is %d\r\n", file_size);

	if( file_size < 0 )
	{
		printf("Client finished transfer\r\n");
		// client finished transfer
		return CMD_NEXT_FINISHED;
	}
	
	// 2. receive section size
	retval = recvPackage(sockInfo, (void*)&section_size, sizeof(section_size));
	if( retval < 0 )
	{
		printf("receive section size error\r\n");
		ret = -1;
		goto exit;
	}
	printf("section size is %d\r\n", section_size);
	
	// 3. receive data loop
	file_ptr = (unsigned char*)malloc(file_size);
	int index = 0;
	do
	{
		int len = ((index+section_size) > file_size) ? (file_size-index) : section_size;
		
		retval = recvPackage(sockInfo, (void*)(file_ptr+index), len);
		//printf("Received %d bytes\r\n", retval);
		if( retval != len )
		{
			printf("Receive data error\r\n");
			ret = -1;
			goto exit;
		}

		index += section_size;
	}while(index < file_size);	
	
	printf("checksum = %08X\r\n", checksum(file_ptr, file_size));
	
	// receive next_comamnd
	transferSize = sizeof(next_command);
	retval = recvPackage(sockInfo, (void*)&next_command, transferSize);
	if( retval != transferSize )
	{
		printf("receive next command error\r\n");
		ret = -1;
		goto exit;
	}
	printf("Next comamnd=%d\r\n", next_command);	
	ret = next_command;
	
exit:
	free(file_ptr);
	return ret;
}
	
int main(int argc, char **argv) 
{
	int sockfd;
	struct sockaddr_in servaddr, cliaddr;
	tTransInfo transInfo;
#ifdef DEBUG_TIME
	struct timeval start, end;
#endif
	
	if( argc != 2 )
	{
		printf("Usage: udp_server PORT_NUMBER\r\n");
		return -1;
	}
		
	// Creating socket file descriptor
	if ( (sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0 ) {
		perror("socket creation failed");
		exit(EXIT_FAILURE);
	}
		
	memset(&servaddr, 0, sizeof(servaddr));
	memset(&cliaddr, 0, sizeof(cliaddr));
		
	// Filling server information
	servaddr.sin_family = AF_INET; // IPv4
	servaddr.sin_addr.s_addr = INADDR_ANY;
	servaddr.sin_port = htons(atoi(argv[1]));
	
	transInfo.sockfd = sockfd;
	transInfo.sockaddr = &servaddr;
	transInfo.addrlen = sizeof(servaddr);
		
	// Bind the socket with the server address
	if ( bind(sockfd, (const struct sockaddr *)&servaddr, sizeof(servaddr)) < 0 )
	{
		perror("bind failed");
		exit(EXIT_FAILURE);
	}
	
	while(1)
	{
		int next_command;
	
		do
		{
			#ifdef DEBUG_TIME
			gettimeofday(&start, NULL);
			#endif	
			
			next_command = getFile(&transInfo);
			
			#ifdef DEBUG_TIME
			gettimeofday(&end, NULL);
			
			long long time_es = ((long long)end.tv_sec * 1000000 + end.tv_usec) -
					((long long)start.tv_sec * 1000000 + start.tv_usec);
			printf("Transfer time = %lld.%lld\r\n"  , time_es/1000, time_es%1000);
			#endif		
		}while(next_command == CMD_NEXT_KEEPGOING);
	}	
	
	close(sockfd);
	return 0;
}
