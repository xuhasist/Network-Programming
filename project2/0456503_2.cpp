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
#include <sys/shm.h>
#include <sys/ipc.h>
#include <sys/stat.h>

using namespace std;

#define PORT 6502
#define BACKLOG 30
#define EXIT 4
#define FINISH 8
#define UNKNOWN 9
#define FAIL 10

#define STRING_SIZE 100
#define BUFFER_SIZE 3500
#define LINE_SIZE 15000
#define COMMAND_SIZE 256
#define PIPE_POOL_NUMBER 100

#define SHMKEY 7883
#define SHMKEY1 7884
#define SHMKEY2 7885

char welcome_message[]="****************************************\n** Welcome to the information server. **\n****************************************\n";
char percent[]="% ";
char default_path[]="bin:.";

int self_position;
char str_mess[STRING_SIZE];

char default_name[]="(no name)";
char user_enter[]="*** User '%s' entered from %s/%d. ***\n";
char user_left[]="*** User '%s' left. ***\n";
char who_title[]="<ID>\t<nickname>\t<IP/port>\t<indicate me>\n";
char who_content[]="%d\t%s\t%s/%d\t%s\n";
char tell_[]="*** %s told you ***: %s\n";
char tell_err[]="*** Error: user #%d does not exist yet. ***\n";
char yell_[]="*** %s yelled ***: %s\n";
char name_[]="*** User from %s/%d is named '%s'. ***\n";
char name_same[]="*** User '%s' already exists. ***\n";

char pipe_push[]="*** %s (#%d) just piped '%s' ***\n";
char pipe_exist[]="*** Error: public pipe #%d already exists. ***\n";
char pipe_pull[]="*** %s (#%d) just received via '%s' ***\n";
char pipe_not_exist[]="*** Error: public pipe #%d does not exist yet. ***\n";

enum pipe_type {normal_pipe,error,push,pull};

struct PIPE_TOKEN{
	char pipe[20];
	int pipe_type;
	int pipe_index;
};

struct COMMAND_TOKEN{
	char command[COMMAND_SIZE];
	int command_index;
};

struct COMMAND_STRING{
	char **command_list;
	int command_length;
	int command_in;
	int command_out;
	int command_err;
	int command_push;
	int command_pull;
};

struct PIPE_POOL{
	int pipe_index;
	int pipe_lifetime;
	PIPE_POOL *next;
};

struct INFORMATION{
	int public_pipe_index;
	bool public_pipe_open;
	int user_number;
};
INFORMATION *information_shm;
int information_shmid;

struct ALL_USER{
	int pid;
	int id;
	char name[20];
	char ip[16];
	int port;
	int socket;
	bool public_pipe_exist[100];
};
ALL_USER *private_shm;
int private_shmid;

char *public_shm;
int public_shmid;

int error_message(string s)
{
	cerr<<"\n"<<s<<" : "<<strerror(errno)<<endl;
		exit(1);
}

void sigint(int sig){
	shmctl(public_shmid,IPC_RMID,NULL);
	shmctl(private_shmid,IPC_RMID,NULL);
	shmctl(information_shmid,IPC_RMID,NULL);
	//close(sockfd);
	exit(sig);
}

void sigusr1_handler(int sig)
{
	if(send(private_shm[self_position].socket,public_shm,strlen(public_shm),0)==-1)
		error_message("send");
}
void sigusr2_handler(int sig)
{
	for(int i=0;i<30;i++)
	{
		if(information_shm->public_pipe_open==true)
			private_shm[self_position].public_pipe_exist[information_shm->public_pipe_index]=true;
		else
			private_shm[self_position].public_pipe_exist[information_shm->public_pipe_index]=false;
	}
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

int recv_input(int new_sockfd,char *dup_recv_line,char *recv_line)
{
	int recv_bytes;

	memset(recv_line,'\0',LINE_SIZE);
	memset(dup_recv_line,'\0',LINE_SIZE);

	if((recv_bytes=recv(new_sockfd,recv_line,LINE_SIZE,0))==-1)
		error_message("recv");

	strcpy(dup_recv_line,recv_line);

	if(dup_recv_line[recv_bytes-1]!='\n')
	{
		do
		{
			memset(recv_line,'\0',LINE_SIZE);

			if((recv_bytes=recv(new_sockfd,recv_line,LINE_SIZE,0))==-1)
				error_message("recv");

			strcat(dup_recv_line,recv_line);

		}while(dup_recv_line[strlen(dup_recv_line)-1]!='\n');
	}
	dup_recv_line[strlen(dup_recv_line)]='\0';

	return 0;
}

int broadcast()
{
	for(int i=0;i<30;i++)
	{
		if(private_shm[i].id!=-1)
		{
			kill(private_shm[i].pid,SIGUSR1);
		}
	}

	return 0;
}
int who()
{
	if(send(private_shm[self_position].socket,who_title,strlen(who_title),0)==-1)
		error_message("send");

	for(int i=0;i<30;i++)
	{
		if(private_shm[i].id!=-1)
		{
			memset(str_mess,'\0',STRING_SIZE);

			if(i==self_position)
				sprintf(str_mess,who_content,private_shm[i].id,private_shm[i].name,private_shm[i].ip,private_shm[i].port,"<-me");
			else
				sprintf(str_mess,who_content,private_shm[i].id,private_shm[i].name,private_shm[i].ip,private_shm[i].port,"");

			if(send(private_shm[self_position].socket,str_mess,strlen(str_mess),0)==-1)
				error_message("send");
		}
	}

	return 0;
}
int tell(int id,char *string)
{
	for(int i=0;i<30;i++)
	{
		if(private_shm[i].id==id)
		{
			memset(public_shm,'\0',1024);
			sprintf(public_shm,tell_,private_shm[self_position].name,string);

			kill(private_shm[i].pid,SIGUSR1);	

			return 0;	
		}
	}

	memset(str_mess,'\0',STRING_SIZE);
	sprintf(str_mess,tell_err,id);

	if(send(private_shm[self_position].socket,str_mess,strlen(str_mess),0)==-1)
		error_message("send");

	return 0;
}
int yell(char *string)
{
	for(int i=0;i<30;i++)
	{
		if(private_shm[i].id!=-1)
		{
			memset(public_shm,'\0',1024);
			sprintf(public_shm,yell_,private_shm[self_position].name,string);

			kill(private_shm[i].pid,SIGUSR1);
		}
	}

	return 0;
}
int check_name(char *new_name)
{
	for(int i=0;i<30;i++)
	{
		if(i!=self_position && strcmp(private_shm[i].name,new_name)==0)
			return false;
	}

	return true;
}
int name(char *new_name)
{
	if(check_name(new_name)==true)
	{
		memset(private_shm[self_position].name,'\0',20);
		strcpy(private_shm[self_position].name,new_name);

		memset(public_shm,'\0',1024);
		sprintf(public_shm,name_,private_shm[self_position].ip,private_shm[self_position].port,new_name);

		broadcast();
	}
	else
	{
		memset(str_mess,'\0',STRING_SIZE);
		sprintf(str_mess,name_same,new_name);

		if(send(private_shm[self_position].socket,str_mess,strlen(str_mess),0)==-1)
			error_message("send");
	}

	return 0;
}

int line_parser(int new_sockfd,char *dup_recv_line,int &token_number,int &pipe_number,int &command_number,char **line_token,PIPE_TOKEN *pipe_token,COMMAND_TOKEN *command_token)
{
	dup_recv_line=strtok(dup_recv_line,"\0");
	dup_recv_line=strtok(dup_recv_line,"\r\n");

	char *p;
	p=strtok(dup_recv_line," ");
	while(p!=NULL)
	{
		strcpy(line_token[token_number],p);
		token_number++;
		p=strtok(NULL," ");
	}

	char *env_path=new char[100];
	char *env_name=new char[100];

	memset(env_path,'\0',100);
	memset(env_name,'\0',100);

	for(int i=0;i<token_number;i++)
	{
		if(strcmp(line_token[i],"exit")==0)
			return EXIT;

		if(strcmp(line_token[0],"printenv")==0)
		{
			env_name=getenv(line_token[i+1]);

			strcpy(env_path,line_token[i+1]);
			strcat(env_path,"=");
			strcat(env_path,env_name);
			strcat(env_path,"\n");

			if((send(new_sockfd,env_path,strlen(env_path),0))==-1)
				error_message("send");

			return FINISH;
		}
		else if(strcmp(line_token[0],"setenv")==0)
		{
			if((setenv(line_token[i+1],line_token[i+2],1))==-1)
				error_message("setenv");

			return FINISH;
		}
		else if(strcmp(line_token[0],"who")==0)
		{
			who();
			return FINISH;
		}
		else if(strcmp(line_token[0],"tell")==0)
		{
			for(int j=3;j<token_number;j++)
			{
				strcat(line_token[2]," ");
				strcat(line_token[2],line_token[j]);
			}
		
			tell(atoi(line_token[1]),line_token[2]);
			return FINISH;
		}
		else if(strcmp(line_token[0],"yell")==0)
		{
			for(int j=2;j<token_number;j++)
			{
				strcat(line_token[1]," ");
				strcat(line_token[1],line_token[j]);
			}

			yell(line_token[1]);
			return FINISH;
		}
		else if(strcmp(line_token[0],"name")==0)
		{
			name(line_token[1]);
			return FINISH;
		}

		if((strncmp(line_token[i],"|",1)==0) || (strncmp(line_token[i],"!",1)==0) || (strncmp(line_token[i],">",1)==0) || (strncmp(line_token[i],"<",1)==0))
		{
			strcpy(pipe_token[pipe_number].pipe,line_token[i]);
			pipe_token[pipe_number].pipe_index=i;

			if((strncmp(line_token[i],"|",1)==0) && strlen(line_token[i])!=1)
				pipe_token[pipe_number].pipe_type=normal_pipe;
			else if(strncmp(line_token[i],"!",1)==0)
				pipe_token[pipe_number].pipe_type=error;
			else if((strncmp(line_token[i],">",1)==0) && strlen(line_token[i])!=1)
				pipe_token[pipe_number].pipe_type=push;
			else if((strncmp(line_token[i],"<",1)==0) && strlen(line_token[i])!=1)
				pipe_token[pipe_number].pipe_type=pull;

			pipe_number++;
		}
		else
		{
			strcpy(command_token[command_number].command,line_token[i]);
			command_token[command_number].command_index=i;
			command_number++;
		}
	}

	delete [] env_path;
	delete [] env_name;

	return 0;
}

bool command_exist(int new_sockfd,char *command)
{
	DIR *dir;
    struct dirent *ptr;
    char *env_path=new char[20];
    env_path=getenv("PATH");

    char unknown_command[100];
	memset(unknown_command,'\0',100);

	char *p;
	p=strtok(env_path,":");
	while(p!=NULL)
	{
		dir=opendir(env_path);

		while((ptr=readdir(dir))!=NULL)
	    {
	        if(strcmp(ptr->d_name,command)==0)
	        	return true;
	    }
	    closedir(dir);

		p=strtok(NULL,":");
	}

	strcat(unknown_command,"Unknown command: [");
	strcat(unknown_command,command);
	strcat(unknown_command,"].\n");

	if((send(new_sockfd,unknown_command,strlen(unknown_command),0))==-1)
		error_message("send");

	return false;
}

int add_pipe_pool(PIPE_POOL **pipe_pool_head,int &pipe_index,int pipe_lifetime)
{
	PIPE_POOL *pipe_pool_temp=new PIPE_POOL;
	PIPE_POOL *pipe_pool_tail;

	pipe_pool_temp->pipe_index=pipe_index;
	pipe_pool_temp->pipe_lifetime=pipe_lifetime;

	if((*pipe_pool_head)==NULL)
	{
		(*pipe_pool_head)=pipe_pool_temp;
		(*pipe_pool_head)->next=NULL;

		return 0;
	}
	else
		pipe_pool_tail=(*pipe_pool_head);

	while(pipe_pool_tail!=NULL)
	{
		if(pipe_pool_tail->next==NULL)
		{
			pipe_pool_tail->next=pipe_pool_temp;
			pipe_pool_tail=pipe_pool_tail->next;
			pipe_pool_tail->next=NULL;
		}
		pipe_pool_tail=pipe_pool_tail->next;
	}

	return 0;
}
int remove_pipe_pool(PIPE_POOL **pipe_pool_head,PIPE_POOL *pipe_pool_remove)
{
	PIPE_POOL *pipe_pool_temp=(*pipe_pool_head);

	if((*pipe_pool_head)==pipe_pool_remove)
	{
		(*pipe_pool_head)=(*pipe_pool_head)->next;
		free(pipe_pool_remove);

		return 0;
	}

	while(pipe_pool_temp!=NULL)
	{
		if(pipe_pool_temp->next==pipe_pool_remove)
		{
			pipe_pool_temp->next=pipe_pool_remove->next;
			free(pipe_pool_remove);
			break;
		}
		pipe_pool_temp=pipe_pool_temp->next;
	}

	return 0;
}
int search_pipe_pool_in(PIPE_POOL **pipe_pool_head,COMMAND_STRING *command_string)
{
	PIPE_POOL *pipe_pool_temp=(*pipe_pool_head);
	PIPE_POOL *pipe_pool_temp_next;

	if(pipe_pool_temp!=NULL)
		pipe_pool_temp_next=pipe_pool_temp->next;

	while(pipe_pool_temp!=NULL)
	{
		if(pipe_pool_temp->pipe_lifetime==0)
		{
			command_string[0].command_in=pipe_pool_temp->pipe_index;

			remove_pipe_pool(pipe_pool_head,pipe_pool_temp);
		}
		else
			pipe_pool_temp->pipe_lifetime--;

		pipe_pool_temp=pipe_pool_temp_next;
		if(pipe_pool_temp!=NULL)
			pipe_pool_temp_next=pipe_pool_temp->next;
	}	

	return 0;
}
int search_pipe_pool_out(PIPE_POOL **pipe_pool_head,int &command_count,COMMAND_STRING *command_string,int now_pipe_lifetime)
{
	PIPE_POOL *pipe_pool_temp=(*pipe_pool_head);
	PIPE_POOL *pipe_pool_temp_next;

	if(pipe_pool_temp!=NULL)
		pipe_pool_temp_next=pipe_pool_temp->next;

	for(int i=0;pipe_pool_temp!=NULL;i++)
	{
		if(pipe_pool_temp->pipe_lifetime==now_pipe_lifetime)
			return pipe_pool_temp->pipe_index;

		pipe_pool_temp=pipe_pool_temp_next;
		if(pipe_pool_temp!=NULL)
			pipe_pool_temp_next=pipe_pool_temp->next;
	}

	return -1;
}

void pipe_use_add_one(int &pipe_use)
{
	pipe_use++;

	if(pipe_use>=1500)
		pipe_use=0;
}

int open_file(char *filename)
{
	FILE *pFile;
	pFile=fopen(filename,"w+");
	fseek(pFile,0,SEEK_SET);

	return fileno(pFile);
}

int command_parser(int &pipe_use,int &command_count,COMMAND_STRING *command_string,PIPE_POOL **pipe_pool_head,int new_sockfd,char *recv_line,int &token_number,int &pipe_number,int &command_number,char **line_token,PIPE_TOKEN *pipe_token,COMMAND_TOKEN *command_token)
{
	int temp_command_string_count=0;
	command_count=1;

	int file_fd;
	int fifo_fd;

	if(command_exist(new_sockfd,command_token[0].command))
		strcpy(command_string[command_count-1].command_list[temp_command_string_count],command_token[0].command);
	else
		return UNKNOWN;

	temp_command_string_count++;

	for(int i=1;i<command_number;i++)
	{
		if((command_token[i].command_index-1)==(command_token[i-1].command_index))
		{
			strcpy(command_string[command_count-1].command_list[temp_command_string_count],command_token[i].command);
			temp_command_string_count++;
		}
		else
		{
			command_string[command_count-1].command_length=temp_command_string_count+1;

			if((i==command_number-1) && (strcmp(pipe_token[pipe_number-1].pipe,">")==0))
				file_fd=open_file(command_token[command_number-1].command);
			else
			{	
				if(command_exist(new_sockfd,command_token[i].command))
				{
					temp_command_string_count=0;
					command_count++;

					strcpy(command_string[command_count-1].command_list[temp_command_string_count],command_token[i].command);
					temp_command_string_count++;
				}
				else
					return UNKNOWN;
			}
		}
	}
	command_string[command_count-1].command_length=temp_command_string_count+1;

	char *p;
	for(int i=0;i<pipe_number;i++)
	{
		if(pipe_token[i].pipe_type==normal_pipe)
		{
			p=strtok(pipe_token[i].pipe,"|");

			command_string[command_count-1].command_out=search_pipe_pool_out(pipe_pool_head,command_count,command_string,atoi(p));
			
			if(command_string[command_count-1].command_out==-1)
			{
				command_string[command_count-1].command_out=pipe_use;
				pipe_use_add_one(pipe_use);
			}

			add_pipe_pool(pipe_pool_head,command_string[command_count-1].command_out,atoi(p));
		}
		else if(pipe_token[i].pipe_type==error)
		{
			p=strtok(pipe_token[i].pipe,"!");

			command_string[command_count-1].command_err=search_pipe_pool_out(pipe_pool_head,command_count,command_string,atoi(p));
			
			if(command_string[command_count-1].command_err==-1)
			{
				command_string[command_count-1].command_err=pipe_use;
				pipe_use_add_one(pipe_use);
			}

			add_pipe_pool(pipe_pool_head,command_string[command_count-1].command_err,atoi(p));
		}	
		else if(pipe_token[i].pipe_type==pull)
		{
			p=strtok(pipe_token[i].pipe,"<");

			if(private_shm[self_position].public_pipe_exist[atoi(p)-1]==false) //public pipe 不存在
			{
				memset(str_mess,'\0',STRING_SIZE);
				sprintf(str_mess,pipe_not_exist,atoi(p));

				if(send(new_sockfd,str_mess,strlen(str_mess),0)==-1)
					error_message("send");

				return FAIL;
			}
			else
			{
				information_shm->public_pipe_index=atoi(p)-1;
				information_shm->public_pipe_open=false;

				for(int j=0;j<30;j++)
				{
					if(private_shm[j].id!=-1)
						kill(private_shm[j].pid,SIGUSR2);
				}

				char path[]="../";
				strcat(path,p);
				fifo_fd=open(path,O_RDONLY);

				command_string[0].command_pull=fifo_fd;

				memset(public_shm,'\0',2014);
				sprintf(public_shm,pipe_pull,private_shm[self_position].name,private_shm[self_position].id,recv_line);

				broadcast();
			}
		}
		else if(pipe_token[i].pipe_type==push)
		{
			p=strtok(pipe_token[i].pipe,">");

			if(private_shm[self_position].public_pipe_exist[atoi(p)-1]==true) //public pipe 已經存在
			{
				memset(str_mess,'\0',STRING_SIZE);
				sprintf(str_mess,pipe_exist,atoi(p));

				if(send(new_sockfd,str_mess,strlen(str_mess),0)==-1)
					error_message("send");

				//search_pipe_pool_in(pipe_pool_head,command_string); //////
				return FAIL;
			}
			else
			{
				information_shm->public_pipe_index=atoi(p)-1;
				information_shm->public_pipe_open=true;

				for(int j=0;j<30;j++)
				{
					if(private_shm[j].id!=-1)
						kill(private_shm[j].pid,SIGUSR2);
				}

				char path[]="../";
				strcat(path,p);
				fifo_fd=open(path,O_WRONLY | O_CREAT | O_TRUNC,0666);

				command_string[command_count-1].command_push=fifo_fd;

				memset(public_shm,'\0',1024);
				sprintf(public_shm,pipe_push,private_shm[self_position].name,private_shm[self_position].id,recv_line);

				broadcast();
			}
		}
	}

	if(command_count==1 && pipe_number==0) //only one command
		search_pipe_pool_in(pipe_pool_head,command_string);
	else if(command_count==1 && pipe_number!=0)
	{
		if((command_string[0].command_out==-1) && (command_string[0].command_err==-1) && (command_string[0].command_push==-1))///
		{
			for(int i=0;i<pipe_number;i++) //command <3 |1 !1
			{
				if(strcmp(pipe_token[i].pipe,">")==0)
					command_string[0].command_out=file_fd;
				else if((strncmp(pipe_token[i].pipe,"|",1)==0) || (strncmp(pipe_token[i].pipe,"!",1)==0))
				{
					command_string[0].command_out=pipe_use; // |n !n
					pipe_use_add_one(pipe_use);
				}
			}
		}

		search_pipe_pool_in(pipe_pool_head,command_string);
	}
	else
	{
		command_string[0].command_out=pipe_use;

		search_pipe_pool_in(pipe_pool_head,command_string);

		for(int i=1;i<command_count-1;i++)
		{
			command_string[i].command_in=command_string[i-1].command_out;

			pipe_use_add_one(pipe_use);

			command_string[i].command_out=pipe_use;
		}
		pipe_use_add_one(pipe_use);

		command_string[command_count-1].command_in=command_string[command_count-2].command_out;

		if(strcmp(pipe_token[pipe_number-1].pipe,">")==0)
			command_string[command_count-1].command_out=file_fd;
	}

	if(command_string[command_count-1].command_out==-1 && command_string[command_count-1].command_push==-1)
		dup2(new_sockfd,1);
	if(command_string[command_count-1].command_err==-1 && command_string[command_count-1].command_push==-1)
		dup2(new_sockfd,2);

	/*cout<<"\ncommand_string[0].command_in = "<<command_string[0].command_in<<endl;
	cout<<"\ncommand_string[command_count-1].command_out = "<<command_string[command_count-1].command_out<<endl;
	cout<<"\ncommand_string[command_count-1].command_err = "<<command_string[command_count-1].command_err<<endl;
	cout<<"\ncommand_string[0].command_pull = "<<command_string[0].command_pull<<endl;
	cout<<"\ncommand_string[command_count-1].command_push = "<<command_string[command_count-1].command_push<<endl;*/

	return 0;
}

int command_exec(int **pipe_array,int &pipe_use,int &command_count,COMMAND_STRING *command_string,PIPE_POOL *pipe_pool_head,int new_sockfd,char *recv_line,int &token_number,int &pipe_number,int &command_number,char **line_token,PIPE_TOKEN *pipe_token,COMMAND_TOKEN *command_token)
{
	char **arg;
	for(int i=0;i<command_count;i++)
	{
		arg=new char*[command_string[i].command_length];

		for(int j=0;j<command_string[i].command_length-1;j++)
		{
			arg[j]=new char[100];
			memset(arg[j],'\0',100);

			strcpy(arg[j],command_string[i].command_list[j]);
		}
		arg[command_string[i].command_length-1]=new char[20];
		arg[command_string[i].command_length-1]=NULL;

		bool no_pipe=true;

		if((command_string[i].command_in!=-1))
		{
			no_pipe=false;

			if(pipe_array[command_string[i].command_in][0]==-1)
				pipe(pipe_array[command_string[i].command_in]);
		}
		if((command_string[i].command_out!=-1))
		{
			if(i!=command_count-1 || (strcmp(pipe_token[pipe_number-1].pipe,">")!=0))
			{
				no_pipe=false;

				if(pipe_array[command_string[i].command_out][0]==-1)
					pipe(pipe_array[command_string[i].command_out]);
			}
		}
		if(command_string[i].command_err!=-1)
		{
			no_pipe=false;

			if(pipe_array[command_string[i].command_err][0]==-1)
				pipe(pipe_array[command_string[i].command_err]);
		}
		if(command_string[i].command_push!=-1 || command_string[i].command_pull!=-1)
			no_pipe=false;

		if(!fork())
		{
			if(no_pipe==true && pipe_number!=0)
			{
				dup2(command_string[i].command_out,1);
				close(command_string[i].command_out);
			}
			else if(!no_pipe)
			{
				if(command_string[i].command_in!=-1)
				{
					dup2(pipe_array[command_string[i].command_in][0],0);
					close(pipe_array[command_string[i].command_in][0]);
					close(pipe_array[command_string[i].command_in][1]);
				}
				if((i==command_count-1) && (strcmp(pipe_token[pipe_number-1].pipe,">")==0))
				{
					dup2(command_string[i].command_out,1);
					close(command_string[i].command_out);
				}
				else if(command_string[i].command_out!=-1)
				{
					if(command_string[i].command_err==command_string[i].command_out)
						dup2(pipe_array[command_string[i].command_err][1],2);

					dup2(pipe_array[command_string[i].command_out][1],1);
					close(pipe_array[command_string[i].command_out][0]);
					close(pipe_array[command_string[i].command_out][1]);
				}
				
				if(command_string[i].command_err!=-1)
				{
					dup2(pipe_array[command_string[i].command_err][1],2);
					close(pipe_array[command_string[i].command_err][0]);
					close(pipe_array[command_string[i].command_err][1]);
				}
				if(command_string[i].command_push!=-1)
				{
					dup2(command_string[i].command_push,1);
					dup2(command_string[i].command_push,2);
					close(command_string[i].command_push);
				}
				if(command_string[i].command_pull!=-1)
				{
					dup2(command_string[i].command_pull,0);
					close(command_string[i].command_pull);
				}
			}
			execvp(command_string[i].command_list[0],arg);

			exit(0);
		}
		else
		{
			if(no_pipe==true && pipe_number!=0)
				close(command_string[i].command_out);
			else if(!no_pipe)
			{	
				if(command_string[i].command_in!=-1)
				{
					close(pipe_array[command_string[i].command_in][0]);
					close(pipe_array[command_string[i].command_in][1]);

					pipe_array[command_string[i].command_in][0]=-1;
					pipe_array[command_string[i].command_in][1]=-1;
				}
			}

			wait(NULL);
		}

		for(int j=0;j<command_string[i].command_length;j++)
			delete [] arg[j];
		delete [] arg;
	}

	return 0;
}

int delete_space(char *recv_line,char *dup_recv_line,char **line_token,PIPE_TOKEN *pipe_token,COMMAND_TOKEN *command_token,COMMAND_STRING *command_string,int **pipe_array)
{
	delete [] recv_line;
	delete [] dup_recv_line;

	for(int i=0;i<5000;i++)
		delete [] line_token[i];
	delete [] line_token;

	delete [] pipe_token;
	delete [] command_token;

	for(int i=0;i<BUFFER_SIZE;i++)
	{		
		for(int j=0;j<COMMAND_SIZE;j++)
			delete [] command_string[i].command_list[j];

		delete [] command_string[i].command_list;
	}
	delete [] command_string;

	for(int i=0;i<PIPE_POOL_NUMBER;i++)
		delete [] pipe_array[i];
	delete [] pipe_array;

	return 0;
}

int child_work(int new_sockfd)
{
	if(send(new_sockfd,percent,strlen(percent),0)==-1)
		error_message("send");

	if((setenv("PATH",default_path,1))==-1)
		error_message("setenv");

	int return_value;

	char *recv_line=new char[LINE_SIZE];
	char *dup_recv_line=new char[LINE_SIZE];

	int token_number;
	int pipe_number;
	int command_number;

	char **line_token=new char*[5000];
	for(int i=0;i<5000;i++)
		line_token[i]=new char[COMMAND_SIZE];

	PIPE_TOKEN *pipe_token=new PIPE_TOKEN[BUFFER_SIZE];
	COMMAND_TOKEN *command_token=new COMMAND_TOKEN[BUFFER_SIZE];

	int pipe_use=0;
	int command_count;

	COMMAND_STRING *command_string=new COMMAND_STRING[BUFFER_SIZE];
	for(int i=0;i<BUFFER_SIZE;i++)
	{
		command_string[i].command_list=new char*[COMMAND_SIZE];

		for(int j=0;j<COMMAND_SIZE;j++)
			command_string[i].command_list[j]=new char[100];
	}

	PIPE_POOL *pipe_pool_head=NULL;

	int **pipe_array=new int*[PIPE_POOL_NUMBER];
	for(int i=0;i<PIPE_POOL_NUMBER;i++)
	{
		pipe_array[i]=new int[2];
		pipe_array[i][0]=-1;
		pipe_array[i][1]=-1;
	}

	recv_input(new_sockfd,dup_recv_line,recv_line);

	int index;
	if(strchr(dup_recv_line,'\r'))
        index=strchr(dup_recv_line,'\r')-dup_recv_line;
    else if(strchr(dup_recv_line,'\n'))
        index=strchr(dup_recv_line,'\n')-dup_recv_line;
    dup_recv_line[index]='\0';

	memset(recv_line,'\0',LINE_SIZE);
	strcpy(recv_line,dup_recv_line);

	while(1)
	{
		token_number=0;
		pipe_number=0;
		command_number=0;

		for(int i=0;i<5000;i++)
			memset(line_token[i],'\0',COMMAND_SIZE);
		for(int i=0;i<BUFFER_SIZE;i++)
		{
			memset(pipe_token[i].pipe,'\0',20);
			pipe_token[i].pipe_type=-1;
			pipe_token[i].pipe_index=-1;
		}
		for(int i=0;i<BUFFER_SIZE;i++)
		{
			memset(command_token[i].command,'\0',COMMAND_SIZE);
			command_token[i].command_index=-1;
		}

		return_value=line_parser(new_sockfd,dup_recv_line,token_number,pipe_number,command_number,line_token,pipe_token,command_token);

		if(return_value==EXIT)
		{
			delete_space(recv_line,dup_recv_line,line_token,pipe_token,command_token,command_string,pipe_array);
			return EXIT;
		}
		else if(return_value!=FINISH)
		{
			command_count=0;

			for(int i=0;i<BUFFER_SIZE;i++)
			{
				for(int j=0;j<COMMAND_SIZE;j++)
					memset(command_string[i].command_list[j],'\0',100);
			}
			for(int i=0;i<BUFFER_SIZE;i++)
			{
				command_string[i].command_in=-1;
				command_string[i].command_out=-1;
				command_string[i].command_err=-1;
				command_string[i].command_push=-1;
				command_string[i].command_pull=-1;
			}

			return_value=command_parser(pipe_use,command_count,command_string,&pipe_pool_head,new_sockfd,recv_line,token_number,pipe_number,command_number,line_token,pipe_token,command_token);
			
			if(return_value!=UNKNOWN  && return_value!=FAIL)
				command_exec(pipe_array,pipe_use,command_count,command_string,pipe_pool_head,new_sockfd,recv_line,token_number,pipe_number,command_number,line_token,pipe_token,command_token);
		}

		if(send(new_sockfd,percent,strlen(percent),0)==-1)
			error_message("send");

		recv_input(new_sockfd,dup_recv_line,recv_line);

		int index;
		if(strchr(dup_recv_line,'\r'))
	        index=strchr(dup_recv_line,'\r')-dup_recv_line;
	    else if(strchr(dup_recv_line,'\n'))
	        index=strchr(dup_recv_line,'\n')-dup_recv_line;
	    dup_recv_line[index]='\0';

		memset(recv_line,'\0',LINE_SIZE);
		strcpy(recv_line,dup_recv_line);
	}

	delete_space(recv_line,dup_recv_line,line_token,pipe_token,command_token,command_string,pipe_array);
	
	return 0;
}

int initial_user(int position)
{
	private_shm[position].pid=-1;

	private_shm[position].id=-1;
	memset(private_shm[position].name,'\0',20);
	memset(private_shm[position].ip,'\0',16);
	private_shm[position].port=-1;

	private_shm[position].socket=-1;

	for(int i=0;i<100;i++)
		private_shm[position].public_pipe_exist[i]=false;

	return 0;
}

int main()
{
	chdir("./ras");

	int sockfd,new_sockfd;

	create_socket(sockfd);
	reap_zombie();

	struct sockaddr_in client_addinfo;

	socklen_t client_addinfo_length;
	client_addinfo_length=sizeof(struct sockaddr_in);

	signal(SIGINT,sigint);

	// 傳送公用message用
	//int public_shmid;
	if((public_shmid=shmget(SHMKEY,1024,IPC_CREAT | 0666))==-1)
        error_message("shmget1");
	public_shm=(char *)shmat(public_shmid,(char *)0,0);

	// 存user資訊用
	//int private_shmid;
	if((private_shmid=shmget(SHMKEY1,30*sizeof(ALL_USER),IPC_CREAT | 0666))==-1)
        error_message("shmget2");
	private_shm=(ALL_USER *)shmat(private_shmid,(char *)0,0);

	for(int i=0;i<30;i++)
		initial_user(i);

	//int information_shmid;
	if((information_shmid=shmget(SHMKEY2,sizeof(INFORMATION),IPC_CREAT | 0666))==-1)
        error_message("shmget3");
	information_shm=(INFORMATION *)shmat(information_shmid,(char *)0,0);

	information_shm->user_number=0;

	signal(SIGUSR1,sigusr1_handler);
	signal(SIGUSR2,sigusr2_handler);

	while(1)
	{
		if((new_sockfd=accept(sockfd,(struct sockaddr*)&client_addinfo,&client_addinfo_length))==-1)
			error_message("accept");

		if(!fork())
		{
			close(sockfd);

			for(int j=0;j<30;j++)
			{
				if(private_shm[j].id==-1)
				{
					self_position=j;

					private_shm[j].pid=getpid();
					private_shm[j].id=j+1;
					strcpy(private_shm[j].name,default_name);
					//strcpy(private_shm[j].ip,inet_ntoa(client_addinfo.sin_addr));
					//private_shm[j].port=(int)ntohs(client_addinfo.sin_port);
					strcpy(private_shm[j].ip,"CGILAB");
					private_shm[j].port=511;
					private_shm[j].socket=new_sockfd;

					information_shm->user_number++;

					break;
				}
			}

			if(information_shm->user_number!=1) //只有自己
			{
				for(int j=0;j<30;j++)
				{
					if(private_shm[j].id!=-1 && j!=self_position)
					{
						for(int i=0;i<100;i++)
							private_shm[self_position].public_pipe_exist[i]=private_shm[j].public_pipe_exist[i];
					}
				}
			}

			if(send(new_sockfd,welcome_message,strlen(welcome_message),0)==-1)
				error_message("send");

			memset(public_shm,'\0',1024);
			//sprintf(public_shm,user_enter,default_name,inet_ntoa(client_addinfo.sin_addr),(int)ntohs(client_addinfo.sin_port));
			sprintf(public_shm,user_enter,default_name,"CGILAB",511);						
			
			broadcast();

			child_work(new_sockfd);

			memset(public_shm,'\0',1024);
			sprintf(public_shm,user_left,private_shm[self_position].name);
			
			broadcast();
			
			initial_user(self_position);

			information_shm->user_number--;

			close(new_sockfd);
			exit(0);
		}
		else
			close(new_sockfd);
	}

	return 0;
}