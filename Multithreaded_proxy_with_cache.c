#include "proxy_parse.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <sys/wait.h>
#include <errno.h>
#include <pthread.h>
#include <semaphore.h>
#include <time.h>

#define MAX_CLIENTS 400 
#define MAX_ELEMENT_SIZE 10*(1<<20)    
#define MAX_SIZE 200*(1<<20)
#define MAX_BYTES 4096


typedef struct cache_element cache_element;
struct cache_element{
    char* data;
    int len;
    char* url;
    time_t lru_time_track;
     cache_element* next;
};

//Function declarations
//find function returns a memory address of a cache element if found. 
cache_element* find(char* url);
int add_cache_element(char* data, int size, char* url);
void remove_cache_element();

int port_number = 8080;
int proxy_socketId; //socket descriptor of proxy server

// array for thread id storing
pthread_t tid[MAX_CLIENTS];

// if client req exceeds the max_client then semaphore puts the waiting threads to sleep and wakes then when traffic on queue decreases 
sem_t seamaphore; 
//using mutex lock
pthread_mutex_t lock;

//pointer to the cache
cache_element* head;
//current size of cache
int cache_size;





int sendErrorMessage(int socket, int status_code)
{
	char str[1024];
	char currentTime[50];
	time_t now = time(0);

	struct tm data = *gmtime(&now);
	strftime(currentTime,sizeof(currentTime),"%a, %d %b %Y %H:%M:%S %Z", &data);

	switch(status_code)
	{
		case 400: snprintf(str, sizeof(str), "HTTP/1.1 400 Bad Request\r\nContent-Length: 95\r\nConnection: keep-alive\r\nContent-Type: text/html\r\nDate: %s\r\nServer: VaibhavN/14785\r\n\r\n<HTML><HEAD><TITLE>400 Bad Request</TITLE></HEAD>\n<BODY><H1>400 Bad Rqeuest</H1>\n</BODY></HTML>", currentTime);
				  printf("400 Bad Request\n");
				  send(socket, str, strlen(str), 0);
				  break;

		case 403: snprintf(str, sizeof(str), "HTTP/1.1 403 Forbidden\r\nContent-Length: 112\r\nContent-Type: text/html\r\nConnection: keep-alive\r\nDate: %s\r\nServer: VaibhavN/14785\r\n\r\n<HTML><HEAD><TITLE>403 Forbidden</TITLE></HEAD>\n<BODY><H1>403 Forbidden</H1><br>Permission Denied\n</BODY></HTML>", currentTime);
				  printf("403 Forbidden\n");
				  send(socket, str, strlen(str), 0);
				  break;

		case 404: snprintf(str, sizeof(str), "HTTP/1.1 404 Not Found\r\nContent-Length: 91\r\nContent-Type: text/html\r\nConnection: keep-alive\r\nDate: %s\r\nServer: VaibhavN/14785\r\n\r\n<HTML><HEAD><TITLE>404 Not Found</TITLE></HEAD>\n<BODY><H1>404 Not Found</H1>\n</BODY></HTML>", currentTime);
				  printf("404 Not Found\n");
				  send(socket, str, strlen(str), 0);
				  break;

		case 500: snprintf(str, sizeof(str), "HTTP/1.1 500 Internal Server Error\r\nContent-Length: 115\r\nConnection: keep-alive\r\nContent-Type: text/html\r\nDate: %s\r\nServer: VaibhavN/14785\r\n\r\n<HTML><HEAD><TITLE>500 Internal Server Error</TITLE></HEAD>\n<BODY><H1>500 Internal Server Error</H1>\n</BODY></HTML>", currentTime);
				  //printf("500 Internal Server Error\n");
				  send(socket, str, strlen(str), 0);
				  break;

		case 501: snprintf(str, sizeof(str), "HTTP/1.1 501 Not Implemented\r\nContent-Length: 103\r\nConnection: keep-alive\r\nContent-Type: text/html\r\nDate: %s\r\nServer: VaibhavN/14785\r\n\r\n<HTML><HEAD><TITLE>404 Not Implemented</TITLE></HEAD>\n<BODY><H1>501 Not Implemented</H1>\n</BODY></HTML>", currentTime);
				  printf("501 Not Implemented\n");
				  send(socket, str, strlen(str), 0);
				  break;

		case 505: snprintf(str, sizeof(str), "HTTP/1.1 505 HTTP Version Not Supported\r\nContent-Length: 125\r\nConnection: keep-alive\r\nContent-Type: text/html\r\nDate: %s\r\nServer: VaibhavN/14785\r\n\r\n<HTML><HEAD><TITLE>505 HTTP Version Not Supported</TITLE></HEAD>\n<BODY><H1>505 HTTP Version Not Supported</H1>\n</BODY></HTML>", currentTime);
				  printf("505 HTTP Version Not Supported\n");
				  send(socket, str, strlen(str), 0);
				  break;

		default:  return -1;

	}
	return 1;
}




int connectRemoteServer(char* host_addr,int port_num){

    //create socket for remote server
    int remoteSocket = socket(AF_INET, SOCK_STREAM, 0);
    if(remoteSocket<0){
        printf("Error in Creating socket\n");
        return -1;
    }
    //get host by the name or ip address provided

    struct hostent* host = gethostbyname(host_addr);
    if(host==NULL){
        fprintf(stderr,"No such host exist\n");
        return -1;
    }

    //Insert Ip addr and port number of host in struct `server_addr`
    struct sockaddr_in server_addr;
    bzero((char*)&server_addr,sizeof(server_addr));
    //sets family to ipv4
    server_addr.sin_family=AF_INET;
    server_addr.sin_port=htons(port_num);

    //copy the host address to the server address
    bcopy((char*)host->h_addr,(char*)&server_addr.sin_addr.s_addr,host->h_length);

    //connect to remote server
    if(connect(remoteSocket,(struct sockaddr*)&server_addr,(socklen_t)sizeof(server_addr))<0){
        fprintf(stderr,"Error in connecting!\n");
        return -1;
    }
    return remoteSocket;
}
 
int handle_request(int clientSocket,ParsedRequest *request, char *tempReq){
    char *buf = (char*)malloc(sizeof(char)*MAX_BYTES);
    strcpy(buf,"GET ");
    strcat(buf,request->path);
    strcat(buf," ");
    strcat(buf, request->version);
    strcat(buf,"\r\n");

    size_t len = strlen(buf);

    if(ParsedHeader_set(request,"Connection","close")<0){
        printf("set header key not work\n");
    }
    if(ParsedHeader_get(request,"Host")==NULL){
        if(ParsedHeader_set(request,"Host",request->host)<0){
            printf("Set \"Host\" header key not working\n");
        }
    }
    if (ParsedRequest_unparse_headers(request, buf + len, (size_t)MAX_BYTES - len) < 0) {
		printf("unparse failed\n");
		//return -1;				// If this happens Still try to send request without header
	}

	int server_port = 80;				// Default Remote Server Port
	if(request->port != NULL)
		server_port = atoi(request->port);

	int remoteSocketID = connectRemoteServer(request->host, server_port);

	if(remoteSocketID < 0)
		return -1;

	int bytes_send = send(remoteSocketID, buf, strlen(buf), 0);

	bzero(buf, MAX_BYTES);

	bytes_send = recv(remoteSocketID, buf, MAX_BYTES-1, 0);
	char *temp_buffer = (char*)malloc(sizeof(char)*MAX_BYTES); //temp buffer
	int temp_buffer_size = MAX_BYTES;
	int temp_buffer_index = 0;

	while(bytes_send > 0)
	{
		bytes_send = send(clientSocket, buf, bytes_send, 0);
		
		for(int i=0;i<bytes_send/sizeof(char);i++){
			temp_buffer[temp_buffer_index] = buf[i];
			// printf("%c",buf[i]); // Response Printing
			temp_buffer_index++;
		}
		temp_buffer_size += MAX_BYTES;
		temp_buffer=(char*)realloc(temp_buffer,temp_buffer_size);

		if(bytes_send < 0)
		{
			perror("Error in sending data to client socket.\n");
			break;
		}
		bzero(buf, MAX_BYTES);

		bytes_send = recv(remoteSocketID, buf, MAX_BYTES-1, 0);

	} 
	temp_buffer[temp_buffer_index]='\0';
	free(buf);
	add_cache_element(temp_buffer, strlen(temp_buffer), tempReq);
	printf("Done\n");
	free(temp_buffer);
	
	
 	close(remoteSocketID);
	return 0;
}

int checkHTTPversion(char *msg){
    int version = -1;
    if(strncmp(msg,"HTTP/1.1",8)==0){
        version=1;
    }
    else if(strncmp(msg,"HTTP/1.0",8)==0){
        version=1;
    }else version=-1;
    return version;
}

void* thread_fn(void* socketNew){
    //decrements (locks) the semaphore and checks the value of the semaphore if the value of semaphore is -ve then it is wait untill the value is +ve
    sem_wait(&seamaphore);
    int p;
    //retrieves the current value and store in P
    sem_getvalue(&seamaphore,&p);
    printf("semaphore value:%d\n",p);

    //socket init
    int* t = (int*)(socketNew);
    int socket = *t; //socket is socket descriptor of the connected client

   //bytes send to the client
    int bytes_send_client, len; //bytes transferred
      //buffer allocation
      //creating buffer of 4kb for a client
      char *buffer = (char*)calloc(MAX_BYTES,sizeof(char));
      //making buffer 0
      bzero(buffer,MAX_BYTES);
      // Receiving the Request of client by proxy server and store it in buffer
      bytes_send_client = recv(socket,buffer,MAX_BYTES,0);

      while(bytes_send_client>0){
        len = strlen(buffer);
        //Checks if the HTTP header termination sequence (\r\n\r\n) is in the buffer. If not, it continues to receive more data.
        if(strstr(buffer, "\r\n\r\n")==NULL){
            //recv (socket, start, end, 0);
            bytes_send_client=recv(socket,buffer+len, MAX_BYTES-len,0);
        }
        else {
            break;
        }
      }
      printf("--------------------------------------------\n");
	printf("%s\n",buffer);
	printf("----------------------%d----------------------\n",strlen(buffer));
    //copy of req (array)
    char * tempReq = (char*)malloc(strlen(buffer)*sizeof(char)+1);

    //tempreq, buffer both store the http req sent by the client
    for(int i=0; i<strlen(buffer);i++){
        tempReq[i]=buffer[i];
    }
    //checking for the req in cache
    struct cache_element* temp = find(tempReq);
    if(temp!=NULL){
        //req found
        int size = temp->len/sizeof(char);
        //position in cached data 
        int pos = 0;
        char response[MAX_BYTES];
        while(pos<size){
            bzero(response,MAX_BYTES);
            for(int i=0;i<MAX_BYTES;i++){
                response[i]=temp->data[pos];
                pos++;
            }
            send(socket,response,MAX_BYTES,0);
        }
        printf("Data retrived from the Cache\n\n");
		printf("%s\n\n",response);
    }
    //NOT UNDERSTOOD WHY DONE
    //No element in cache
    	else if (bytes_send_client > 0)
	{
		len = strlen(buffer);
		ParsedRequest* request = ParsedRequest_create();

		if (ParsedRequest_parse(request, buffer, len) < 0)
		{
			printf("Parsing failed\n");
		}
		else
		{
			bzero(buffer, MAX_BYTES);
			if (!strcmp(request->method, "GET"))
			{
				if (request->host && request->path && (checkHTTPversion(request->version) == 1))
				{
					bytes_send_client = handle_request(socket, request, tempReq);
                    //if nothing comes from server
					if (bytes_send_client == -1)
					{
						sendErrorMessage(socket, 500);
					}
				}
				else
					sendErrorMessage(socket, 500);
			}
			else
			{
				printf("This code doesn't support any method other than GET\n");
			}
		}
        //free the req pointer
		ParsedRequest_destroy(request);
	}
        else if( bytes_send_client < 0)
	{
		perror("Error in receiving from client.\n");
	}

    //if no req from client, means client is disconnected
	else if(bytes_send_client == 0)
	{
		printf("Client disconnected!\n");
	}

    shutdown(socket, SHUT_RDWR);
	close(socket);
	free(buffer);
	sem_post(&seamaphore);

	sem_getvalue(&seamaphore, &p);
	printf("Semaphore post value:%d\n", p);
	free(tempReq);
	return NULL;
}

int main(int argc, char *argv[]){
    int client_socketId, client_len;
    struct sockaddr_in server_addr, client_addr;

    //Initializing a semaphore
    sem_init(&seamaphore,0,MAX_CLIENTS);
    pthread_mutex_init(&lock,NULL);

    if(argc==2){
        port_number = atoi(argv[1]);

    }else{
        printf("Too few arguments\n");
        printf("Give port number");

        exit(1);
    }
    //Create the proxy socket
    proxy_socketId = socket(AF_INET, SOCK_STREAM, 0);
    if(proxy_socketId <0){
        perror("Failed to create socket.\n");
        exit(1);
    }
    int reuse = 1;
    if(setsockopt(proxy_socketId,SOL_SOCKET,SO_REUSEADDR, (const char *)&reuse, sizeof(reuse))<0){
        perror("setsocketopt(SO_REUSEADDR) failed\n");
    }
    //zeroing the structure to avoid undefined behaviour
    bzero((char*)&server_addr,sizeof(server_addr));
    //setting ip address family(IPV4) ,port, ip address
    server_addr.sin_family=AF_INET;
    server_addr.sin_port=htons(port_number);
    server_addr.sin_addr.s_addr=INADDR_ANY;

    //bind
    if(bind(proxy_socketId,(struct sockaddr *)&server_addr, sizeof(server_addr))<0){
        perror("Port is not free\n");
        exit(1);
    }
    printf("Binding on port: %d\n",port_number);

    //listen
    int listen_status = listen(proxy_socketId,MAX_CLIENTS);
    if(listen_status<0){
        perror("Error while listening!!\n");
        exit(1);
    }
    //i variable is to check how many clients are connected
    int i =0;
    //store socket descriptors for connected clients;
    int Connected_socketId[MAX_CLIENTS];

    //accept connection
    while(1){
        bzero((char*)&client_addr,sizeof(client_addr));
        client_len=sizeof(client_addr);

        client_socketId = accept(proxy_socketId,(struct sockaddr *)&client_addr, (socklen_t *)&client_len);

        if(client_socketId<0){
            fprintf(stderr,"Error in accepting connection!\n");
            exit(1);
        }else{
            Connected_socketId[i]=client_socketId;
        }

        //getting IP address and port number of the client
        struct sockaddr_in* client_pt=(struct sockaddr_in*)&client_addr;
        struct in_addr ip_addr=client_pt->sin_addr;
        char str[INET_ADDRSTRLEN];
        inet_ntop(AF_INET,&ip_addr,str, INET_ADDRSTRLEN);
        printf("Client is connected with port number: %d and ip address: %s \n",ntohs(client_addr.sin_port),str);

        //creating a new thread
        pthread_create(&tid[i],NULL,thread_fn,(void*)&Connected_socketId[i]);
        i++;

    }

    
    return 0;
}


cache_element* find(char*url){
    //checks the url in the cache 

    cache_element* site = NULL;
    int temp_lock_val=pthread_mutex_lock(&lock);
    printf("Find cache Lock Acquired %d\n",temp_lock_val);
    if(head!=NULL){
        site = head;
        while(site!=NULL){
            //strcmp returns 0 for identical
            if(!strcmp(site->url,url)){
                printf("LRU time track before : %ld", site->lru_time_track);
                printf("\nURL found\n");
                //update the time
                site->lru_time_track=time(NULL);
                printf("LRU Time track after: %ld",site->lru_time_track);
                break;
            }
            site=site->next;
        } 
    }
    else{
        printf("URL not found\n");
    }
    temp_lock_val = pthread_mutex_unlock(&lock);
    printf("Find cache Lock Unlocked %d\n",temp_lock_val);
    return site;
}
void remove_cache_element() {
    // If cache is not empty, search for the node with the least lru_time_track and delete it
    cache_element *prev = NULL;   // Pointer to the previous element
    cache_element *current = head;  // Pointer to the current element
    cache_element *least_used = head;  // Pointer to the least used element
    cache_element *least_used_prev = NULL;  // Pointer to the element before the least used element

    pthread_mutex_lock(&lock);
    printf("Remove Cache Lock Acquired\n");

    // Traverse the cache to find the least recently used element
    while (current != NULL) {
        if (current->lru_time_track < least_used->lru_time_track) {
            least_used = current;
            least_used_prev = prev;
        }
        prev = current;
        current = current->next;
    }

    // Remove the least recently used element
    if (least_used == head) {
        head = head->next;  // Handle the base case where the head is the least used
    } else if (least_used_prev != NULL) {
        least_used_prev->next = least_used->next;  // Bypass the least used element
    }

    // Update cache size
    cache_size -= (least_used->len + sizeof(cache_element) + strlen(least_used->url) + 1);

    // Free the removed element's data
    free(least_used->data);
    free(least_used->url);
    free(least_used);

    pthread_mutex_unlock(&lock);
    printf("Remove Cache Lock Unlocked\n");
}

int add_cache_element(char* data,int size,char* url){
    int temp_lock_val = pthread_mutex_lock(&lock);
    printf("Add Cache Lock Acquired %d\n", temp_lock_val);

    //element size = size of data+ null terminator \0+ length of url+ size of cache element (URL,timestamp)
    int element_size = size+1+strlen(url)+sizeof(cache_element);

    if(element_size>MAX_ELEMENT_SIZE){
        temp_lock_val=pthread_mutex_unlock(&lock);
        printf("Add Cache Lock Unlocked %d\n", temp_lock_val);
        printf("Size of the element is greater than max limit");
        return 0;
    }
    else{
        while(cache_size+element_size>MAX_SIZE){
        //we keep removing untill we get enough space
        remove_cache_element();
        }
        cache_element* element = (cache_element*) malloc(sizeof(cache_element));

        element->data = (char*)malloc(size+1);
        strcpy(element->url,url);
        element->lru_time_track=time(NULL);
        element->next=head;
        element->len=size;
        head=element;
        cache_size+=element_size;
        temp_lock_val=pthread_mutex_unlock(&lock);
        printf("Add Cache Lock Unlocked %d\n", temp_lock_val);
        return 1;

    }
    return 0;

}
