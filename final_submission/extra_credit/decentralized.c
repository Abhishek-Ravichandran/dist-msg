#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <assert.h>
#include <string.h>
#include <semaphore.h>
#include <time.h>
#include <fcntl.h>
#include <pthread.h>
#include <ifaddrs.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <signal.h>

#define BUFFER_SIZE 2000
#define SHARED 0

struct user {
    char user_name[20];
    struct sockaddr_in user_socket;
    int hasJoinedChat;
    
    //checked on receiver's side for duplication testing
    int last_seq_no_rcvd;
    
    //checked on sender's side for failure testing
    int last_seq_no_acked;
    
    //highest sequence number suggested by user - checked on sender's side
    int highest_suggested;
    
    //highest sequence number seen
    int highest_seen;
    
    time_t time_last_alive;
};

//struct for user linked list object
struct userListObj {
    struct user* userObj;
    struct userListObj* next;
};

//struct for message linked list 
struct messageListObj {
    char* msg;
    struct messageListObj* next;
};

// struct for threaded queue access
struct queueObj {
    struct messageListObj* headMsg;
    pthread_mutex_t queue_mutex;
    sem_t queue_sem;
};

//struct for connect thread
struct connectThreadObj {
    sem_t joinedSem;  
};

struct connectThreadObj* connectObj;

//local user object
struct user* local_user;

//connecting user socket object
struct sockaddr_in connecting_socket;

//user list object
struct userListObj* head;

//local socket fd
int local_socket_fd;

//new chat
int new_chat = 1;

//highest sequence number request sent
int highest_suggested = 0;

//local sequence number
int local_seq_no = 0;

void print_user_list();

char* convertUsertoString(struct user *user_obj) {
    char* payload;

    asprintf(&payload,  "%s\n"
                        "%s\n"    
                        "%d\n"
                        "%d\n"
                        "%d\n",  
                        
                        user_obj->user_name,
                        inet_ntoa(user_obj->user_socket.sin_addr), 
                        ntohs(user_obj->user_socket.sin_port),  
                        user_obj->hasJoinedChat,
                        user_obj->highest_seen);

    return payload;
}

char* getPayload(char* msg_type, char* message) {
    char* payload;
    
    asprintf(&payload,  "%s"                     
                        "%s",
                        msg_type, 
                        message);
    
    return payload;
}

char* add_new_user_to_list(char* str) {
    
    char* payload = "";
    
    //declare new user entry
    struct userListObj* newEntry = (struct userListObj*)malloc(sizeof(struct userListObj));
    struct user* newUser = (struct user*)malloc(sizeof(struct user));
    newEntry->userObj = newUser;
    
    struct userListObj* curr = head;
    
    //get to end of list
    if(curr != NULL){
        while(curr->next != NULL) {
            curr = curr->next;
        }
    }
    
    //assign name
    strcpy(newEntry->userObj->user_name, strtok(str, "\n"));
    
    //assign socket attributes
    newEntry->userObj->user_socket.sin_family = AF_INET;
    inet_aton(strtok(NULL, "\n"), &(newEntry->userObj->user_socket.sin_addr));
	newEntry->userObj->user_socket.sin_port = htons(atoi(strtok(NULL, "\n")));
	bzero(&(newEntry->userObj->user_socket.sin_zero), 8);
	
	//set has joined chat
	newEntry->userObj->hasJoinedChat = 0;
	
	//assign last message sequence number received
    newEntry->userObj->last_seq_no_rcvd = 0;
    
    //assign last message sequence number acked
    newEntry->userObj->last_seq_no_acked = 0;
    
    //assign highest suggested sequence no
    newEntry->userObj->highest_suggested = -1;
    
    //assign highest seen message sequence number
    newEntry->userObj->highest_seen = local_user->highest_seen + 1;
    
    //assign user time last alive
    newEntry->userObj->time_last_alive = time(NULL);
    
    //add user to list
  	newEntry->next = NULL;
  	if(curr != NULL)
  	 	curr->next= newEntry;
  	else
  	    head = newEntry;

    //create join notice
  	asprintf(&payload, "notice\nNOTICE %s joined on %s:%d\n", 
  	                                                newEntry->userObj->user_name, 
  	                                                inet_ntoa(newEntry->userObj->user_socket.sin_addr), 
  	                                                ntohs(newEntry->userObj->user_socket.sin_port));
  	
  	return payload;
}

void update_user_list(char* user_list_string) {
    
    struct userListObj* curr = head;
    
    //free memory for previous list
    // while(curr != NULL) {
    //     struct userListObj* temp = curr;
    //     curr = curr->next;
    //     free(temp->userObj);
    //     free(temp);
    // }
    
    head = NULL;
    char* user_to_add = strtok(user_list_string, ",");

    while(user_list_string != NULL) {
        user_list_string = strtok(NULL, "\0");
        
        struct userListObj* newEntry = (struct userListObj*)malloc(sizeof(struct userListObj));
        struct user* newUser = (struct user*)malloc(sizeof(struct user));
        newEntry->userObj = newUser;
    
        struct userListObj* curr = head;
        
        //get to end of list
        if(curr != NULL){
            while(curr->next != NULL) {
                curr = curr->next;
            }
        }
    
        //assign name
        strcpy(newEntry->userObj->user_name, strtok(user_to_add, "\n"));
    
        //assign socket attributes
        newEntry->userObj->user_socket.sin_family = AF_INET;
        inet_aton(strtok(NULL, "\n"), &(newEntry->userObj->user_socket.sin_addr));
	    newEntry->userObj->user_socket.sin_port = htons(atoi(strtok(NULL, "\n")));
	    bzero(&(newEntry->userObj->user_socket.sin_zero), 8);
	
	    //set has joined chat
	    newEntry->userObj->hasJoinedChat = 1;
	    char* temp = strtok(NULL, "\n");
	    
	    newEntry->userObj->last_seq_no_acked = 0;
	    
	    newEntry->userObj->last_seq_no_rcvd = 0;
	    
	    newEntry->userObj->highest_suggested = -1;
	
        newEntry->userObj->highest_seen = atoi(strtok(NULL, "\n"));
        
        newEntry->userObj->time_last_alive = time(NULL);

        if(newEntry->userObj->user_socket.sin_addr.s_addr == local_user->user_socket.sin_addr.s_addr) {
            if(newEntry->userObj->user_socket.sin_port == local_user->user_socket.sin_port) {
                if(local_user->hasJoinedChat == 0) {
                    local_user = newEntry->userObj;
                    highest_suggested = local_user->highest_seen;
                }
                else
                    newEntry->userObj = local_user;
            }
        }
    
        //add user to list
      	newEntry->next = NULL;
  	    if(curr != NULL)
  	 	    curr->next= newEntry;
  	    else
  	        head = newEntry;
        
        user_to_add = strtok(user_list_string, ",");
    }

}

void add_msg_to_queue(char* text, struct queueObj* queue) {
    pthread_mutex_lock(&(queue->queue_mutex));
    struct messageListObj* curr = queue->headMsg;
    struct messageListObj* newMsg= (struct messageListObj*)malloc(sizeof(struct messageListObj));
    newMsg->msg = text;
    newMsg->next = NULL;
    
    if(curr != NULL) {
        while(curr->next != NULL)
            curr = curr->next;
        curr->next = newMsg;
    }
    else {
        queue->headMsg = newMsg;
    }
    
    pthread_mutex_unlock(&(queue->queue_mutex));
    sem_post(&(queue->queue_sem));
}

void handle_msg(char* str, struct sockaddr_in sender_socket) {
    char* msg = strtok(str, "\n");
    int seq_no = atoi(strtok(NULL, "\n"));
    
    if(seq_no == local_user->highest_seen + 1) {
        printf("NEW MESSAGE SEQ NO: %d HIGHEST SEEN: %d\n", seq_no, local_user->highest_seen);
        local_user->highest_seen = seq_no;
        
        printf("%s\n", msg);
        
        printf("HIGHEST SUGGESTED: %d HIGHEST SEEN: %d\n", highest_suggested, local_user->highest_seen);
        if(highest_suggested < local_user->highest_seen) {
            printf("REVISING MY OPINIONS!\n");
            highest_suggested = local_user->highest_seen;
        }
    }
    
    char* payload;
    asprintf(&payload, "ack-msg\n%d\n", seq_no);
        
    if (sendto(local_socket_fd, payload, strlen(payload), 0, (struct sockaddr *) &(sender_socket), sizeof(struct sockaddr))==-1) {
        perror("Sendto error\n");
        exit(1);
    }
}

void handle_seq_no_request(char* str, struct sockaddr_in sender_socket) {
    
    int sender_seq_no = atoi(strtok(str, "\n"));
    
    struct userListObj* curr = head;
    
    while(curr != NULL) {
        if(curr->userObj->user_socket.sin_addr.s_addr == sender_socket.sin_addr.s_addr) {
            if(curr->userObj->user_socket.sin_port == sender_socket.sin_port) {
                if(curr->userObj->last_seq_no_rcvd < sender_seq_no) {
                    curr->userObj->last_seq_no_rcvd = sender_seq_no;
                    
                    char* payload;
                    asprintf(&payload, "sequence-num-proposed\n%d\n", ++highest_suggested);
                    printf("PROPOSING: %d\n", highest_suggested);
                    
                    if (sendto(local_socket_fd, payload, strlen(payload), 0, (struct sockaddr *) &(sender_socket), sizeof(struct sockaddr))==-1) {
                        perror("Sendto error\n");
    	                exit(1);
                    }
                    
                    break;
                }
            }
        }
        curr = curr->next;
    }
}

void handle_seq_no_proposed(char* str, struct sockaddr_in sender_socket) {

    int seq_no = atoi(strtok(str, "\n"));
    
    struct userListObj* curr = head;
    
    while(curr != NULL) {
        
        if(curr->userObj->user_socket.sin_addr.s_addr == sender_socket.sin_addr.s_addr) {
            if(curr->userObj->user_socket.sin_port == sender_socket.sin_port) {
                curr->userObj->highest_suggested = seq_no;
                break;
            }
        }
        
        curr = curr->next;
    }
}

void handle_msg_ack(char* str, struct sockaddr_in sender_socket) {
    int seq_no = atoi(strtok(str, "\n"));
    
    struct userListObj* curr = head;
    
    while(curr != NULL) {
        
        if(curr->userObj->user_socket.sin_addr.s_addr == sender_socket.sin_addr.s_addr) {
            if(curr->userObj->user_socket.sin_port == sender_socket.sin_port) {
                curr->userObj->last_seq_no_acked = seq_no;
                break;
            }
        }
        
        curr = curr->next;
    }
}

void handle_list(char* list, struct sockaddr_in sender_socket) {
    update_user_list(list);
    sem_post(&(connectObj->joinedSem));
}

void handle_ping(struct sockaddr_in sender_socket) {
    
    struct userListObj* curr = head;
    
    while(curr != NULL) {
        if(curr->userObj->user_socket.sin_addr.s_addr == sender_socket.sin_addr.s_addr) {
            if(curr->userObj->user_socket.sin_port == sender_socket.sin_port) {
                curr->userObj->time_last_alive = time(NULL);
                break;
            }
        }
        curr = curr->next;
    }
}

char* get_user_list() {
    char* payload = "";
    
    struct userListObj* curr = head;
    
    while(curr != NULL) {
        payload = getPayload(payload, convertUsertoString(curr->userObj));
        payload = getPayload(payload, ",");
        curr = curr->next;
    }
    
    payload = getPayload("list\n", payload);
    
    return payload;
}

void broadcast_user_list() {
    char* payload = get_user_list();
    
    struct userListObj* curr = head;
    
    while(curr != NULL) {
        
        if (sendto(local_socket_fd, payload, strlen(payload), 0, (struct sockaddr *) &(curr->userObj->user_socket), sizeof(struct sockaddr))==-1) {
            perror("Sendto error\n");
    	    exit(1);
    	    }
        
        curr = curr->next;
    }
}

void print_user_list() {         
    struct userListObj* curr = head;

    while(curr != NULL) {
        printf("%s %s:%d", curr->userObj->user_name, inet_ntoa(curr->userObj->user_socket.sin_addr),
                ntohs(curr->userObj->user_socket.sin_port));
        printf("\n");
        curr = curr->next;
    }
}

void remove_user_from_list(struct userListObj *toRemoveUserListObj, struct queueObj* queue) {
	struct userListObj *curr = head;
	char* payload;
	
	if(head->userObj->user_socket.sin_addr.s_addr == toRemoveUserListObj->userObj->user_socket.sin_addr.s_addr) {
	    if(head->userObj->user_socket.sin_port == toRemoveUserListObj->userObj->user_socket.sin_port) {
	        char* leftName;
	        asprintf(&leftName, "%s", head->userObj->user_name);
	        if(head->next != NULL) {
	            struct userListObj* temp = head->next;
	            free(head);
	            head = temp;
	            asprintf(&payload, "message\nNOTICE %s left the chat or crashed\n", leftName);
	            add_msg_to_queue(payload, queue);
	            broadcast_user_list();
	            return;
	        }
	    }
	}
	
	if(curr != NULL) {
	    while(curr->next != NULL) {
	        if(curr->next->userObj->user_socket.sin_addr.s_addr == toRemoveUserListObj->userObj->user_socket.sin_addr.s_addr) {
	            if(curr->next->userObj->user_socket.sin_port == toRemoveUserListObj->userObj->user_socket.sin_port) {
	                struct userListObj* temp = curr->next;
    	            curr->next = curr->next->next;
    	            char* payload;
    	            asprintf(&payload, "message\nNOTICE %s left the chat or crashed\n", temp->userObj->user_name);
    	            add_msg_to_queue(payload, queue);
    	            free(temp);
    	            broadcast_user_list();
    	            break;
	            }
	        }
	        curr = curr->next;
	    }
	}
}

void* checkSendQueue(void* args) {
    
    struct queueObj* queue = args;
    
    while(1) {
        sem_wait(&(queue->queue_sem));
        pthread_mutex_lock(&(queue->queue_mutex));
        
        char* msg_to_send = queue->headMsg->msg;
        
        struct userListObj* curr = head;
        
        int all_suggested = 1, all_ack = 0;
        
        char* payload;
        asprintf(&payload, "sequence-num-request\n%d\n", local_seq_no + 1);
        
        while(curr != NULL) {
            if(curr->userObj->highest_suggested == -1 && curr->userObj->hasJoinedChat == 1) {
                // printf("DIS GUY: %s NO: %d\n", curr->userObj->user_name, curr->userObj->highest_suggested);
                all_suggested = 0;
                if (sendto(local_socket_fd, payload, strlen(payload), 0, (struct sockaddr *) &(curr->userObj->user_socket), sizeof(struct sockaddr))==-1) {
    	            perror("Sendto error\n");
    	            exit(1);
                }
            }
            
            curr = curr->next;
        }
        
        if(all_suggested == 1) {
            
            curr = head;
            int max_seq_no = -1;
            
            while(curr != NULL) {
                if(curr->userObj->highest_suggested > max_seq_no && curr->userObj->hasJoinedChat == 1) {
                    max_seq_no = curr->userObj->highest_suggested;
                }
                curr = curr->next;
            }
            
            printf("HIGHEST SUGGESTED SEQ NO WAS: %d\n", max_seq_no);
            asprintf(&payload, "%s%d\n", msg_to_send, max_seq_no);
            
            curr = head;
            all_ack = 1;
            
            while(curr != NULL) {
                if(curr->userObj->last_seq_no_acked < highest_suggested && curr->userObj->hasJoinedChat == 1) {
                    // printf("HASNT ACKED: %s\n", curr->userObj->user_name);
                    all_ack = 0;
                    
                    if (sendto(local_socket_fd, payload, strlen(payload), 0, (struct sockaddr *) &(curr->userObj->user_socket), sizeof(struct sockaddr))==-1) {
    	                perror("Sendto error\n");
    	                exit(1);
                    }
                }
                
                curr = curr->next;
            }
        }
            
        if(all_ack == 1) { 
            
            char* header = strtok(msg_to_send, "\n");
            
            if(strcmp(header, "notice") == 0)
                broadcast_user_list();
            
            queue->headMsg = queue->headMsg->next;
            
            curr = head;
            
            while(curr != NULL) {
                curr->userObj->highest_suggested = -1;
                curr = curr->next;
            }
            
            local_seq_no++;
        }
        else
            sem_post(&(queue->queue_sem));
        
        pthread_mutex_unlock(&(queue->queue_mutex));
        usleep(100*1000);
    }
}

//thread function for checking initial connection
void* checkConnected(void* args) {

    // struct connectThreadObj* connectObj = args;
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += 3;
    int ret = sem_timedwait(&(connectObj->joinedSem), &ts);

    if (ret == -1 && errno == ETIMEDOUT) {
        printf("Sorry, no chat is active on %s:%d, try again later.\nBye.\n",inet_ntoa(connecting_socket.sin_addr),ntohs(connecting_socket.sin_port));
        exit(1);
    } 
    else {
        printf("Succeeded, current users:\n");
		print_user_list();
		if(new_chat == 1) {
		    printf("Waiting for others to join...\n");
		    new_chat = 0;
		}
	    pthread_exit(NULL);
    }
}

void* pingOthers(void* args) {
    while(1) {
        struct userListObj* curr = head;
        
        while(curr != NULL) {
            char* payload = "ping\n";
            
            if (sendto(local_socket_fd, payload, strlen(payload), 0, (struct sockaddr *) &(curr->userObj->user_socket), sizeof(struct sockaddr))==-1) {
            perror("Sendto error\n");
    	    exit(1);
    	    }
    	    
    	    curr = curr->next;
        }
        
        sleep(3);
    }
}

void* checkAlive(void* args) {
    struct queueObj* sendQueue = args;
    
    while(1) {
        struct userListObj* curr = head;
        int currentTime = time(NULL);
        
        while(curr != NULL) {
            // printf("NAME: %s DELAY: %d\n", curr->userObj->user_name, currentTime - curr->userObj->time_last_alive);
            if(currentTime - curr->userObj->time_last_alive > 6) {
                remove_user_from_list(curr, sendQueue);
            }
            curr = curr->next;
        }
    
        sleep(2);
    }
}

int check_if_socket_used(struct sockaddr_in sender_socket) {
    struct userListObj* curr = head;
    
    while(curr != NULL) {
        if(curr->userObj->user_socket.sin_addr.s_addr == sender_socket.sin_addr.s_addr) {
            if(curr->userObj->user_socket.sin_port == sender_socket.sin_port) {
                return 1;
            }
        }
        curr = curr->next;
    }
    
    return 0;
}

void refuse_connection(struct sockaddr_in sender_socket) {
    
    char* payload = "cant-connect";
    
    if (sendto(local_socket_fd, payload, strlen(payload), 0, (struct sockaddr *) &(sender_socket), sizeof(struct sockaddr))==-1) {
        perror("Sendto error\n");
        exit(1);
    }
}

int main(int argc, char* argv[]) {
    
    // signal(SIGINT, killHandler);
    
    //return value for select
    int select_retval = 0;
    
    //number of bytes received
    int num_bytes = 0;
    
    //size of socket object
    int size_of_socket = sizeof(struct sockaddr);
    
    //thread start tracker
    int haveStarted = 0;
    
    //buffer for recvfrom
    char buffer[BUFFER_SIZE];
    
    //payload for sendto
    char* payload;
    
    //sender socket object
    struct sockaddr_in sender_socket;
    
    local_user = (struct user*)malloc(sizeof(struct user));
    
    head = (struct userListObj*)malloc(sizeof(struct userListObj));
    
    //check command-line args
    if(argc != 2 && argc != 3) {
		printf("Usage: ./dchat (NAME) | (NAME CLIENT_IP:CLIENT_PORT)\n");
		exit(1);
	}
	
	//init local user object
    local_user = (struct user*)malloc(sizeof(struct user));
    
    //init struct for send queue access
    struct queueObj* sendQueue = (struct queueObj*)malloc(sizeof(struct queueObj));
    sendQueue->headMsg = NULL;
    sem_init(&(sendQueue->queue_sem), SHARED, 0);
    
    //init struct for connect thread
    connectObj = (struct connectThreadObj*)malloc(sizeof(struct connectThreadObj));
    sem_init(&(connectObj->joinedSem), SHARED, 0);
    
    //readset for select
    fd_set readset;
    FD_ZERO(&readset);
    
    //declare threads
    pthread_t sendThread;
    pthread_t connectThread;
    pthread_t pingThread;
    pthread_t checkThread;
    
    //initialize local UDP socket
	if((local_socket_fd = socket(AF_INET, SOCK_DGRAM, 0)) == -1) {
		printf("Socket Error");
		exit(1);
	}
	
    //set local user attributes
    strcpy(local_user->user_name, argv[1]);
    local_user->last_seq_no_rcvd = 0;
    local_user->last_seq_no_acked = 0;
    local_user->highest_suggested = -1;
    local_user->highest_seen = 0;
    local_user->hasJoinedChat = 0;
    local_user->time_last_alive = time(NULL);
    
    //set local user socket attributes
    local_user->user_socket.sin_family = AF_INET;
    local_user->user_socket.sin_port = htons(8000);
    bzero(&(local_user->user_socket.sin_zero), 8);
    
    //set local user IP address
    struct ifaddrs * ifAddrStruct=NULL;
    struct ifaddrs * ifa=NULL;
    void * tmpAddrPtr=NULL;

    char addressBuffer[INET_ADDRSTRLEN];

    getifaddrs(&ifAddrStruct);

    for (ifa = ifAddrStruct; ifa != NULL; ifa = ifa->ifa_next) {
        if (!ifa->ifa_addr) {
            continue;
        }
        if (ifa->ifa_addr->sa_family == AF_INET) { // check it is IP4
            // is a valid IP4 Address
            tmpAddrPtr=&((struct sockaddr_in *)ifa->ifa_addr)->sin_addr;
            inet_ntop(AF_INET, tmpAddrPtr, addressBuffer, INET_ADDRSTRLEN);
            if(strcmp(ifa->ifa_name, "em1") == 0 || strcmp(ifa->ifa_name, "eth0") == 0)
                break;
        }
    }
    
    if (ifAddrStruct!=NULL) freeifaddrs(ifAddrStruct);
    inet_aton(addressBuffer, &(local_user->user_socket.sin_addr));

    //bind socket to port
    while (bind(local_socket_fd, (struct sockaddr *)&(local_user->user_socket), size_of_socket) < 0) {
        local_user->user_socket.sin_port = htons(ntohs(local_user->user_socket.sin_port) + 1);
    }
    
    //if new chat
    if (argc == 2) {
        head->userObj = local_user;
        printf("%s started a new chat, listening on %s:%d\n", local_user->user_name,inet_ntoa(local_user->user_socket.sin_addr),ntohs(local_user->user_socket.sin_port));
	    sem_post(&(connectObj->joinedSem));
	    local_user->hasJoinedChat = 1;
	    if(pthread_create(&pingThread, NULL, &pingOthers, NULL) != 0) {
	        perror("Pthread Create");
	        exit(1);
	   }
	   
	   if(pthread_create(&checkThread, NULL, &checkAlive, (void*)sendQueue) != 0) {
	       perror("Pthread Create");
	       exit(1);
	   }
	   haveStarted = 1;
	} 
	//if connecting to existing chat
	else {
	    new_chat = 0;
	    //set connecting socket attributes
	    connecting_socket.sin_family = AF_INET;
	    inet_aton(strtok(argv[2], ":"), &(connecting_socket.sin_addr));
	    connecting_socket.sin_port = htons(atoi(strtok(NULL, "\0")));
	    bzero(&(connecting_socket.sin_zero), 8);
	    printf("%s joining a new chat on %s:%d, listening on %s:%d\n", local_user->user_name, inet_ntoa(connecting_socket.sin_addr), ntohs(connecting_socket.sin_port),
	                inet_ntoa(local_user->user_socket.sin_addr), ntohs(local_user->user_socket.sin_port));
	    
	    if (connecting_socket.sin_port == local_user->user_socket.sin_port) {
	        if (connecting_socket.sin_addr.s_addr == local_user->user_socket.sin_addr.s_addr) {
	            printf("Sorry, no chat is active on %s:%d, try again later.\nBye.\n",inet_ntoa(connecting_socket.sin_addr),ntohs(connecting_socket.sin_port));
                exit(1); 
	        }
	    }
	    
	    payload = getPayload("join\n", convertUsertoString(local_user));
	    
	    //send join message to connecting user
	    if (sendto(local_socket_fd, payload, strlen(payload), 0, (struct sockaddr *) &connecting_socket, size_of_socket)==-1) {
	        printf("Sorry, no chat is active on %s:%d, try again later.\nBye.\n",inet_ntoa(connecting_socket.sin_addr),ntohs(connecting_socket.sin_port));
            exit(1); 
	    }
	}
	
	if (pthread_create(&sendThread, NULL, &checkSendQueue, (void *)sendQueue) != 0) {
    	perror("Pthread Create");
    	exit(1);
    }
    
    if (pthread_create(&connectThread, NULL, &checkConnected, NULL) != 0) {
    	perror("Pthread Create");
    	exit(1);
    }
    
	while(1) {
	    
	    //reset buffer
	    memset(buffer, '\0', BUFFER_SIZE - 1);
	    
	    //set STDIN and socket in select readset
	    FD_SET(0, &readset);
        FD_SET(local_socket_fd, &readset);
        
        select_retval = select(local_socket_fd + 1, &readset, NULL, NULL, NULL);
        
        
        //exit if invalid select return value
        if (select_retval < 0) {
            perror("Select");
            exit(1);
        }
        
        if (select_retval > 0) {
            
            //something occurred on socket
            if (FD_ISSET(local_socket_fd, &readset)) {
            
                //read bytes from socket
                if ((num_bytes = recvfrom(local_socket_fd, buffer, BUFFER_SIZE - 1, 0, (struct sockaddr *)&sender_socket, &size_of_socket)) == -1) {
        			perror("Recv Error");
        			exit(1);
        		}
        		
        		
        		buffer[num_bytes] = '\0';
        //  		printf("%s\n", buffer);
                
                //get message type and message
        		char* msg_type = strtok(buffer, "\n");
        		char* msg = strtok(NULL, "\0");
        		
        		if(strcmp(msg_type, "join") == 0) {
        		    if(check_if_socket_used(sender_socket) == 1) {
        		        refuse_connection(sender_socket);
        		    }
        		    else
        		        add_msg_to_queue(add_new_user_to_list(msg), sendQueue);
        		}
        		
        		if(strcmp(msg_type, "list") == 0) {
        		    handle_list(msg, sender_socket);
        		    
        		    if(haveStarted == 0) {
        		        if(pthread_create(&pingThread, NULL, &pingOthers, NULL) != 0) {
                            perror("Pthread Create");
                        	exit(1);
                        }
                        
                        if(pthread_create(&checkThread, NULL, &checkAlive, (void*)sendQueue) != 0) {
                            perror("Pthread Create");
                        	exit(1);
                        }
                        haveStarted = 1;
        		    }
        		    
        		  //  continue;
        		}
        		
        		if(strcmp(msg_type, "message") == 0) {
        		    handle_msg(msg, sender_socket);
        		  //  continue;
        		}
        		
        		if(strcmp(msg_type, "notice") == 0) {
        		    handle_msg(msg, sender_socket);
        		  //  continue;
        		}
        		
        		if(strcmp(msg_type, "sequence-num-request") == 0) {
        		    handle_seq_no_request(msg, sender_socket);
        		  //  continue;
        		}
        		
        		if(strcmp(msg_type, "ack-msg") == 0) {
        		    handle_msg_ack(msg, sender_socket);
        		  //  continue;
        		}
        		
        		if(strcmp(msg_type, "sequence-num-proposed") == 0) {
        		    handle_seq_no_proposed(msg, sender_socket);
        		  //  continue;
        		}
        		
        		if(strcmp(msg_type, "ping") == 0) {
        		    handle_ping(sender_socket);
        		  //  continue;
        		}
        		
        		if(strcmp(msg_type, "cant-connect") == 0) {
        		    printf("Socket already in use!\n");
        		    exit(0);
        		}
        		
        		
            }
            
            //something occurred on stdin
        	if(FD_ISSET(0, &readset)) {
        	    
        	    //read text from stdin into buffer
        	    memset(buffer, '\0', BUFFER_SIZE - 1);
                num_bytes = read(0, buffer, sizeof(buffer));
                buffer[num_bytes] = '\0';
                
                if(strcmp(buffer, "\0") == 0) {

                    exit(0);
                }
                else {
                      asprintf(&payload, "message\n%s%s", getPayload(local_user->user_name, "::"), buffer);
                      add_msg_to_queue(payload, sendQueue);
                }
                
            }
        }
	}
	
	return 0;
}