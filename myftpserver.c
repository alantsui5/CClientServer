#include "myftp.h"
#include <stddef.h>
#include <pthread.h>

//Global Datas
pthread_t tid[no_of_threads];
int tid_i = 0;
int client_sd[no_of_threads];
int global[no_of_threads];

void display_header(struct message_s header)
{
	//Display header information
	if (check_myftp(header.protocol) == 0)
	{
		printf("This is MYFTP header\n");
	}
	printf("  Protocol : ");
	int i;
	for (i = 0; i < sizeof(header.protocol); i++)
	{
		printf("%c", header.protocol[i]);
	}
	printf("\n");
	printf("  Type	   : %x\n", header.type);
	printf("  Length   : %d bytes\n", header.length);
}

void message_to_client(int clientsd, struct message_s m_header, char *payload, int payload_length)
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
	if (sendn(clientsd, send_message, m_header.length) < 0)
	{
		printf("Send error: %s (Errno:%d)\n", strerror(errno), errno);
		exit(0);
	}
	free(send_message);
}

void server_list(int clientsd, struct packet recv_packet)
{
	struct message_s list_reply = {.protocol = {'m', 'y', 'f', 't', 'p'}, .type = 0xA2};
	char payload[1024];
	memset(payload, 0, 1);

	DIR *dirp;
	if ((dirp = opendir("data")) == NULL)
	{
		perror("Open directory Error");
		exit(0);
	};

	struct dirent *entry;
	while ((entry = readdir(dirp)))
	{
		printf("%s\n", entry->d_name);
		strcat(payload, entry->d_name);
		strcat(payload, "\n");
	}
	closedir(dirp);
	list_reply.length = 10 + strlen(payload);
	message_to_client(clientsd, list_reply, payload, strlen(payload));
}

void server_get(int clientsd, struct packet recv_packet)
{
	//Check File Existance
	char path[256] = "./data/";
	strcat(path, recv_packet.payload);
	if (access(path, F_OK) != -1)
	{
		printf("[%s] Exist.\n", recv_packet.payload);

		//Construct GET Reply
		struct message_s get_reply;
		memcpy(get_reply.protocol, (unsigned char[]){'m', 'y', 'f', 't', 'p'}, 5);
		get_reply.type = 0xB2;
		get_reply.length = sizeof(struct message_s);
		message_to_client(clientsd, get_reply, NULL, 0);

		//Open File
		FILE *fptr = fopen(path, "rb");
		if (fptr == NULL)
		{
			printf("file open error: %s (Errno:%d)\n", (char *)strerror(errno), errno);
			return;
		}

		//Get File Size
		fseek(fptr, 0, SEEK_END);
		int filesize = ftell(fptr);
		rewind(fptr);

		//Send File
		char *buffer = malloc(sizeof(char) * filesize);
		printf("File Size To Send %lu\n", fread(buffer, sizeof(char), filesize, fptr));
		struct message_s file_data;
		memcpy(file_data.protocol, (unsigned char[]){'m', 'y', 'f', 't', 'p'}, 5);
		file_data.type = 0xFF;
		file_data.length = sizeof(struct message_s) + filesize;
		message_to_client(clientsd, file_data, buffer, filesize);
		free(buffer);
		fclose(fptr);
		printf("File Transfer Completed.\n");
	}
	else
	{
		printf("%s Does Not Exist.\n", recv_packet.payload);

		//Construct GET Reply
		struct message_s get_reply;
		memcpy(get_reply.protocol, (unsigned char[]){'m', 'y', 'f', 't', 'p'}, 5);
		get_reply.type = 0xB3;
		get_reply.length = sizeof(struct message_s);
		message_to_client(clientsd, get_reply, NULL, 0);
	}
}

void server_put(int clientsd, struct packet recv_packet)
{
	char path[256] = "./data/";
	strcat(path, recv_packet.payload);
	//Construct Put Reply Message
	struct message_s put_reply;
	memcpy(put_reply.protocol, (unsigned char[]){'m', 'y', 'f', 't', 'p'}, 5);
	put_reply.type = 0xC2;
	put_reply.length = sizeof(struct message_s);
	message_to_client(clientsd, put_reply, NULL, 0);

	//Receive File and write to disk
	struct packet file_data;
	int file_data_len;
	if ((file_data_len = recvn(clientsd, &file_data, sizeof(struct message_s))) < 0)
	{
		printf("Send Error: %s (Errno:%d)\n", strerror(errno), errno);
		return;
	}
	if (file_data_len == 0)
	{
		printf("0 Packet Received\n");
		return;
	}

	//Validate Message
	if (check_myftp(file_data.header.protocol) < 0)
	{
		printf("Invalid Protocol\n");
		return;
	}

	if (file_data.header.type != 0xFF)
	{
		printf("Invalid Message Type\n");
		return;
	}

	FILE *fptr = fopen(path, "w");

	int transfered_data_len = 0;

	file_data.header.length = ntohl(file_data.header.length);

	if (file_data.header.length > 10)
	{
		printf("File size received : %d\n", file_data.header.length - 10);
		char payload[Buffer_Size + 1];
		while (1)
		{
			memset(&payload, 0, Buffer_Size + 1);
			if ((file_data_len = recv(clientsd, &payload, Buffer_Size, 0)) < 0)
			{
				printf("Send error: %s (Errno:%d)\n", strerror(errno), errno);
				break;
			}
			fwrite(payload, 1, file_data_len, fptr);
			transfered_data_len += file_data_len;
			if (file_data.header.length - 10 <= transfered_data_len)
				break;
		}
	}
	fclose(fptr);
	printf("(%s) Upload Completed.\n", recv_packet.payload);
}

void *recv_message(void *input)
{
	int len;
	struct packet *recv_packet;
	recv_packet = malloc(sizeof(char) * 10);
	while (1)
	{
		int totalsize = 0;
		if ((len = recvn(client_sd[*((int *)input)], &recv_packet->header, 10)) < 0)
		{
			printf("Send error: %s (Errno:%d)\n", strerror(errno), errno);
			pthread_exit(NULL);
		}
		if (len == 0)
			pthread_exit(NULL);

		recv_packet->header.length = ntohl(recv_packet->header.length);

		if (recv_packet->header.length > 10)
		{
			char recbuf[Buffer_Size];
			recv_packet = realloc(recv_packet, sizeof(char) * (recv_packet->header.length));
			while (1)
			{
				memset(&recbuf, 0, Buffer_Size);
				if ((len = recv(client_sd[*((int *)input)], &recbuf, Buffer_Size, 0)) < 0)
				{
					break;
				}
				if (totalsize == 0)
					strcpy(recv_packet->payload, recbuf);
				else
					strcat(recv_packet->payload, recbuf);
				totalsize += len;
				if (recv_packet->header.length - 10 <= totalsize)
					break;
			}
		}
		else
		{
			//printf("No Payload");
		}

		//Validate Message
		if (check_myftp(recv_packet->header.protocol) < 0)
		{
			printf("Invalid Protocol\n");
			break;
		}

		if ((unsigned char)recv_packet->header.type == 0xA1)
		{
			server_list(client_sd[*((int *)input)], *recv_packet);
		}
		else if ((unsigned char)recv_packet->header.type == 0xB1)
		{
			server_get(client_sd[*((int *)input)], *recv_packet);
		}
		else if ((unsigned char)recv_packet->header.type == 0xC1)
		{
			server_put(client_sd[*((int *)input)], *recv_packet);
		}
	}
	free(recv_packet);
}

int main(int argc, char **argv)
{
	if (argc != 2)
	{
		printf("Usage: ./myftpserver PORT_NUMBER\n");
		exit(0);
	}

	int port = atoi(argv[1]);

	int sd = socket(AF_INET, SOCK_STREAM, 0);
	//Reusable port
	long val = 1;
	if (setsockopt(sd, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(long)) == -1)
	{
		perror("setsockopt");
		exit(1);
	}
	//int client_sd;
	struct sockaddr_in server_addr;
	memset(&server_addr, 0, sizeof(server_addr));
	server_addr.sin_family = AF_INET;
	server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	server_addr.sin_port = htons(port);
	if (bind(sd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
	{
		printf("bind error: %s (Errno:%d)\n", strerror(errno), errno);
		exit(0);
	}
	if (listen(sd, 10) < 0)
	{
		printf("listen error: %s (Errno:%d)\n", strerror(errno), errno);
		exit(0);
	}
	while (1)
	{
		if (tid_i == no_of_threads)
		{
			int j;
			for (j = 0; j < no_of_threads; j++)
				pthread_join(tid[j], NULL);
			tid_i = 0;
		}
		struct sockaddr_in client_addr;
		int addr_len = sizeof(client_addr);
		if ((client_sd[tid_i] = accept(sd, (struct sockaddr *)&client_addr, &addr_len)) < 0)
		{
			printf("accept error: %s (Errno:%d)\n", (char *)strerror(errno), errno);
			exit(0);
		}
		global[tid_i] = tid_i;
		pthread_create(&tid[tid_i], NULL, recv_message, &(global[tid_i]));
		tid_i++;
	}
	return 0;
}
