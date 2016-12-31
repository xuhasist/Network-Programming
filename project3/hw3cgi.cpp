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

using namespace std;

#define MAXSIZE 7000

#define SERVER_NUM 5

#define F_CONNECTING 6
#define F_READING 7
#define F_WRITING 8
#define F_DONE 9
#define EXIT 10

bool batch_exist[SERVER_NUM];
char server_host[SERVER_NUM][30];
char server_port[SERVER_NUM][6];
char batch_file[SERVER_NUM][20];
int batch_socket[SERVER_NUM];
int file_fd[SERVER_NUM];
int file_offset[SERVER_NUM];
int status[SERVER_NUM];
int lines[SERVER_NUM];

int batch_number;

int error_message(string s)
{
	int fd=open("QQQQ",O_WRONLY | O_CREAT | O_TRUNC,0666);
	dup2(fd,2);
	cerr<<s<<" : "<<strerror(errno)<<"\n";
	//cerr<<s;
	//exit(1);

	return 0;
}

int query_string_parser(char *query_string)
{
	int id=-1;

	char *p;
	p=strtok(query_string,"&"); //h1=140.113.210.101&p1=7000&f1=batch_file1
	while(p!=NULL)
	{
		id=((int)p[1])-48-1; //0's ascii code //從0

		if(p[0]=='h' && p[3]!='\0')  //h1=140.113.210.101
		{
			batch_exist[id]=true;
			strcpy(server_host[id],p+3);
		}
		else if(p[0]=='p' && p[3]!='\0') //p1=7000
			strcpy(server_port[id],p+3);
		else if(p[0]=='f' && p[3]!='\0') //f1=batch_file1
			strcpy(batch_file[id],p+3);

		p=strtok(NULL,"&");
	}

	return 0;
}

int connect_server(int position)
{
	int sockfd;
	struct sockaddr_in dest_addinfo;
	struct hostent *he;

	sockfd=socket(AF_INET,SOCK_STREAM,0);

	batch_socket[position]=sockfd;

	he=gethostbyname(server_host[position]);

	memset(&dest_addinfo,0,sizeof(dest_addinfo)); 
	dest_addinfo.sin_family=AF_INET;
	dest_addinfo.sin_addr=*((struct in_addr*)he->h_addr);
	dest_addinfo.sin_port=htons(atoi(server_port[position]));

	int flag=fcntl(sockfd,F_GETFL,NULL);
	if(flag==-1)
		flag=0;
	fcntl(sockfd,F_SETFL,flag | O_NONBLOCK);

	if(connect(sockfd,(struct sockaddr*)&dest_addinfo,sizeof(dest_addinfo))==-1);
	batch_number++;

	return 0;
}

int recv_input(int sockfd,char *recv_mes)
{
	int recv_bytes;
	char *dup_recv_mes=new char[MAXSIZE];

	memset(recv_mes,'\0',MAXSIZE);
	memset(dup_recv_mes,'\0',MAXSIZE);

	recv_bytes=read(sockfd,recv_mes,1);

	if(recv_bytes==0)
		return EXIT;
	else if(recv_bytes>0)
	{
		strcpy(dup_recv_mes,recv_mes);

		if(recv_mes[0]=='%')
		{
			memset(recv_mes,'\0',MAXSIZE);
			recv_bytes=read(sockfd,recv_mes,1);

			strcat(dup_recv_mes,recv_mes);
		}
		else if(dup_recv_mes[recv_bytes-1]!='\n')
		{
			do
			{
				memset(recv_mes,'\0',MAXSIZE);
				recv_bytes=read(sockfd,recv_mes,1);

				strcat(dup_recv_mes,recv_mes);

			}while(dup_recv_mes[strlen(dup_recv_mes)-1]!='\n');
		}

		memset(recv_mes,'\0',MAXSIZE);
		strcpy(recv_mes,dup_recv_mes);
	}

	return recv_bytes;
}

int select_server()
{
	int recv_byte;
	char *recv_mes=new char[MAXSIZE];
	char *a=new char[MAXSIZE];

	fd_set rfds,wfds,rs,ws;
	int nfds=0;
	struct timeval tv;

	FD_ZERO(&rfds);
	FD_ZERO(&wfds);
	FD_ZERO(&rs);
	FD_ZERO(&ws);

	for(int i=0;i<SERVER_NUM;i++)
	{
		if(batch_exist[i]==true)
		{
			status[i]=F_CONNECTING;

			FD_SET(batch_socket[i],&rs);
			FD_SET(batch_socket[i],&ws);

			if(batch_socket[i]>nfds)
				nfds=batch_socket[i];
		}
	}

	while(batch_number>0)
	{
		memcpy(&rfds,&rs,sizeof(rfds));
		memcpy(&wfds,&ws,sizeof(wfds));

		tv.tv_sec = 0;
		tv.tv_usec = 1000;

		select(nfds+1,&rfds,&wfds,NULL,&tv);

		for(int i=0;i<SERVER_NUM;i++)
		{
			if(status[i]==F_CONNECTING && (FD_ISSET(batch_socket[i],&rfds) || FD_ISSET(batch_socket[i],&wfds)))
			{
				int error=0;
				socklen_t n = sizeof(int);
				if(getsockopt(batch_socket[i],SOL_SOCKET,SO_ERROR,&error,&n)<0 || error!=0)
					error_message("getsockopt");

				status[i]=F_READING;
				FD_CLR(status[i],&rs);
			}
			else if(status[i]==F_WRITING && FD_ISSET(batch_socket[i],&wfds)) //來自檔案 寫給server
			{
				recv_byte=recv_input(file_fd[i],recv_mes);
				//error_message(recv_mes);

				if(recv_byte>0)
				{
					/*if(strncmp(recv_mes,"exit",4)==0)
					{
						recv_byte=recv_input(batch_socket[i],a);
						if(recv_byte>0)
						{
							cout<<"<script>document.all['m"<<i<<"'].innerHTML += \"";

							for(int j=0;a[j]!='\0';j++)
							{						
								if(a[j]=='"')
									cout<<"&quot;";
								else if(a[j]=='\'')
									cout<<"&apos;";
								else if(a[j]==' ')
									cout<<"&nbsp;";
								else if(a[j]=='<')
									cout<<"&lt;";
								else if(a[j]=='>')
									cout<<"&gt;";
								else if(a[j]=='\n')
									cout<<"<br>";
								else if(a[j]=='\r')
									continue;
								else
									cout<<a[j];
							}
							cout<<"\";</script>\n";
							fflush(stdout);
						}
					}*/

					send(batch_socket[i],recv_mes,strlen(recv_mes),0);
					//sleep(1);

					//lines[i]++;
					cout<<"<script>document.all['m"<<i<<"'].innerHTML += \"";
					//cout<<lines[i]<<"&nbsp;";

					for(int j=0;recv_mes[j]!='\0';j++)
					{
						if(recv_mes[j]=='"')
							cout<<"<b>&quot;</b>";
						else if(recv_mes[j]=='\'')
							cout<<"<b>&apos;</b>";
						else if(recv_mes[j]==' ')
							cout<<"&nbsp;";
						else if(recv_mes[j]=='<')
							cout<<"<b>&lt;</b>";
						else if(recv_mes[j]=='>')
							cout<<"<b>&gt;</b>";
						else if(recv_mes[j]=='\n')
							cout<<"<br>";
						else if(recv_mes[j]=='\r')
							continue;
						else
							cout<<"<b>"<<recv_mes[j]<<"</b>";
					}
					cout<<"\";</script>\n";
					fflush(stdout);

					FD_CLR(batch_socket[i],&ws);
					status[i]=F_READING;
					FD_SET(batch_socket[i],&rs);
				}
			}
			else if(status[i]==F_READING && FD_ISSET(batch_socket[i],&rfds)) //來自server
			{
				recv_byte=recv_input(batch_socket[i],recv_mes);
				//error_message(recv_mes);

				if(recv_byte==EXIT)
				{
					FD_CLR(batch_socket[i],&rs);
					status[i]=F_DONE;
					batch_number--;
				}
				else if(recv_byte>0)
				{
					lines[i]++;
					cout<<"<script>document.all['m"<<i<<"'].innerHTML += \"";
					cout<<lines[i]<<"&nbsp;";

					for(int j=0;recv_mes[j]!='\0';j++)
					{						
						if(recv_mes[j]=='"')
							cout<<"&quot;";
						else if(recv_mes[j]=='\'')
							cout<<"&apos;";
						else if(recv_mes[j]==' ')
							cout<<"&nbsp;";
						else if(recv_mes[j]=='<')
							cout<<"&lt;";
						else if(recv_mes[j]=='>')
							cout<<"&gt;";
						else if(recv_mes[j]=='\n')
							cout<<"<br>";
						else if(recv_mes[j]=='\r')
							continue;
						else
							cout<<recv_mes[j];
					}
					cout<<"\";</script>\n";
					fflush(stdout);

					if(recv_mes[0]=='%')
					{
						FD_CLR(batch_socket[i],&rs);
						status[i]=F_WRITING;
						FD_SET(batch_socket[i],&ws);
					}
				}
			}
		}
	}

	return 0;
}

int initial()
{
	for(int i=0;i<SERVER_NUM;i++)
	{
		batch_exist[i]=false;
		memset(server_host[i],'\0',30);
		memset(server_port[i],'\0',6);
		memset(batch_file[i],'\0',20);
		batch_socket[i]=-1;
		file_fd[i]=-1;
		file_offset[i]=-1;
		status[i]=-1;
		batch_number=0;
		lines[i]=0;
	}

	return 0;
}

int main()
{
	initial();

	char *query_string=new char[100];

	cout<<"Content-type: text/html\n\n";

	cout<<"<html>\n";
	cout<<"<head>\n";
	cout<<"<meta http-equiv=\"Content-Type\" content=\"text/html; charset=big5\" />\n";
	cout<<"<title>Network Programming Homework 3</title>\n";
	cout<<"</head>\n";
	cout<<"<body bgcolor=#336699>\n";
	cout<<"<font face=\"Courier New\" size=2 color=#FFFF99>\n";
	cout<<"<table width=\"800\" border=\"1\">\n";
	cout<<"<tr>\n";

	memset(query_string,'\0',100);
	query_string=getenv("QUERY_STRING");

	query_string_parser(query_string);

	for(int i=0;i<SERVER_NUM;i++)
	{
		if(batch_exist[i]==true) 
			cout<<"<td>"<<server_host[i]<<"</td>";
	}

	cout<<"</tr>\n";
	cout<<"<tr>\n";

	for(int i=0;i<SERVER_NUM;i++)
	{
		if(batch_exist[i]==true) 
			cout<<"<td valign=\"top\" id=\"m"<<i<<"\"></td>";
	}

	cout<<"</tr>\n";
	cout<<"</table>\n";
	fflush(stdout);

	for(int i=0;i<SERVER_NUM;i++)
	{
		if(batch_exist[i]==true) 
		{
			connect_server(i);
			file_fd[i]=open(batch_file[i],O_RDONLY);
		}
	}

	select_server();

	cout<<"</font>\n";
	cout<<"</body>\n";
	cout<<"</html>\n";
	fflush(stdout); 

	return 0;
}



