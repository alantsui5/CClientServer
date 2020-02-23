#include "myftp.h"
#include <arpa/inet.h>

void quit_with_usage_msg()
{
	printf("Usage: ./myftpclient <server ip addr> <server port> <list|get|put> <file>\n");
	exit(0);
}

void message_to_server(int sd, struct message_s m_header, char *payload, int payload_length)
{
	struct packet *send_message;
	send_message = malloc(sizeof(struct message_s));
	send_message->header = m_header;
	send_message->header.length = htonl(send_message->header.length);
	if (payload != NULL)
	{
		send_message = realloc(send_message, 10 + payload_length);
		memcpy(send_message->payload, payload, m_header.length - 10);
	}
	if (sendn(sd, send_message, m_header.length) < 0)
	{
		printf("Send error: %s (Errno:%d)\n", strerror(errno), errno);
		exit(0);
	}
	free(send_message);
}

void client_list(int sd)
{
	//Construct List Request
	struct message_s list_request;
	memcpy(list_request.protocol, (unsigned char[]){'m', 'y', 'f', 't', 'p'}, 5);
	list_request.type = 0xA1;
	list_request.length = sizeof(struct message_s);
	struct packet list_request_packet;
	list_request_packet.header = list_request;

	message_to_server(sd,list_request,NULL,0);
	
	int len;
	struct packet list_reply;
	int totalsize = 0;
	if ((len = recvn(sd, &list_reply, sizeof(struct message_s))) < 0)
	{
		printf("Send error: %s (Errno:%d)\n", strerror(errno), errno);
		return;
	}

	//Validate Message 
	if (check_myftp(list_reply.header.protocol) < 0)
	{
		printf("Invalid Protocol\n");
		return;
	}

	if(list_reply.header.type != 0xA2){
		printf("Invalid Message Type\n");
		return;
	}

	if (len == 0)
		return;

	list_reply.header.length = ntohl(list_reply.header.length);

	if (list_reply.header.length > 10)
	{
		printf("---file list start---\n");
		char payload[Buffer_Size];
		while (1)
		{
			memset(&payload, 0, Buffer_Size);
			if ((len = recv(sd, &payload, Buffer_Size, 0)) < 0)
			{
				printf("Send error: %s (Errno:%d)\n", strerror(errno), errno);
				break;
			}
			printf("%s", payload);
			totalsize += len;
			if (list_reply.header.length - 10 <= totalsize)
				break;
		}

		printf("---file list end---\n");
	}
	return;
}

void client_get(int sd, char *filename)
{
	printf("Get (%s)\n", filename);

	//Construct GET Request Message
	struct message_s get_request;
	memcpy(get_request.protocol, (unsigned char[]){'m', 'y', 'f', 't', 'p'}, 5);
	get_request.type = 0xB1;
	get_request.length = sizeof(struct message_s) + strlen(filename) + 1;
	
	message_to_server(sd, get_request, filename,strlen(filename) + 1);

	//Receive GET Reply
	struct packet get_reply;
	int len;
	//Error
	if ((len = recvn(sd, &get_reply, sizeof(struct message_s))) < 0)
	{
		printf("Send error: %s (Errno:%d)\n", strerror(errno), errno);
		return;
	}

	if (len == 0)
	{
		printf("0 Packet Received\n");
		return;
	}

	//Check MYFTP
	if (check_myftp(get_reply.header.protocol) < 0)
	{
		printf("Invalid Protocol\n");
		return;
	}

	//Process Reply
	if ((unsigned char)get_reply.header.type == 0xB2)
	{
		//Receive File and write to disk
		struct packet file_data;
		int file_data_len;
		if ((file_data_len = recvn(sd, &file_data, sizeof(struct message_s))) < 0)
		{
			printf("Send Error: %s (Errno:%d)\n", strerror(errno), errno);
			return;
		}
		if (file_data_len == 0)
		{
			printf("0 Packet Received\n");
			return;
		}
		FILE *fptr = fopen(filename, "w");
		int transfered_data_len = 0;

		file_data.header.length = ntohl(file_data.header.length);

		if (file_data.header.length > 10)
		{
			printf("File size received : %d\n", file_data.header.length - 10);
			char payload[Buffer_Size + 1];
			while (1)
			{
				memset(&payload, 0, Buffer_Size + 1);
				if ((file_data_len = recv(sd, &payload, Buffer_Size,0)) < 0)
				{
					printf("Send error: %s (Errno:%d)\n", strerror(errno), errno);
					break;
				}
				fwrite(payload,1,file_data_len,fptr);
				transfered_data_len += file_data_len;
				if (file_data.header.length - 10 <= transfered_data_len)
					break;
			}
		}
		fclose(fptr);
		printf("[%s] Download Completed.\n",filename);
	}
	else if ((unsigned char)get_reply.header.type == 0xB3)
	{
		printf("File does not exist.\n");
		return;
	}else{
		printf("Invalid message type.\n");
		return;
	}
}

void client_put(int sd, char* filename){
	printf("Put (%s)\n", filename);
	
	// Check File existance
	if( access(filename, F_OK ) != -1 )
    	printf("[%s] Exist.\n", filename);
	else {
		printf("The File [%s] does not exist.\n",filename);
		return;
	}
	//Construct Put Request
	struct message_s put_request;
	memcpy(put_request.protocol, (unsigned char[]){'m', 'y', 'f', 't', 'p'}, 5);
	put_request.type = 0xC1;
	put_request.length = sizeof(struct message_s)+strlen(filename)+1;
	message_to_server(sd, put_request, filename, strlen(filename)+1);

	//Receive Post Reply
	struct packet post_reply;
	int len;
	//Error
	if ((len = recvn(sd, &post_reply, sizeof(struct message_s))) < 0)
	{
		printf("Send error: %s (Errno:%d)\n", strerror(errno), errno);
		return;
	}

	if (len == 0)
	{
		printf("0 Packet Received\n");
		return;
	}

	//Validate Message 
	if (check_myftp(post_reply.header.protocol) < 0)
	{
		printf("Invalid Protocol\n");
		return;
	}

	if(post_reply.header.type != 0xC2){
		printf("Invalid Message Type\n");
		return;
	}

	//Open File
	FILE * fptr = fopen(filename,"rb");
	if(fptr==NULL){
		printf("file open error: %s (Errno:%d)\n",(char *)strerror(errno),errno);
		return;
	}

	//Get File Size
	fseek(fptr,0,SEEK_END);
	int filesize = ftell(fptr);
	rewind(fptr);

	//Send File
	char * buffer = malloc(sizeof(char) * filesize);
	printf("File Size To Send %lu\n",fread(buffer,sizeof(char),filesize,fptr));
	struct message_s file_data;
	memcpy(file_data.protocol, (unsigned char[]){'m', 'y', 'f', 't', 'p'}, 5);
	file_data.type = 0xFF;
	file_data.length = sizeof(struct message_s) + filesize;
	message_to_server(sd, file_data, buffer, filesize);

	free(buffer);
	fclose(fptr);
	printf("File Transfer Completed.\n");
		
}

int main(int argc, char **argv)
{
	//Mode
	// 0 - LIST
	// 1 - GET
	// 2 - PUT
	int mode;
	char filename[255];

	//Input Checking
	if (argc < 4)
	{
		quit_with_usage_msg();
	}

	if ((strcmp(argv[3], "get")) == 0 || (strcmp(argv[3], "put") == 0))
	{
		if (argc == 5)
		{
			strcpy(filename, argv[4]);
			if (strcmp(argv[3], "get") == 0)
			{
				mode = 1;
			}
			else
			{
				mode = 2;
			}
		}
		else
		{
			quit_with_usage_msg();
		}
	}
	else if ((strcmp(argv[3], "list")) == 0)
	{
		mode = 0;
	}
	else
	{
		quit_with_usage_msg();
	}

	//Connection Setup
	int sd = socket(AF_INET, SOCK_STREAM, 0);
	struct sockaddr_in server_addr;
	memset(&server_addr, 0, sizeof(server_addr));
	server_addr.sin_family = AF_INET;
	server_addr.sin_addr.s_addr = inet_addr(argv[1]);
	server_addr.sin_port = htons(atoi(argv[2]));
	if (connect(sd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
	{
		printf("connection error: %s (Errno:%d)\n", strerror(errno), errno);
		exit(0);
	}

	//Operation
	switch (mode)
	{
	case 0:
		client_list(sd);
		break;
	case 1:
		client_get(sd, filename);
		break;
	case 2:
		client_put(sd, filename);
		break;
	}

	close(sd);
	return 0;
}