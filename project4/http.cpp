#include <iostream>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h> 
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/wait.h>

using namespace std;

#define PORT 6565
#define BACKLOG 5
#define MAXSIZE 2048

char *query_string=new char[MAXSIZE];
char *request_method=new char[MAXSIZE];
char *script_name=new char[MAXSIZE];

int error_message(string s)
{
	cerr<<"\n"<<s<<" : "<<strerror(errno)<<endl;
		exit(1);
}

int create_socket(int &sockfd)
{
	struct sockaddr_in server_addinfo;

	server_addinfo.sin_family=AF_INET;
	server_addinfo.sin_addr.s_addr=htonl(INADDR_ANY);
	server_addinfo.sin_port=htons(PORT);

	if((sockfd=socket(AF_INET,SOCK_STREAM,0))==-1)
		error_message("socket");

	int yes=1;
	if(setsockopt(sockfd,SOL_SOCKET,SO_REUSEADDR,&yes,sizeof(int))==-1)
		error_message("setsockopt");

	if(bind(sockfd,(struct sockaddr*) &server_addinfo,sizeof(server_addinfo))==-1)
		error_message("bind");

	if(listen(sockfd,BACKLOG)==-1)
		error_message("listen");

	return 0;
}

int string_parser(char *recv_mes)
{
	memset(query_string,'\0',MAXSIZE);
	memset(request_method,'\0',MAXSIZE);
	memset(script_name,'\0',MAXSIZE);

	int index;

	char *p;
	p=strtok(recv_mes," ");
	while(p!=NULL)
	{
		strcpy(request_method,p);

		if(strcmp(request_method,"GET")==0)
		{
			p=strtok(NULL," ");

			if(strchr(p,'?'))
			{
				index=strchr(p,'?')-p;
				strncpy(script_name,p,index);

				strcpy(query_string,p+index+1);
			}
			else
				strcpy(script_name,p);

			break;
		}

		p=strtok(NULL," ");
	}

	return 0;
}

int set_env()
{
	setenv("QUERY_STRING",query_string,1);
	setenv("SCRIPT_NAME",script_name,1);
	setenv("REQUEST_METHOD",request_method,1);

	setenv("CONTENT_LENGTH","0",1);
	setenv("REMOTE_HOST","NCTU",1);
	setenv("REMOTE_ADDR","140.113.94.27",1);
	setenv("ANTH_TYPE","auth_type",1);
	setenv("REMOTE_USER","remote_user",1);
	setenv("REMOTE_IDENT","remote_ident",1);

	return 0;
}

int cgi_exec(int new_sockfd)
{
	int file_fd;
	int recv_byte;
	char *recv_mes=new char[MAXSIZE];
	char *exec_file=new char[MAXSIZE];
	char *p;

	memset(exec_file,'\0',MAXSIZE);

	p=strrchr(script_name,'.');
	if(strcmp(p,".cgi")==0)
	{
		strcat(exec_file,".");
		strcat(exec_file,script_name);
		dup2(new_sockfd,STDOUT_FILENO);
		execlp(exec_file,script_name,NULL);
	}
	else if(strncmp(p,".htm",4)==0)
	{
		strcat(exec_file,".");
		strcat(exec_file,script_name);

		file_fd=open(exec_file,O_RDONLY);

		memset(recv_mes,'\0',MAXSIZE);
		if((recv_byte=read(file_fd,recv_mes,MAXSIZE))==-1)
			error_message("read");
		recv_mes[recv_byte]='\0';

		if(send(new_sockfd,"\r\n",2,0)==-1)
			error_message("send");
		if(send(new_sockfd,recv_mes,strlen(recv_mes),0)==-1)
			error_message("send");
	}

	return 0;
}

int child_work(int new_sockfd)
{
	int recv_byte;
	char *recv_mes=new char[MAXSIZE];

	memset(recv_mes,'\0',MAXSIZE);
	if((recv_byte=recv(new_sockfd,recv_mes,MAXSIZE,0))==-1)
		error_message("recv");

	string_parser(recv_mes);

	set_env();

	if(send(new_sockfd,"HTTP/1.1 200 OK\r\n",17,0)==-1)
		error_message("send");
	if(send(new_sockfd,"Content-Type: text/html\r\n",25,0)==-1)
		error_message("send");

	cgi_exec(new_sockfd);

	return 0;
}

int main()
{
	int sockfd,new_sockfd;
	struct sockaddr_in client_addinfo;

	socklen_t client_addinfo_length;
	client_addinfo_length=sizeof(struct sockaddr_in);

	create_socket(sockfd);

	while(1)
	{
		if((new_sockfd=accept(sockfd,(struct sockaddr*)&client_addinfo,&client_addinfo_length))==-1)
			error_message("accept");

		if(!fork())
		{
			close(sockfd);

			child_work(new_sockfd);

			close(new_sockfd);
			exit(0);
		}
		else
			close(new_sockfd);
	}

	return 0;
}