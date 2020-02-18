#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <stddef.h>
#include <dirent.h>

#define PORT 12345

struct message_s
{
	unsigned char protocol[5]; /* protocol string (5 bytes) */
	unsigned char type;		   /* type (1 byte) */
	unsigned int length;	   /* length (header + payload) (4 bytes) */
} __attribute__((packed));

struct packet
{
	struct message_s header;
	char payload[1024];
} __attribute__((packed));

//Ensure complete data transfer
int sendn(int sd, void *buf, int buf_len)
{
	int n_left = buf_len;
	int n;
	while (n_left > 0)
	{
		if ((n = send(sd, buf + (buf_len - n_left), n_left, 0)) < 0)
		{
			if (errno == EINTR)
				n = 0;
			else
				return -1;
		}
		else if (n == 0)
		{
			return 0;
		}
		n_left -= n;
	}
	return buf_len;
}

int recvn(int sd, void *buf, int buf_len)
{
	int n_left = buf_len;
	int n;
	while (n_left > 0)
	{
		if ((n = recv(sd, buf + (buf_len - n_left), n_left, 0)) < 0)
		{
			if (errno == EINTR)
				n = 0;
			else
				return -1;
		}
		else if (n == 0)
		{
			return 0;
		}
		n_left -= n;
	}
	return buf_len;
}

int check_myftp(unsigned char ptc[]){
	if( (ptc[0] != 'm') || (ptc[1] != 'y') || (ptc[2] != 'f') || (ptc[3] != 't')|| (ptc[4] != 'p')){
		return -1;
	}
	return 0;
}

void display_header(struct message_s header){
	//Display header information
	if(check_myftp(header.protocol) == 0){
		printf("This is MYFTP header\n");
	}
	printf("  Protocol : ");
	for(int i = 0; i < sizeof(header.protocol); i++){
		printf("%c", header.protocol[i]);
	}
	printf("\n");
	printf("  Type	   : %x\n", header.type);
	printf("  Length   : %d bytes\n", header.length);
}

//Added List function
void MESSAGE_TO_CLIENT(int clientsd,struct message_s Header,char * payload){
	struct packet* SEND_MESSAGE;
	SEND_MESSAGE = malloc(sizeof(struct message_s));
	SEND_MESSAGE->header=Header;
	if(payload!=NULL){
		SEND_MESSAGE = realloc(SEND_MESSAGE,10+strlen(payload));
		memcpy(SEND_MESSAGE->payload,payload,Header.length-12);
	}
	if(sendn(clientsd,SEND_MESSAGE, Header.length) < 0){
		printf("Send error: %s (Errno:%d)\n",strerror(errno),errno);
		exit(0);
	}	
	free(SEND_MESSAGE);
}

void LIST(int clientsd , struct packet RECEIVE_MESSAGE){
	printf("110");
	
	struct message_s LS_REPLY={.protocol={'m','y','f','t','p'},.type=0xA2};
	char payload[1024];
	memset(payload,0,1);

	DIR * dirp = opendir("data");
	if((dirp = opendir("data"))== NULL){
		perror("Open directory Error");
	}
	printf("Directory Stream is now open");
	struct dirent * entry;
	int i = 0;
	while((entry=readdir(dirp))){
		i++;
		printf("\n%s",entry->d_name);
		strcat(payload,entry->d_name);
		strcat(payload,"\n");
	}
	closedir(dirp);
	printf("\n\n readdir() found a total of %i files\n",i);
	printf("The payload is %s",payload);
	/*
	struct dirent * entry;
	struct dirent * result;
	int len_entry;
	len_entry = offsetof(struct dirent, d_name)+fpathconf(dirfd(dirp),_PC_NAME_MAX)+1;
	entry = malloc(len_entry);
	if(!entry)
		exit(0);
	
	while(1){
		printf("Before read");
		readdir_r(dirp,entry,&result);
		printf("After read");
		break;
		/*
		if(!result)
			break;
		else{
			if((strcmp(result->d_name,".")!=0) && (strcmp(result->d_name,"..")!=0)){
				strcat(payload,result->d_name);
				strcat(payload,"\n");
			}
		}
		
	}

	
	free(entry);
	closedir(dirp);
	*/
	LS_REPLY.length = 10 +strlen(payload)+1;
	MESSAGE_TO_CLIENT(clientsd,LS_REPLY,payload);
	
}
//List



int main(int argc, char **argv)
{
	int sd = socket(AF_INET, SOCK_STREAM, 0);
	//Reusable port
	long val = 1;
	if (setsockopt(sd, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(long)) == -1)
	{
		perror("setsockopt");
		exit(1);
	}
	int client_sd;
	struct sockaddr_in server_addr;
	struct sockaddr_in client_addr;
	memset(&server_addr, 0, sizeof(server_addr));
	server_addr.sin_family = AF_INET;
	server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	server_addr.sin_port = htons(PORT);
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
	int addr_len = sizeof(client_addr);
	if ((client_sd = accept(sd, (struct sockaddr *)&client_addr, &addr_len)) < 0)
	{
		printf("accept erro: %s (Errno:%d)\n", strerror(errno), errno);
		exit(0);
	}

	struct packet request_packet;
	int len;
	//Receive Request
	if ((len = recvn(client_sd, &request_packet.header, sizeof(struct message_s))) < 0)
	{
		printf("receive error: %s (Errno:%d)\n", strerror(errno), errno);
		exit(0);
	}
	printf("RECEIVED INFO: \n");
	display_header(request_packet.header);
	/* my Code*/
	if((unsigned char)request_packet.header.type==0xA1){
		printf("The listing function starts\n");

		//srequest_packet = realloc(request_packet,sizeof(char) * (request_packet.header.length))
		LIST(client_sd,request_packet);
	} else {
		printf("There is an error");
	}
	/* my Code */
	
	// if (strlen(buff) != 0)
	// 	printf("%s\n", buff);

	// if (strcmp("exit", buff) == 0)
	// {
	// 	close(client_sd);
	// }
	

	close(sd);
	return 0;
}




