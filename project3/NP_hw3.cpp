#include <iostream>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <fstream> 
#include <windows.h>
#include <list>

using namespace std;

#include "resource.h"

#define SERVER_PORT 7799

#define WM_SOCKET_NOTIFY (WM_USER + 1)
#define WM_SOCKET_SELECT (WM_USER + 2)

BOOL CALLBACK MainDlgProc(HWND, UINT, WPARAM, LPARAM);
int EditPrintf (HWND, TCHAR *, ...);
//=================================================================
//	Global Variables
//=================================================================
list<SOCKET> Socks;

static HWND hwndEdit;

#define MAXSIZE 6500
#define STRING_SIZE 1024
#define SERVER_NUM 5

#define F_CONNECTING 6
#define F_READING 7
#define F_WRITING 8
#define F_DONE 9

#define EXIT 10

char *query_string=new char[STRING_SIZE];
char *request_method=new char[STRING_SIZE];
char *script_name=new char[STRING_SIZE];

bool batch_exist[SERVER_NUM];
char server_host[SERVER_NUM][30];
char server_port[SERVER_NUM][6];
char batch_file[SERVER_NUM][20];
int batch_socket[SERVER_NUM];
FILE *file_fd[SERVER_NUM];

bool ready_exit[SERVER_NUM];
int batch_number;


int error_message(char *s)
{
	EditPrintf(hwndEdit, TEXT("%s : "),s);
	EditPrintf(hwndEdit, TEXT("%s\r\n"),strerror(errno));

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
		batch_number=0;
		ready_exit[i]=false;
	}

	return 0;
}

int recv_input(FILE *stream,char *recv_mes)
{
	int recv_byte;
	char *dup_recv_mes=new char[MAXSIZE];

	memset(recv_mes,'\0',MAXSIZE);
	memset(dup_recv_mes,'\0',MAXSIZE);

	recv_byte=fread(recv_mes,1,1,stream);
	recv_mes[1]='\0';

	strcpy(dup_recv_mes,recv_mes);

	if(recv_mes[0]=='%')
	{
		memset(recv_mes,'\0',MAXSIZE);

		recv_byte=fread(recv_mes,1,1,stream);
		recv_mes[1]='\0';

		strcat(dup_recv_mes,recv_mes);
	}
	else if(dup_recv_mes[recv_byte-1]!='\n' && !feof(stream))
	{
		do
		{
			memset(recv_mes,'\0',MAXSIZE);

			recv_byte=fread(recv_mes,1,1,stream);
			recv_mes[1]='\0';

			strcat(dup_recv_mes,recv_mes);

		}while(dup_recv_mes[strlen(dup_recv_mes)-1]!='\n' && !feof(stream));
	}

	memset(recv_mes,'\0',MAXSIZE);
	strcpy(recv_mes,dup_recv_mes);

	char a[11];
	memset(a,'\0',11);
	strncpy(a,dup_recv_mes,10);

	return 0;
}

int recv_message(int sockfd,char *recv_mes)
{
	int recv_bytes;
	char *dup_recv_mes=new char[MAXSIZE];

	memset(recv_mes,'\0',MAXSIZE);
	memset(dup_recv_mes,'\0',MAXSIZE);

	if((recv_bytes=recv(sockfd,recv_mes,1,0))==-1)
		recv_mes[0]='\0';
	recv_mes[1]='\0';

	if(recv_bytes>0)
	{
		strcpy(dup_recv_mes,recv_mes);

		if(recv_mes[0]=='%')
		{
			memset(recv_mes,'\0',MAXSIZE);

			if((recv_bytes=recv(sockfd,recv_mes,1,0))==-1)
				error_message("recv");
			recv_mes[1]='\0';

			strcat(dup_recv_mes,recv_mes);
		}
		else if(dup_recv_mes[recv_bytes-1]!='\n')
		{
			do
			{
				memset(recv_mes,'\0',MAXSIZE);

				if((recv_bytes=recv(sockfd,recv_mes,1,0))<=0)
					break;
				recv_mes[1]='\0';

				strcat(dup_recv_mes,recv_mes);

			}while(dup_recv_mes[strlen(dup_recv_mes)-1]!='\n');
		}
		memset(recv_mes,'\0',MAXSIZE);
		strcpy(recv_mes,dup_recv_mes);
	}

	return 0;
}

int string_parser(char *recv_mes)
{
	memset(query_string,'\0',STRING_SIZE);
	memset(request_method,'\0',STRING_SIZE);
	memset(script_name,'\0',STRING_SIZE);

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

int connect_server(int position)
{
	struct sockaddr_in dest_addinfo;
	struct hostent *he;

	he=gethostbyname(server_host[position]);

	memset(&dest_addinfo,0,sizeof(dest_addinfo)); 
	dest_addinfo.sin_family=AF_INET;
	dest_addinfo.sin_addr=*((struct in_addr*)he->h_addr);
	dest_addinfo.sin_port=htons(atoi(server_port[position]));

	connect(batch_socket[position],(struct sockaddr*)&dest_addinfo,sizeof(dest_addinfo));

	batch_number++;

	return 0;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
	LPSTR lpCmdLine, int nCmdShow)
{

	return DialogBox(hInstance, MAKEINTRESOURCE(ID_MAIN), NULL, MainDlgProc);
}

BOOL CALLBACK MainDlgProc(HWND hwnd, UINT Message, WPARAM wParam, LPARAM lParam)
{
	WSADATA wsaData;

	//static HWND hwndEdit;
	static SOCKET msock, ssock;
	static struct sockaddr_in sa;

	int err;

	switch(Message) 
	{
	case WM_INITDIALOG:
		hwndEdit = GetDlgItem(hwnd, IDC_RESULT);
		break;
	case WM_COMMAND:
		switch(LOWORD(wParam))
		{
		case ID_LISTEN:

			WSAStartup(MAKEWORD(2, 0), &wsaData);

			//create master socket
			msock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

			if( msock == INVALID_SOCKET ) {
				EditPrintf(hwndEdit, TEXT("=== Error: create socket error ===\r\n"));
				WSACleanup();
				return TRUE;
			}

			err = WSAAsyncSelect(msock, hwnd, WM_SOCKET_NOTIFY, FD_ACCEPT | FD_CLOSE | FD_READ | FD_WRITE);

			if ( err == SOCKET_ERROR ) {
				EditPrintf(hwndEdit, TEXT("=== Error: select error ===\r\n"));
				closesocket(msock);
				WSACleanup();
				return TRUE;
			}

			//fill the address info about server
			sa.sin_family		= AF_INET;
			sa.sin_port			= htons(SERVER_PORT);
			sa.sin_addr.s_addr	= INADDR_ANY;

			//bind socket
			err = bind(msock, (LPSOCKADDR)&sa, sizeof(struct sockaddr));

			if( err == SOCKET_ERROR ) {
				EditPrintf(hwndEdit, TEXT("=== Error: binding error ===\r\n"));
				WSACleanup();
				return FALSE;
			}

			err = listen(msock, 2);

			if( err == SOCKET_ERROR ) {
				EditPrintf(hwndEdit, TEXT("=== Error: listen error ===\r\n"));
				WSACleanup();
				return FALSE;
			}
			else {
				EditPrintf(hwndEdit, TEXT("=== Server START ===\r\n"));
			}

			break;
		case ID_EXIT:
			EndDialog(hwnd, 0);
			break;
		};
		break;

	case WM_CLOSE:
		EndDialog(hwnd, 0);
		break;

	case WM_SOCKET_NOTIFY:
		switch( WSAGETSELECTEVENT(lParam) )
		{
		case FD_ACCEPT:
			ssock = accept(msock, NULL, NULL);
			Socks.push_back(ssock);
			EditPrintf(hwndEdit, TEXT("=== Accept one new client(%d), List size:%d ===\r\n"), ssock, Socks.size());
			break;
		case FD_READ:
			{
				initial();

				int recv_byte;
				char recv_mes[MAXSIZE];
				int id;

				char *p;					
				char *s;

				FILE *pFile;
				char file_name[STRING_SIZE];

				char temp_string[STRING_SIZE];
				char send_string1[STRING_SIZE];

				memset(send_string1,'\0',STRING_SIZE);
				strcpy(send_string1,"<td valign=\"top\" id=\"m%d\"></td>");

				memset(recv_mes,'\0',MAXSIZE);
				if((recv_byte=recv(ssock,recv_mes,MAXSIZE,0))==-1)
					break;
				recv_mes[recv_byte]='\0';

				string_parser(recv_mes);

				p=strrchr(script_name,'.');
				if(strcmp(p,".cgi")==0)
				{
					send(ssock,"HTTP/1.1 200 OK\r\n",strlen("HTTP/1.1 200 OK\r\n"),0);				
					send(ssock,"Content-Type: text/html\r\n",strlen("Content-Type: text/html\r\n"),0);
					send(ssock,"\r\n",2,0);

					send(ssock,"<html>\n",strlen("<html>\n"),0);
					send(ssock,"<head>\n",strlen("<head>\n"),0);
					send(ssock,"<meta http-equiv=\"Content-Type\" content=\"text/html; charset=big5\" />\n",strlen("<meta http-equiv=\"Content-Type\" content=\"text/html; charset=big5\" />\n"),0);
					send(ssock,"<title>Network Programming Homework 3</title>\n",strlen("<title>Network Programming Homework 3</title>\n"),0);
					send(ssock,"</head>\n",strlen("</head>\n"),0);
					send(ssock,"<body bgcolor=#336699>\n",strlen("<body bgcolor=#336699>\n"),0);
					send(ssock,"<font face=\"Courier New\" size=2 color=#FFFF99>\n",strlen("<font face=\"Courier New\" size=2 color=#FFFF99>\n"),0);
					send(ssock,"<table width=\"800\" border=\"1\">\n",strlen("<table width=\"800\" border=\"1\">\n"),0);
					send(ssock,"<tr>\n",strlen("<tr>\n"),0);

					s=strtok(query_string,"&"); //h1=140.113.210.101&p1=7000&f1=batch_file1
					while(s!=NULL)
					{
						id=((int)s[1])-48-1; //0's ascii code //從0

						if(s[0]=='h' && s[3]!='\0')  //h1=140.113.210.101
						{
							batch_exist[id]=true;
							strcpy(server_host[id],s+3);
						}
						else if(s[0]=='p' && s[3]!='\0') //p1=7000
							strcpy(server_port[id],s+3);
						else if(s[0]=='f' && s[3]!='\0') //f1=batch_file1
							strcpy(batch_file[id],s+3);

						s=strtok(NULL,"&");
					}

					for(int i=0;i<SERVER_NUM;i++)
					{
						if(batch_exist[i]==true)
						{
							send(ssock,"<td>",strlen("<td>"),0);
							send(ssock,server_host[i],strlen(server_host[i]),0);
							send(ssock,"</td>",strlen("</td>"),0);
						}
					}

					send(ssock,"</tr>\n",strlen("</tr>\n"),0);
					send(ssock,"<tr>\n",strlen("<tr>\n"),0);

					for(int i=0;i<SERVER_NUM;i++)
					{
						if(batch_exist[i]==true)
						{
							memset(temp_string,'\0',STRING_SIZE);
							sprintf(temp_string,send_string1,i);
							send(ssock,temp_string,strlen(temp_string),0);
						}
					}

					cout<<"</tr>\n";
					cout<<"</table>\n";

					for(int i=0;i<SERVER_NUM;i++)
					{
						if(batch_exist[i]==true)
						{
							WSADATA wsaData;
							WSAStartup(MAKEWORD(2, 0), &wsaData);

							batch_socket[i]=socket(AF_INET,SOCK_STREAM,0);
							WSAAsyncSelect(batch_socket[i],hwnd,WM_SOCKET_SELECT,FD_READ | FD_CLOSE);

							connect_server(i);
							file_fd[i]=fopen(batch_file[i],"r");
						}
					}
				}
				else if(strcmp(p,".html")==0)
				{
					send(ssock,"HTTP/1.1 200 OK\r\n",strlen("HTTP/1.1 200 OK\r\n"),0);
					send(ssock,"Content-Type: text/html\r\n",strlen("Content-Type: text/html\r\n"),0);

					strcpy(file_name,script_name+1);

					pFile=fopen(file_name,"r");

					memset(recv_mes,'\0',MAXSIZE);
					recv_byte=fread(recv_mes,1,MAXSIZE,pFile);
					recv_mes[recv_byte]='\0';

					send(ssock,"\r\n",strlen("\r\n"),0);
					send(ssock,recv_mes,strlen(recv_mes),0);

					closesocket(ssock);
				}		
				break;
			}
		case FD_WRITE:
			break;
		case FD_CLOSE:
			break;
		};
		break;

	case WM_SOCKET_SELECT:
		switch(WSAGETSELECTEVENT(lParam))
		{
		case FD_READ:
			{
				int position=-1;

				for(int i=0;i<SERVER_NUM;i++)
				{
					if(batch_exist[i]==true && batch_socket[i]==wParam)
					{
						position=i;
						break;
					}
				}

				if(position==-1 || batch_exist[position]==false)
					break;

				char recv_mes[MAXSIZE];

				char temp_string[STRING_SIZE];
				char send_string2[STRING_SIZE];
				strcpy(send_string2,"<script>document.all['m%d'].innerHTML += \"");

				recv_message(batch_socket[position],recv_mes);

				memset(temp_string,'\0',STRING_SIZE);
				sprintf(temp_string,send_string2,position);
				send(ssock,temp_string,strlen(temp_string),0);

				for(int j=0;recv_mes[j]!='\0';j++)
				{
					if(recv_mes[j]=='"')
						send(ssock,"&quot;",strlen("&quot;"),0);
					else if(recv_mes[j]=='\'')
						send(ssock,"&apos;",strlen("&apos;"),0);
					else if(recv_mes[j]==' ')
						send(ssock,"&nbsp;",strlen("&nbsp;"),0);
					else if(recv_mes[j]=='<')
						send(ssock,"&lt",strlen("&lt"),0);
					else if(recv_mes[j]=='>')
						send(ssock,"&gt;",strlen("&gt;"),0);
					else if(recv_mes[j]=='\n')
						send(ssock,"<br>",strlen("<br>"),0);
					else if(recv_mes[j]=='\r')
						continue;
					else
						send(ssock,&recv_mes[j],1,0);
				}
				send(ssock,"\";</script>\n",strlen("\";</script>\n"),0);

				if(ready_exit[position]==true && batch_exist[position]==true)
				{
					ready_exit[position]=false;
					batch_exist[position]=false;
					batch_number--;
					if(batch_number==0)
					{
						send(ssock,"</font>\n",strlen("</font>\n"),0);
						send(ssock,"</body>\n",strlen("</body>\n"),0);
						send(ssock,"</html>\n",strlen("</html>\n"),0);

						for(int i=0;i<SERVER_NUM;i++)
							closesocket(batch_socket[i]);
						closesocket(ssock);
					}
				}
				else if(recv_mes[0]=='%')
				{
					recv_input(file_fd[position],recv_mes);
					//EditPrintf(hwndEdit, TEXT("recv_mes : %s\n"),recv_mes);
					send(batch_socket[position],recv_mes,strlen(recv_mes),0);

					memset(temp_string,'\0',STRING_SIZE);
					sprintf(temp_string,send_string2,position);
					send(ssock,temp_string,strlen(temp_string),0);

					for(int j=0;recv_mes[j]!='\0';j++)
					{
						if(recv_mes[j]=='"')
							send(ssock,"<b>&quot;</b>",strlen("<b>&quot;</b>"),0);
						else if(recv_mes[j]=='\'')
							send(ssock,"<b>&apos;</b>",strlen("<b>&apos;</b>"),0);
						else if(recv_mes[j]==' ')
							send(ssock,"&nbsp;",strlen("&nbsp;"),0);
						else if(recv_mes[j]=='<')
							send(ssock,"<b>&lt;</b>",strlen("<b>&lt;</b>"),0);
						else if(recv_mes[j]=='>')
							send(ssock,"<b>&gt;</b>",strlen("<b>&gt;</b>"),0);
						else if(recv_mes[j]=='\n')
							send(ssock,"<br>",strlen("<br>"),0);
						else if(recv_mes[j]=='\r')
							continue;
						else
						{
							send(ssock,"<b>",strlen("<b>"),0);
							send(ssock,&recv_mes[j],1,0);
							send(ssock,"</b>",strlen("</b>"),0);
						}
					}
					//EditPrintf(hwndEdit, TEXT("2.recv_mes : %s\n"),recv_mes);
					send(ssock,"\";</script>\n",strlen("\";</script>\n"),0);

					char *p=strtok(recv_mes,"\r\n\0");
					if(p!=NULL && strcmp(p,"exit")==0)
					{
						ready_exit[position]=true;
						Sleep(1);

						if(strcmp(server_port[position],"6500")==0)
						{
							ready_exit[position]=false;
							batch_exist[position]=false;
							batch_number--;
							if(batch_number==0)
							{
								send(ssock,"</font>\n",strlen("</font>\n"),0);
								send(ssock,"</body>\n",strlen("</body>\n"),0);
								send(ssock,"</html>\n",strlen("</html>\n"),0);

								for(int i=0;i<SERVER_NUM;i++)
									closesocket(batch_socket[i]);
								closesocket(ssock);
							}
						}
					}
				}
				break;
			}
		case FD_WRITE:	
			break;
		case FD_CLOSE:
			break;
		};
		break;

	default:
		return FALSE;
	};

	return TRUE;
}

int EditPrintf (HWND hwndEdit, TCHAR * szFormat, ...)
{
	TCHAR   szBuffer [7000] ;
	va_list pArgList ;

	va_start (pArgList, szFormat) ;
	wvsprintf (szBuffer, szFormat, pArgList) ;
	va_end (pArgList) ;

	SendMessage (hwndEdit, EM_SETSEL, (WPARAM) -1, (LPARAM) -1) ;
	SendMessage (hwndEdit, EM_REPLACESEL, FALSE, (LPARAM) szBuffer) ;
	SendMessage (hwndEdit, EM_SCROLLCARET, 0, 0) ;
	return SendMessage(hwndEdit, EM_GETLINECOUNT, 0, 0); 
}