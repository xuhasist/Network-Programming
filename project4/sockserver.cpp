#include <iostream>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <signal.h>
#include <dirent.h>
#include <unistd.h>
#include <fcntl.h>

using namespace std;

#define PORT 7200
#define PORT2 0
#define BACKLOG 2
#define normal_type 11
#define bind_type 12

#define MAXSIZE 7000
#define SIZE1 1024

char permit_c[3][20];
char permit_b[3][20];
int c=0; //number of permit_c
int b=0;

int error_message(string s)
{
	cerr<<"\n"<<s<<" : "<<strerror(errno)<<endl;
	//exit(1);
}

void sigchld_handler(int s)
{
	while(waitpid(-1,NULL,WNOHANG)>0);
}
int reap_zombie()
{
	struct sigaction sa;

	sa.sa_handler=sigchld_handler;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags=SA_RESTART;

	if(sigaction(SIGCHLD,&sa,NULL)==-1)
		error_message("sigaction");

	return 0;
}

int create_socket(int &sockfd,int type)
{
	struct sockaddr_in server_addinfo;

	server_addinfo.sin_family=AF_INET;
	server_addinfo.sin_addr.s_addr=htonl(INADDR_ANY);

	if(type==normal_type)
		server_addinfo.sin_port=htons(PORT);
	else if(type==bind_type)
		server_addinfo.sin_port=htons(PORT2);

	if((sockfd=socket(AF_INET,SOCK_STREAM,0))==-1)
	{
		error_message("socket");
		return -1;
	}

	int yes=1;
	if(setsockopt(sockfd,SOL_SOCKET,SO_REUSEADDR,&yes,sizeof(int))==-1)
		error_message("setsockopt");

	if(bind(sockfd,(struct sockaddr*) &server_addinfo,sizeof(server_addinfo))==-1)
		error_message("bind");

	if(listen(sockfd,BACKLOG)==-1)
		error_message("listen");

	return 0;
}

int read_firewall(int file_fd)
{
	int recv_byte;
	char *recv_mes=new char[SIZE1];

	memset(recv_mes,'\0',SIZE1);
	if((recv_byte=read(file_fd,recv_mes,SIZE1))==-1)
		error_message("read");
	recv_mes[recv_byte]='\0';

	char *p;
	p=strtok(recv_mes," -\r\n\0"); 
	while(p!=NULL)
	{
		if(strcmp(p,"c")==0)
		{
			p=strtok(NULL," -\r\n\0");
			if(p!=NULL && strcmp(p,"permit")!=0)
			{
				memset(permit_c[c],'\0',20);
				strcpy(permit_c[c],p);
				c++;
			}
		}
		else if(strcmp(p,"b")==0)
		{
			p=strtok(NULL," -\r\n\0");
			if(p!=NULL && strcmp(p,"permit")!=0)
			{
				memset(permit_b[b],'\0',20);
				strcpy(permit_b[b],p);
				b++;
			}
		}

		p=strtok(NULL," -\r\n\0");
	}

	return 0;
}

bool connect_success(int CD,char *DST_IP)
{
	if(CD==1)
	{
		if(c==0)
			return true;

		for(int i=0;i<c;i++)
		{
			if(strncmp(DST_IP,permit_c[i],strlen(permit_c[i]))==0) 
				return true;
		}
	}
	else if(CD==2)
	{
		if(b==0)
			return true;

		for(int i=0;i<b;i++)
		{
			if(strncmp(DST_IP,permit_b[i],strlen(permit_b[i]))==0) 
				return true;
		}
	}

	return false;
}

int select_(int fd1,int fd2)
{
	int recv_byte;
	unsigned char *recv_mes=new unsigned char[MAXSIZE];

	fd_set rfds,rs;
	int nfds;
	struct timeval tv;

	bool fd1_exist=true;
	bool fd2_exist=true;

	FD_ZERO(&rfds);
	FD_ZERO(&rs);

	FD_SET(fd1,&rs);
	FD_SET(fd2,&rs);

	if(fd1>nfds)
		nfds=fd1;
	if(fd2>nfds)
		nfds=fd2;

	while(fd1_exist==true && fd2_exist==true)
	{
		memcpy(&rfds,&rs,sizeof(rfds));

		tv.tv_sec = 0;
		tv.tv_usec = 1000;

		select(nfds+1,&rfds,NULL,NULL,&tv);

		if(FD_ISSET(fd1,&rfds)) //cgi //ftp client
		{
			memset(recv_mes,'\0',MAXSIZE);
			recv_byte=recv(fd1,recv_mes,MAXSIZE,0);

			if(recv_byte>0)
				send(fd2,recv_mes,recv_byte,0);
			else if(recv_byte<=0)
				fd1_exist=false;
		}
		if(FD_ISSET(fd2,&rfds)) //ras //ftp server
		{
			memset(recv_mes,'\0',MAXSIZE);
			recv_byte=recv(fd2,recv_mes,MAXSIZE,0);

			if(recv_byte>0)
				send(fd1,recv_mes,recv_byte,0);
			else if(recv_byte<=0)
				fd2_exist=false;
		}
	}

	return 0;
}

int connect_(int new_sockfd,unsigned char *reply_mes,unsigned int DST_PORT,char *DST_IP)
{
	int sockfd;
	struct sockaddr_in dest_addinfo;
	struct hostent *he;

	sockfd=socket(AF_INET,SOCK_STREAM,0);

	he=gethostbyname(DST_IP);

	memset(&dest_addinfo,'\0',sizeof(dest_addinfo)); 
	dest_addinfo.sin_family=AF_INET;
	dest_addinfo.sin_addr=*((struct in_addr*)he->h_addr);
	dest_addinfo.sin_port=htons(DST_PORT);

	int rv=connect(sockfd,(struct sockaddr*)&dest_addinfo,sizeof(dest_addinfo));
	if(rv==-1)
	{
		cout<<"SOCKS_CONNECT REJECT ....\n";
		reply_mes[1]=91;
		send(new_sockfd,reply_mes,8,0);
		return 0;
	}
	else
	{
		cout<<"SOCKS_CONNECT GRANTED ....\n";
		send(new_sockfd,reply_mes,8,0);
	}

	select_(new_sockfd,sockfd);

	return 0;
}

int bind_(int new_sockfd,unsigned char *reply_mes,unsigned int DST_PORT,char *DST_IP)
{
	int sockfd1,sockfd2;
	struct sockaddr_in client_addinfo, server_addinfo;

	socklen_t client_addinfo_length;
	client_addinfo_length=sizeof(struct sockaddr_in);

	int rv=create_socket(sockfd1,bind_type);

	getsockname(sockfd1,(struct sockaddr*)&server_addinfo,&client_addinfo_length);
	cout << "server_addrinfo: " << ntohs(server_addinfo.sin_port) << endl;
	reply_mes[2]=ntohs(server_addinfo.sin_port)/256;
	reply_mes[3]=ntohs(server_addinfo.sin_port)%256;

	if(rv<0)
	{
		cout<<"1.SOCKS_BIND REJECT ....\n";
		reply_mes[1]=91;
		send(new_sockfd,reply_mes,8,0);
		return 0;
	}
	else
	{
		cout<<"1.SOCKS_BIND GRANTED ....\n";
		send(new_sockfd,reply_mes,8,0);
	}

	if((sockfd2=accept(sockfd1,(struct sockaddr*)&client_addinfo,&client_addinfo_length))<0)
	{
		cout<<"2.SOCKS_BIND REJECT ....\n";
		reply_mes[1]=91;
		send(new_sockfd,reply_mes,8,0);
		return 0;
	}
	else
	{
		cout<<"2.SOCKS_BIND GRANTED ....\n";
		send(new_sockfd,reply_mes,8,0);
	}

	select_(new_sockfd,sockfd2);

	return 0;
}

int child_work(int new_sockfd)
{
	int recv_byte;
	unsigned char *recv_mes=new unsigned char[MAXSIZE];

	int VN;
	int CD;
	unsigned int DST_PORT;
	char *DST_IP=new char[30];
	unsigned char* USER_ID;
	unsigned char* DOMAIN_NAME;

	unsigned char *reply_mes=new unsigned char[9];
	struct hostent *he;

	memset(recv_mes,'\0',MAXSIZE);
	recv_byte=recv(new_sockfd,recv_mes,MAXSIZE,0);
	recv_mes[recv_byte]='\0';

	VN=(int)recv_mes[0];
	CD=(int)recv_mes[1];
	DST_PORT=recv_mes[2]*256+recv_mes[3];

	memset(DST_IP,'\0',30);
	sprintf(DST_IP,"%u.%u.%u.%u",(int)recv_mes[4],(int)recv_mes[5],(int)recv_mes[6],(int)recv_mes[7]);

	if(strncmp(DST_IP,"0.0.0.",6)==0)
	{
		DOMAIN_NAME=recv_mes+8;
		while(DOMAIN_NAME[0]!='\0')
			DOMAIN_NAME=DOMAIN_NAME+1;
		DOMAIN_NAME=DOMAIN_NAME+1;

		he=gethostbyname((const char*)DOMAIN_NAME);
		DST_IP=inet_ntoa(*((struct in_addr*)he->h_addr));
	}
	
	USER_ID=recv_mes+8;

	cout<<"\nVN: "<<VN<<", CD: "<<CD<<", DST_IP: "<<DST_IP<<", DST_PORT: "<<DST_PORT<<", USERID: "<<USER_ID<<endl;

	memset(reply_mes,'\0',9);
	reply_mes[0]=0;

	bool rv=connect_success(CD,DST_IP);
	if(rv==true)
		reply_mes[1]=90;
	else if(rv==false)
		reply_mes[1]=91;

	reply_mes[2]=recv_mes[2];
	reply_mes[3]=recv_mes[3];
	reply_mes[4]=recv_mes[4];
	reply_mes[5]=recv_mes[5];
	reply_mes[6]=recv_mes[6];
	reply_mes[7]=recv_mes[7];

	if(reply_mes[1]==91)
	{
		cout<<"SOCKS_CONNECT REJECT ....\n";
		send(new_sockfd,reply_mes,8,0);
	}
	else if(reply_mes[1]==90 && CD==1)
		connect_(new_sockfd,reply_mes,DST_PORT,DST_IP);
	else if(reply_mes[1]==90 && CD==2)
	{
		reply_mes[4]=0;
		reply_mes[5]=0;
		reply_mes[6]=0;
		reply_mes[7]=0;
		bind_(new_sockfd,reply_mes,DST_PORT,DST_IP);
	}

	return 0;
}

int main()
{
	int file_fd;
	int sockfd,new_sockfd;
	struct sockaddr_in client_addinfo;

	socklen_t client_addinfo_length;
	client_addinfo_length=sizeof(struct sockaddr_in);
	
	create_socket(sockfd,normal_type);
	reap_zombie();

	file_fd=open("socks.conf",O_RDONLY);
	read_firewall(file_fd);

	while(1)
	{
		if((new_sockfd=accept(sockfd,(struct sockaddr*)&client_addinfo,&client_addinfo_length))==-1)
			error_message("accept");

		if(!fork())
		{
			close(sockfd);

			cout<<"\nSrc = "<<inet_ntoa(client_addinfo.sin_addr)<<"("<<client_addinfo.sin_port<<")";

			child_work(new_sockfd);

			close(new_sockfd);
			exit(0);
		}
		else
			close(new_sockfd);
	}

	return 0;
}