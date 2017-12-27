#include <stdio.h>
#include <fcntl.h>
#include <string.h>
#include <sys/types.h> 
#include <sys/socket.h> 
#include <errno.h>
#include <unistd.h>
#include <sys/time.h>
#include <stdlib.h>
#include <sys/select.h>
#include <pthread.h>
#include <signal.h>
#include <netinet/in.h> 
#include <arpa/inet.h> 
#include <netdb.h>
#include <time.h> 


//GLOBAL VARIABLES DECLARATIN/INIT--------------------------------------------------------------------------------------------------------------------------------
struct CacheElement 
{
	unsigned char * response;
	char request_url[200];
	size_t content_length;
	size_t total_size;
	int cache_status;
	int hit_count;
	struct  CacheElement *  next;
};

typedef struct CacheElement CacheElement;
CacheElement * cache_list;

int open_sockets[50],socket_count = 0;
pthread_mutex_t lock;
static int c_size,CacheElement_count;


//STARTING THE SERVER----------------------------------------------------------------------------------------------------------------------------

int startserver() 
{
	int sd,s_port;
	char * s_hostname; 
	char hostname[256];
	struct sockaddr_in servaddr;
	struct hostent * hostentry;
	socklen_t size;
	
	sd = socket(AF_INET, SOCK_STREAM, 0);
	if(socket < 0)
	{
		fprintf(stderr, "Error in creating socket %s\n",strerror(errno) );
		return sd;
	}
	
	memset(&servaddr, '0', sizeof(servaddr));
	servaddr.sin_family = AF_INET;
	servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
	servaddr.sin_port = 0;
//BIND&LISTEN
	if(bind(sd,(struct sockaddr *)&servaddr, sizeof(servaddr)) != 0)
	{
		fprintf(stderr, "Error in binding socket => %s\n",strerror(errno) );
		return -1;
	}
	listen(sd, 5);
//GET HOST NAME
	if(gethostname(hostname, 256) == -1)
	{
		fprintf(stderr, "host name error => %s\n",strerror(errno) );
		return -1;
	}
	hostentry = gethostbyname(hostname);
	s_hostname = hostentry->h_name;	
	size = sizeof(servaddr);
//SOCKET DETAILS
	if(getsockname(sd, (struct sockaddr *)&servaddr, &size) == -1)
	{
		fprintf(stderr, "port number error => %s\n",strerror(errno) );
		return -1;
	}

	s_port = ntohs(servaddr.sin_port);	
	printf("\tHost name: '%s' \n\tport number: '%d'\n", s_hostname, s_port);	
	return (sd);
}


//HOOK ON TO SERVER-------------------------------------------------------------------------------------------------------------------------------------
int hooktoserver(char *s_hostname, int s_port) 
{
	int sd,i,c_port; 
	struct sockaddr_in servaddr, clientaddr;
	struct hostent *hostentry;
	struct in_addr **addr_list;
	char *server_ip;
	socklen_t clientaddrsize = sizeof(clientaddr);
	memset(&servaddr, '0', sizeof(servaddr));
	
	if((sd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
	{
		fprintf(stderr, "Error in creating socket %s\n", strerror(errno));
		return sd;
	}

	servaddr.sin_family = AF_INET;
	servaddr.sin_port = htons(s_port);	
	
	if((hostentry = gethostbyname(s_hostname)) == NULL)
	{
		fprintf(stderr, "Error in creating socket %s\n", strerror(errno));
		return -1;
	}

	addr_list = (struct in_addr **) hostentry->h_addr_list;

	for(i = 0; addr_list[i] != NULL; i++)
	{
		servaddr.sin_addr = *addr_list[i];
		break;
	}
//CONNECTION ESTABLISHING
	if(connect(sd, (struct sockaddr *)&servaddr, sizeof(servaddr)) < 0)
	{
		printf("Error: Connection failed : %s", strerror(errno));
		return -1;
	}
	
	fflush(stdout);
	return (sd);
}


//PARSING THE REQUEST 
int parse(char * request, char * host_name, int host_name_size, int * host_port,  char * request_url,int request_url_size)
{
	char dummy_req[1000];
	char temp[request_url_size];
	char temp1[host_name_size];
	char *token;
	int found = 0;
	strncpy(dummy_req, request, 1000);
	token = strtok(dummy_req, "\n");
	while(token != NULL)
	{
		if(sscanf(token,"GET%sHTTP/1.1", temp) != 0)
		{
			strncpy(request_url, temp, request_url_size);
			found ++;
		}
		if(sscanf(token,"Host: %s", temp) != 0)
		{
			strncpy(temp1, temp, host_name_size);	
			found ++;
		}
		token = strtok(NULL, "\n");
	}
	if(found == 2)
	{
		token = strtok(temp1, ":");
		strncpy(host_name, token, host_name_size);

		if((token = strtok(NULL, ":")) != NULL)  *host_port = atoi(token);
		else *host_port = 80;		
		return 1;
	}
	else	return 0;
	
}


//CHECK IF IN CACHE
CacheElement *is_in_cache(CacheElement ** cache_list, char * request_url)
{
	CacheElement * t_CacheElement=*cache_list;
	while(t_CacheElement != NULL)
	{
		if(strcmp(t_CacheElement->request_url, request_url) == 0)
		{
			t_CacheElement->cache_status = 1; t_CacheElement->hit_count++;
			return t_CacheElement;
		}
		t_CacheElement = t_CacheElement->next;
	}
	return NULL;
}

void LRU()
{
	c_size=sizeof(CacheElement)*100;
}

	
//ADD WEBPAGE TO CACHE
void add_to_cache(CacheElement ** cache_list, CacheElement * t_CacheElement)
{	if(c_size>0)
	{		
		CacheElement * temp_CacheElement; CacheElement_count++;
		temp_CacheElement = * cache_list;
		if(temp_CacheElement == NULL)
		{
			*cache_list = t_CacheElement;
			return;
		}
		while(temp_CacheElement -> next != NULL)
		{
			temp_CacheElement = temp_CacheElement->next;
		}
		temp_CacheElement->next = t_CacheElement; t_CacheElement->hit_count=1;
		c_size-=sizeof(t_CacheElement);
	}
	else
	{
		LRU();
		add_to_cache(cache_list, t_CacheElement);
		

	}

}


//generate request
CacheElement * make_request(CacheElement ** cache_list, char * host_name, int host_port, char * request_url, char * request)
{
	int server_socket,cont_size;
	server_socket = hooktoserver(host_name, host_port);
	unsigned char * temp_response;
	size_t total_size,content_length,init_size,size,remaining_bytes,temp; 
	unsigned char * response;	
	char dummy_resp[1000];
	char *token, *remaining; 
	size_t * content_size=&content_length;	
	CacheElement *t_CacheElement;

	if(!server_socket)
	{
		printf(" Error connecting to host %s\n", host_name);
		return NULL;
	}
	if(!send(server_socket, request, strlen(request), 0))
	{
		perror(" Error sending request to host %s");
		close(server_socket);
		return NULL;
	}
	temp_response = malloc(1000 * sizeof(char));
	if(!(total_size = recv(server_socket, temp_response, 1000, 0)))
	{
		perror(" Error in sending request to host %s");
		close(server_socket);
		return NULL;
	}
	
	response = malloc(total_size * sizeof( unsigned char));
	memcpy(response, temp_response, total_size);	
	strncpy(dummy_resp, (char *)temp_response, total_size);
	token = strtok(dummy_resp, "\n");
	while(token != NULL)
	{
		if(sscanf(token, "Content-Length: %zd", &temp) != 0)
		{
			*content_size = temp;
		}
		token = strtok(NULL, "\n");
	}
	if((remaining = strstr((char *)temp_response, "\r\n\r\n")) != NULL)
	{
		remaining_bytes = *content_size - ((char *)temp_response + total_size - remaining) + 4;
		cont_size= remaining_bytes; goto cont;
	}
	cont_size= -1; goto cont;

cont:
	if(cont_size == -1)
	{
		printf(" Error in getting content size");
		close(server_socket);
		return NULL;
	}
	else remaining_bytes=cont_size;
	free(temp_response);
	if(remaining_bytes > 0)
	{
		init_size = total_size;
		total_size += remaining_bytes;
		temp_response = malloc(remaining_bytes * sizeof(unsigned char));
		int readval;
		size_t toberead;
		unsigned char * ptr;

		toberead = remaining_bytes;
		ptr = temp_response;

		while (toberead > 0) 
		{
				size_t byteread;
		
				byteread = read(server_socket, ptr, toberead);
				if (byteread <= 0) 
				{
					if (byteread == -1)
					{
						//perror("read");
						readval=0; goto read_bytes;
					}
					readval=1; goto read_bytes;
				}

				toberead -= byteread;
				ptr += byteread;
		}
			readval=1; goto read_bytes;

read_bytes:
		if(readval == 0)
		{

			perror("  Error in getting response body from server");
			close(server_socket);
			return NULL;
		}
		size=readval;
		response = realloc(response, total_size * sizeof(unsigned char));
		memcpy(response + init_size, temp_response, remaining_bytes);
		free(temp_response);
	}
	close(server_socket);
	t_CacheElement = malloc(sizeof(CacheElement));
	t_CacheElement->response = NULL;
	memset(t_CacheElement->request_url, '\0', 200);
	t_CacheElement->next = NULL;
	t_CacheElement->cache_status = 0;
	t_CacheElement->response = response;
	t_CacheElement->total_size = total_size;
	t_CacheElement->content_length = content_length;
	strcpy(t_CacheElement->request_url, request_url);
	return t_CacheElement;
}

//RECIEVE REQUESTS
CacheElement * get_request(CacheElement **cache_list, char*host_name, int  host_port, char * request_url, char * request)
{
	CacheElement * t_CacheElement;
	t_CacheElement = is_in_cache(cache_list, request_url);
	if(t_CacheElement != NULL)
	{
		return t_CacheElement;
	}
	t_CacheElement = make_request(cache_list, host_name, host_port, request_url, request);
	if(t_CacheElement != NULL)
	{
		add_to_cache(cache_list, t_CacheElement);
	}
	return t_CacheElement;
}


//SOCKET FUNCTIONS
void remove_socket(int socket)
{
	int i;
	pthread_mutex_lock(&lock);
	for(i =0; i < socket_count; i ++)
	{
		if(socket == open_sockets[i])
	{
			open_sockets[i] = open_sockets[socket_count - 1];
			socket_count --;
			break;
		}
	}
	pthread_mutex_unlock(&lock);
}

void * new_client(void * client_sock)
{
	int client_socket, i,c_port,host_port,res;
	struct timespec start, stop;
	clock_gettime(NULL, &start);
	pthread_detach(pthread_self());
	client_socket = * (int *)client_sock;
	char * c_hostname; 
	struct hostent * cliententry;
	struct in_addr * clientaddress;
	struct sockaddr_in clientaddr;
	socklen_t clientaddrlen = sizeof(clientaddr);
	memset(&clientaddr, '0', sizeof(clientaddr));
	char request[1000],host_name[64], request_url[200];	
	size_t tobesent;
	unsigned char * ptr;

	CacheElement * t_CacheElement;
	if(getpeername(client_socket,(struct sockaddr *) &clientaddr, &clientaddrlen) == -1)
	{
		perror("Error in get peer name");
	}

	clientaddress = &clientaddr.sin_addr;	
	c_hostname = inet_ntoa(*clientaddress);
	c_port = ntohs(clientaddr.sin_port);

	if(recv(client_socket, request, 1000, 0) == 0)
	{
		printf("The request not received properly\n");
		goto final;
	}
	if(parse(request, host_name,64, &host_port, request_url , 200) == 0)
	{
		printf("  Error cannot parse request\n");
		goto final;	
	}
	
	t_CacheElement = get_request(&cache_list, host_name, host_port, request_url, request);
	if(t_CacheElement == NULL)
	{
		printf("Error in getting response from cache\n");
		goto final;
	}	
	tobesent = t_CacheElement->total_size;
	ptr = t_CacheElement->response;
	while (tobesent > 0) 
	{
		size_t bytesent;
	
		bytesent = write(client_socket, ptr, tobesent);
		if (bytesent <= 0) {
			if (bytesent == -1)
			{
				perror("write error");
				res=0; goto sendn;
			}
			res=1; goto sendn;
		}
		tobesent -= bytesent;
		ptr += bytesent;
	}
	res=1; goto sendn;

sendn:
	if(!res)
	{
		perror(" Error in sending response to client");
		goto final;
	}

	clock_gettime(NULL, &stop);
	float timedif=(stop.tv_sec - start.tv_sec) * 1000 + (stop.tv_nsec - start.tv_nsec) / 100000;

	printf("\n----------------------------\nHOSTNAME:%s\nURL:%s\nCACHE STATUS:%s\nREQ-SIZE:%d\nACCESS TIME:%d", c_hostname, t_CacheElement->request_url, (t_CacheElement->cache_status == 0)?"CACHE_MISS" : "CACHE_HIT", t_CacheElement->content_length);
final:

	remove_socket(client_socket);
	close(client_socket);
	return NULL;
}

//ACCEPT NEW CLIENT

void * accept_new_client()
{	
	int servsock, clientsock;
	struct sockaddr_in clientaddr;

	socklen_t clientaddrlen = sizeof(clientaddr);
	servsock = startserver();
	pthread_t c_thread;
	if (servsock == -1)
	{
		perror("Error on starting server: ");
		exit(1);
	}
	while(1)
	{
		memset(&clientaddr, '0', sizeof(clientaddr));
		char * c_hostname;
		int c_port; 
		struct hostent * cliententry;
		struct in_addr * clientaddress;
		if((clientsock = accept(servsock, (struct sockaddr *)&clientaddr, (socklen_t *) &clientaddrlen)) < 0)
		{
			fprintf(stderr, "Error in accepting new socket \n");
			continue;
		}
		pthread_mutex_lock(&lock);
		open_sockets[socket_count] = clientsock;
		socket_count ++;
		pthread_mutex_unlock(&lock);
		clientaddress = &clientaddr.sin_addr;
		cliententry = gethostbyaddr((char * )clientaddress, sizeof(clientaddress), AF_INET);
		if(cliententry == NULL)
		{
			c_hostname = inet_ntoa(*clientaddress);
		}
		else
			c_hostname = cliententry->h_name;
		
		c_port = ntohs(clientaddr.sin_port);
		if(pthread_create(&c_thread, NULL, new_client, &clientsock) != 0)
		{
			perror("Cannot create worker thread");
			remove_socket(clientsock);
			close(clientsock);
			close(servsock);
			pthread_exit(NULL);
		}
	}
	c_size-=sizeof(CacheElement);
	pthread_exit(NULL);
}

//MAIN

int main(int argc, char *argv[]) {
	
	if (argc != 2)
	{
		fprintf(stderr, "<usage :> %s <size>\n", argv[0]);
		exit(1);
	}
	int c_size= atoi(argv[1]);
	printf("cache of size %d is created\n",c_size);


	CacheElement_count=0;
	pthread_t sthread;
	sigset_t sigmask;
	sigemptyset (&sigmask);
	sigaddset(&sigmask, SIGPIPE);


	if(pthread_sigmask(SIG_BLOCK, &sigmask, NULL) != 0)
	{
		perror("Error in Masking Signal");
		exit(-1);
	}

	if (pthread_mutex_init(&lock, NULL) != 0)
	{
		perror("Error in creating lock");
    
	}

	if(pthread_create(&sthread, NULL, accept_new_client, NULL) < 0)
	{
		perror("Error in creating server thread");
		exit(-1);
	}
	
	pthread_join(sthread, NULL);
	pthread_mutex_destroy(&lock);
	
	return 1;
}



