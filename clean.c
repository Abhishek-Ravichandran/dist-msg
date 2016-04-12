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

#define BUFFER_SIZE 2000
#define SHARED 0

//global variables

//declare user list
struct userListObj* head;

//declare local user
struct user* local_user;

//local socket fd
int local_socket_fd = 0;

//sequence number of latest message sent out/received
int last_seq_no_sent = 0;
int last_seq_no_rcvd = 0;

//coordinator socket object
struct sockaddr_in coordinator_socket;

//struct for user object
struct user {
    int user_id;
    char user_name[20];
    struct sockaddr_in user_socket;
    int is_leader;
    int lastAcknowledgedMsg;
    time_t time_last_alive;
    int hasJoinedChat;
    int isAlive;
    int isHoldingElection;
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

void updateLastAlive(struct user *obj) {
    obj->time_last_alive = time(NULL);
}

char* convertUsertoString(struct user *user_obj) {
    char* payload;

    asprintf(&payload,  "%d\n"                     
                        "%s\n"
                        "%s\n"    
                        "%d\n"
                        "%d\n"
                        "%d\n"
                        "%d\n",  
                        user_obj->user_id, user_obj->user_name, inet_ntoa(user_obj->user_socket.sin_addr), ntohs(user_obj->user_socket.sin_port), \
                            user_obj->is_leader, user_obj->lastAcknowledgedMsg, user_obj->time_last_alive);

    return payload;
}

char* getNumAsString(int num) {
    char* string;
    
    asprintf(&string, "%d\n", num);
    
    return string;
}

char* getPayload(char* msg_type, char* message) {
    char* payload;
    
    asprintf(&payload,  "%s"                     
                        "%s",
                        msg_type, message);
    
    return payload;
}

char* add_user_string_to_list(char* user_string) {
    char* payload = "";
    struct userListObj* curr = head;
    
    //declare new user entry
    struct userListObj* newEntry = (struct userListObj*)malloc(sizeof(struct userListObj));
    struct user* newUser = (struct user*)malloc(sizeof(struct user));
    newEntry->userObj = newUser;

    //get to end of list
    if(curr != NULL){
        while(curr->next != NULL) {
            curr = curr->next;
        }
    }
    
    //set new user attributes
    if (local_user->is_leader == 1) {
        newEntry->userObj->user_id = curr->userObj->user_id + 1;
        char* temp = strtok(user_string, "\n");
    }
    else
    if (local_user->is_leader != 1) {
        newEntry->userObj->user_id = atoi(strtok(user_string, "\n"));
    }
    strcpy(newEntry->userObj->user_name, strtok(NULL, "\n"));
    newEntry->userObj->user_socket.sin_family = AF_INET;
    inet_aton(strtok(NULL, "\n"), &(newEntry->userObj->user_socket.sin_addr));
	newEntry->userObj->user_socket.sin_port = htons(atoi(strtok(NULL, "\n")));
	bzero(&(newEntry->userObj->user_socket.sin_zero), 8);
	newEntry->userObj->is_leader = atoi(strtok(NULL, "\n"));
  	updateLastAlive(newEntry->userObj);
  	if(local_user->is_leader == 1)
  	    newEntry->userObj->lastAcknowledgedMsg = local_user->lastAcknowledgedMsg + 1;
  	else
  	    newEntry->userObj->lastAcknowledgedMsg = atoi(strtok(NULL, "\n"));
  	newEntry->userObj->isAlive = 1;
  	
  	//add user to list
  	newEntry->next = NULL;
  	if(curr != NULL)
  	 	curr->next= newEntry;
  	else
  	    head = newEntry;
  	
  	if(local_user->is_leader == 1) {
  	    asprintf(&payload, "message-request\nNOTICE %s joined on %s:%d\n", newEntry->userObj->user_name, inet_ntoa(newEntry->userObj->user_socket.sin_addr), 
  	        ntohs(newEntry->userObj->user_socket.sin_port));
  	}
  	
  	return payload;
}

void remove_user_from_list(struct userListObj *toRemoveUserListObj) {
	struct userListObj *curr = head;
	
	if(curr != NULL) {
	    while(curr->next != NULL) {
	        if(curr->next->userObj->user_id == toRemoveUserListObj->userObj->user_id) {
	            struct userListObj* temp = curr->next;
	            curr->next = curr->next->next;
	            free(temp);
	            break;
	        }
	        curr = curr->next;
	    }
	}
}

void broadcast_user_list() {
    char* payload = "";
    
    struct userListObj* curr = head;
    
    while(curr != NULL) {
        payload = getPayload(payload, convertUsertoString(curr->userObj));
        payload = getPayload(payload, ",");
        curr = curr->next;
    }
    
    payload = getPayload("list\n", payload);
    
    curr = head->next;
    
    while(curr != NULL) {
        if (sendto(local_socket_fd, payload, strlen(payload), 0, \
            (struct sockaddr *) &(curr->userObj->user_socket), sizeof(struct sockaddr))==-1) {
                perror("Send Error\n");
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
        if(curr->userObj->is_leader)
            printf(" (leader)");
        printf("\n");
        curr = curr->next;
    }
}

void update_user_list(char* user_list_string) {
    
    struct userListObj* curr = head;
    
    //free memory for previous list
    while(curr != NULL) {
        struct userListObj* temp = curr;
        curr = curr->next;
        free(temp->userObj);
        free(temp);
    }
    
    head = NULL;
    char* user_to_add = strtok(user_list_string, ",");

    while(user_list_string != NULL) {
        user_list_string = strtok(NULL, "\0");
        char* temp = add_user_string_to_list(user_to_add);
        user_to_add = strtok(user_list_string, ",");
    }
    
    curr = head;
    
    while(curr != NULL) {
        if(strcmp(curr->userObj->user_name, local_user->user_name) == 0) {
            if(curr->userObj->lastAcknowledgedMsg == 0 || local_user->lastAcknowledgedMsg == 0) {
                last_seq_no_rcvd = curr->userObj->lastAcknowledgedMsg;
                local_user->lastAcknowledgedMsg = curr->userObj->lastAcknowledgedMsg;
            }
            local_user->user_id = curr->userObj->user_id;
            break;
        }
        curr = curr->next;
    }
}

void send_join_msg(char *str, struct user* obj) {
    char *payload;
    char* temp = strtok(str,"\n");
    temp = strtok(NULL,"\n");
    inet_aton(strtok(NULL,"\n"), &(coordinator_socket.sin_addr));
    coordinator_socket.sin_port = htons(atoi(strtok(NULL,"\n")));
    coordinator_socket.sin_family = AF_INET;
    bzero(&(coordinator_socket.sin_zero), 8);
    
    payload = getPayload("join\n",convertUsertoString(obj));
    
    if (sendto(local_socket_fd, payload, strlen(payload), 0, (struct sockaddr *) &(coordinator_socket), sizeof(struct sockaddr))==-1) {
	    perror("Sendto error\n");
	    exit(1);
	}
}

void handle_message_ack(char* str) {
    int id = atoi(strtok(str, "\n"));
    int msgNo = atoi(strtok(NULL, "\0"));
    
    struct userListObj* curr = head;
    
    while(curr != NULL) {
        if(curr-> userObj->user_id == id) {
            curr->userObj->lastAcknowledgedMsg = msgNo;
            break;
        }
        curr = curr->next;
    }
}

void handle_msg(char* str) {
    char* payload;
    char* msg_to_print = strtok(str, "\n");
    int msg_sq_no = atoi(strtok(NULL, "\n"));
    
    if(msg_sq_no > last_seq_no_rcvd) {
        printf("%s\n", msg_to_print);
        last_seq_no_rcvd = msg_sq_no;
    }
    
    payload = getPayload(getPayload("ack-msg\n", getNumAsString(local_user->user_id)), getNumAsString(msg_sq_no));
    
    if (sendto(local_socket_fd, payload, strlen(payload), 0, (struct sockaddr *) &(head->userObj->user_socket), sizeof(struct sockaddr))==-1) {
	    perror("Sendto error\n");
	    exit(1);
	}
}

void handle_ping(char* str, struct sockaddr_in sender_socket) {
    char* payload = "ping-ack\n";
    int received_id = atoi(strtok(str, "\n"));
    struct userListObj* curr = head;
    while(curr != NULL) {
        if(curr->userObj->user_id == received_id) {
            updateLastAlive(curr->userObj);
            break;
        }
        curr = curr->next;
    }
    if (sendto(local_socket_fd, payload, strlen(payload), 0, (struct sockaddr *) &(sender_socket), sizeof(struct sockaddr))==-1) {
	    perror("Sendto error\n");
	    exit(1);
	}
}

void handle_alive_req(struct sockaddr_in sender_socket) {
    char* payload = getPayload("alive-yes\n", getNumAsString(local_user->user_id));
    if (sendto(local_socket_fd, payload, strlen(payload), 0, (struct sockaddr *) &(sender_socket), sizeof(struct sockaddr))==-1) {
	    perror("Sendto error\n");
	    exit(1);
	}
}

void handle_alive_yes(char* str, struct sockaddr_in sender_socket) {
    int id = atoi(strtok(str, "\n"));
    
    struct userListObj* curr = head;
    while(curr != NULL) {
        if(curr->userObj->user_id == id) {
            curr->userObj->isAlive = 1;
            break;
        }
        curr = curr->next;    
    }
}

struct queueObj* add_msg_to_queue(char* text, struct queueObj* queue) {
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
    return queue;
}

void handle_user_left(struct userListObj *curr, struct queueObj* queue) {
	char* payload;
	char leftName[20];
	sprintf(leftName, "%s", curr->userObj->user_name);
	remove_user_from_list(curr);
	broadcast_user_list();
	asprintf(&payload, "message-request\nNOTICE %s left the chat or crashed\n", leftName);
    add_msg_to_queue(payload, queue);
}

void handle_quit(char* str, struct queueObj* queue) {
    int id = atoi(strtok(str, "\n"));
    
    struct userListObj* curr = head;
    
    while(curr != NULL) {
        if(curr->userObj->user_id == id)
            break;
        curr = curr->next;
    }
    
    handle_user_left(curr, queue);
}

void print_queue(struct queueObj* queue) {
    pthread_mutex_lock(&(queue->queue_mutex));
    struct messageListObj* curr = queue->headMsg;
    
    while(curr != NULL) {
        printf("%s\n", curr->msg);
        curr = curr->next;
    }
    pthread_mutex_unlock(&(queue->queue_mutex));
}

//thread function for holding election
void* hold_election(void* args) {
    struct userListObj* curr = head;
    struct queueObj* queue = args;

    char* payload = "alive-req\n";

    while(curr != NULL) {
        if(curr->userObj->user_id < local_user->user_id) {
            if (sendto(local_socket_fd, payload, strlen(payload), 0, (struct sockaddr *) &(curr->userObj->user_socket), sizeof(struct sockaddr))==-1) {
        	    perror("Sendto error\n");
        	    exit(1);
        	}
        }
        curr->userObj->isAlive = 0;
        curr = curr->next;
    }
    
    sleep(4);
    
    curr = head;

    while(curr != NULL) {
        if(curr->userObj->user_id < local_user->user_id) {
            if(curr->userObj->isAlive == 1) {
                pthread_exit(NULL);
            }
        }
        curr = curr->next;
    }
    
    curr = head;
    
    while(curr->next != NULL) {
        if(curr->next->userObj->user_id == local_user->user_id) {
            curr->next = curr->next->next;
            break;
        }
        curr = curr->next;
    }
    
    last_seq_no_rcvd = 0;
    local_user->lastAcknowledgedMsg = 0;
    
    while(curr != NULL) {
        curr->userObj->time_last_alive = time(NULL);
        curr->userObj->lastAcknowledgedMsg = last_seq_no_rcvd;
        curr = curr->next;
    }
    
    last_seq_no_sent = last_seq_no_rcvd;
    local_user->user_id = 1;
    local_user->is_leader = 1;
    
    char* string;
    asprintf(&string, "message-request\nNOTICE %s left the chat or crashed\n", head->userObj->user_name);
    
    head->userObj = local_user;
    broadcast_user_list();
    
    add_msg_to_queue(string, queue);
    local_user->isHoldingElection = 0;      //leader
    
    pthread_exit(NULL);
}
//thread function for checking send queue
void* checkSendQueue(void* args) {
    struct queueObj* queue = args;
    while(1) {
        sem_wait(&(queue->queue_sem));
        pthread_mutex_lock(&(queue->queue_mutex));
        char* msg_to_send = queue->headMsg->msg;
        
        if (sendto(local_socket_fd, msg_to_send, strlen(msg_to_send), 0, (struct sockaddr *) &(head->userObj->user_socket), sizeof(struct sockaddr))==-1) {
	        perror("Sendto error in send_join_message\n");
	        exit(1);
	    }
        
        queue->headMsg = queue->headMsg->next;
        pthread_mutex_unlock(&(queue->queue_mutex));
    }
}

//thread function for checking broadcast queue
void* checkBroadcastQueue(void* args) {
    struct queueObj* queue = args;
    char* payload;
    
    while(1) {
        sem_wait(&(queue->queue_sem));
        pthread_mutex_lock(&(queue->queue_mutex));
        char* msg_to_send = queue->headMsg->msg;
        
        struct userListObj* curr = head;
        
        int all_ack = 1;
        
        while(curr != NULL) {
            if(curr->userObj->lastAcknowledgedMsg != last_seq_no_sent + 1) {
                all_ack = 0;
                char last_seq_no_string[15];
                sprintf(last_seq_no_string, "%d\n", last_seq_no_sent + 1);
                payload = getPayload(msg_to_send, last_seq_no_string);
                if (sendto(local_socket_fd, payload, strlen(payload), 0, (struct sockaddr *) &(curr->userObj->user_socket), sizeof(struct sockaddr))==-1) {
    	            perror("Sendto error\n");
    	            exit(1);
                }
            }
            curr = curr->next;
        }
        
        if(all_ack == 1) {              //all acked, head of the queue is removed
            queue->headMsg = queue->headMsg->next;
            last_seq_no_sent += 1;
        }
        else
            sem_post(&(queue->queue_sem));
        
        pthread_mutex_unlock(&(queue->queue_mutex)); 
        sleep(1);
    }
}

//thread function for checking initial connection
void* checkConnected(void* args) {
    struct connectThreadObj* connectObj = args;
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += 3;
    int ret = sem_timedwait(&(connectObj->joinedSem), &ts);
    if (ret == -1 && errno == ETIMEDOUT) {
        printf("Sorry, no chat is active on %s:%d, try again later.\nBye.\n",inet_ntoa(coordinator_socket.sin_addr),ntohs(coordinator_socket.sin_port));
        exit(1);
    } 
    else {
        printf("Succeeded, current users:\n");
		print_user_list();
		if(local_user->is_leader == 1)
		    printf("Waiting for others to join...\n");
	    pthread_exit(NULL);
    }
}

//thread function for checking client liveness
void* checkClientTimeStamps(void* args) {
    struct userListObj* curr;
    struct queueObj* queue = args;
    char* payload;
    while(1) {
        if(local_user->is_leader == 1) {
            time_t currentTime = time(NULL);
            curr = head;
            while(curr != NULL) {
                if(curr->userObj->is_leader != 1) {
            	    int delay = currentTime - curr->userObj->time_last_alive;
                    // printf("USER: %s DELAY: %d\n", curr->userObj->user_name, delay);
                    if (delay > 6) {
            	        handle_user_left(curr, queue);
            	    }
                }
                curr = curr->next;
            }
        }
        sleep(3);
    }
}

//thread function for checking coordinator liveness
void* checkServerTimeStamp(void* args) {
    while(1) {
        if(local_user->isHoldingElection != 1 && local_user->is_leader != 1) {
            time_t currentTime = time(NULL);
            if(currentTime - head->userObj->time_last_alive > 6) {
                pthread_t electionThread;
                local_user->isHoldingElection = 1;
                if (pthread_create(&electionThread, NULL, &hold_election, (void*)args) != 0) {
                	perror("Pthread Create");
                	exit(1);
                }
                pthread_join(electionThread, NULL);
            }
        }
        sleep(2);
    }
}

//thread function for pinging coordinator
void* pingServer(void* args) {
    while(1) {
        if(local_user->is_leader != 1) {
	        // create payload
        	char *payload;
        	asprintf(&payload, "ping\n%d\n", local_user->user_id);
        	// send to coordinator
        	//making sure you don't ping yourself
    	    if (local_user->user_id != 1) {
        		if(sendto(local_socket_fd, payload, strlen(payload), 0, (struct sockaddr *) &(head->userObj->user_socket), sizeof(struct sockaddr)) == -1) {
        	            perror("Send error");
        	            exit(1);
        	    }
    	    }
        }
	    sleep(3);
	}
	
	pthread_exit(NULL);
}

int main(int argc, char* argv[]) {
    
    //check command-line args
    if(argc != 2 && argc != 3) {
		printf("Usage: ./dchat (NAME) | (NAME COORDINATOR_IP:COORDINATOR_PORT)\n");
		exit(1);
	}
    
    //for socket initialization
    int yes = 1;
    
    //return value for select
    int select_retval = 0;
    
    //number of bytes received
    int num_bytes = 0;
    
    //size of socket object
    int size_of_socket = sizeof(struct sockaddr);
    
    //message sequence number
    int sequence_number = 0;
    
    //buffer for recvfrom
    char buffer[BUFFER_SIZE];
    
    //payload for sendto
    char* payload;
    
    //sender socket object
    struct sockaddr_in sender_socket;
    
    //init local user object
    local_user = (struct user*)malloc(sizeof(struct user));
    
    //init struct for send queue access
    struct queueObj* sendQueue = (struct queueObj*)malloc(sizeof(struct queueObj));
    sendQueue->headMsg = NULL;
    sem_init(&(sendQueue->queue_sem), SHARED, 0);
    
    //init struct for broadcast queue access
    struct queueObj* broadcastQueue = (struct queueObj*)malloc(sizeof(struct queueObj));
    broadcastQueue->headMsg = NULL;
    sem_init(&(broadcastQueue->queue_sem), SHARED, 0);
    
    //init struct for connect thread
    struct connectThreadObj* connectObj = (struct connectThreadObj*)malloc(sizeof(struct connectThreadObj));
    sem_init(&(connectObj->joinedSem), SHARED, 0);
    
    //readset for select
    fd_set readset;
    // FD_ZERO(&readset);
    
    //declare threads
    pthread_t sendThread;
    pthread_t broadcastThread;
    pthread_t connectThread;
    pthread_t pingThread;
    pthread_t checkClientThread;
    pthread_t checkServerThread;
    
    //initialize local UDP socket
	if((local_socket_fd = socket(AF_INET, SOCK_DGRAM, 0)) == -1) {
		printf("Socket Error");
		exit(1);
	}
	
	//to reuse previous port
// 	if(setsockopt(local_socket_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) == -1) {
// 		perror("Socket Error");
// 		exit(1);
// 	}
    
    //set local user attributes
    local_user->user_id = 1;
    local_user->isHoldingElection = 0;
    strcpy(local_user->user_name, argv[1]);
    local_user->time_last_alive = time(NULL);
    local_user->lastAcknowledgedMsg = 0;
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
    
    //if coordinator
    if (argc == 2) {
		local_user->is_leader = 1;
		head = (struct userListObj*)malloc(sizeof(struct userListObj));
	    head->userObj = local_user;
	    head->next = NULL;
	    sem_post(&(connectObj->joinedSem));
	    local_user->hasJoinedChat = 1;
	    printf("%s started a new chat, listening on %s:%d\n", local_user->user_name,inet_ntoa(local_user->user_socket.sin_addr),ntohs(local_user->user_socket.sin_port));
	} 
	//if connecting to coordinator
	else {
	    local_user->is_leader = 0;
	    //set coordinator socket attributes
	    coordinator_socket.sin_family = AF_INET;
	    inet_aton(strtok(argv[2], ":"), &(coordinator_socket.sin_addr));
	    coordinator_socket.sin_port = htons(atoi(strtok(NULL, "\0")));
	    bzero(&(coordinator_socket.sin_zero), 8);
	    
	    
	    //get join payload
	    payload = getPayload("join\n", convertUsertoString(local_user));
	    
	    //send join message to coordinator
	    if (sendto(local_socket_fd, payload, strlen(payload), 0, (struct sockaddr *) &coordinator_socket, size_of_socket)==-1) {
	      perror("Sendto error\n");
	      exit(1);
	    }
	    
	    printf("%s joining a new chat on %s:%d, listening on %s:%d\n", local_user->user_name, inet_ntoa(coordinator_socket.sin_addr), ntohs(coordinator_socket.sin_port),
	                inet_ntoa(local_user->user_socket.sin_addr), ntohs(local_user->user_socket.sin_port));
	}
	
	//start threads
	if (pthread_create(&sendThread, NULL, &checkSendQueue, (void *)sendQueue) != 0) {
    	perror("Pthread Create");
    	exit(1);
    }
   
    if (pthread_create(&broadcastThread, NULL, &checkBroadcastQueue, (void *)broadcastQueue) != 0) {
    	perror("Pthread Create");
    	exit(1);
    }
 
    if (pthread_create(&connectThread, NULL, &checkConnected, (void *)connectObj) != 0) {
        perror("Pthread Create");
        exit(1);
    }
     
    if (pthread_create(&checkClientThread, NULL, &checkClientTimeStamps, (void *)sendQueue) != 0) {
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
        
        
        //handle event if something occurred
        if (select_retval > 0) {
            
            //something occurred on socket
            if (FD_ISSET(local_socket_fd, &readset)) {
            
                //read bytes from socket
                if ((num_bytes = recvfrom(local_socket_fd, buffer, BUFFER_SIZE - 1, 0, (struct sockaddr *)&sender_socket, &size_of_socket)) == -1) {
        			perror("Recv Error");
        			exit(1);
        		}
        		
        		
        		buffer[num_bytes] = '\0';
        // 		printf("%s\n", buffer);
                
        		//get message type and message
        		char* msg_type = strtok(buffer, "\n");
        		char* msg = strtok(NULL, "\0");
        		
        		//if join message
        		if(strcmp(msg_type, "join") == 0) {
        		    
        		    //if not coordinator, send coordinator info back to user
        		    if(local_user->is_leader == 0) {
        		        payload = getPayload("coordinator-info\n", convertUsertoString(head->userObj));
        		        
        		        if (sendto(local_socket_fd, payload, strlen(payload), 0, (struct sockaddr *) &sender_socket, size_of_socket)==-1) {
	      					perror("error in sending leader info\n");
	      					exit(1);
	    				}
        		    }
        		    //if coordinator, join user to user list and broadcast new list
        		    if(local_user->is_leader == 1) {
        		        payload = add_user_string_to_list(msg);
        		        broadcast_user_list();
        		        add_msg_to_queue(payload, sendQueue);
        		    }
        		    
        		}
        		
        		//if list message, update own user list
        		if(strcmp(msg_type, "list") == 0) {
        		    update_user_list(msg);
        		    local_user->isHoldingElection = 0;      //for other clients
        		    if(local_user->hasJoinedChat == 0) {
        		        sem_post(&(connectObj->joinedSem));
            		    if (pthread_create(&pingThread, NULL, &pingServer, NULL) != 0) {
            		        perror("Pthread Create");
            		        exit(1);
            		    }
            		    if (pthread_create(&checkServerThread, NULL, &checkServerTimeStamp, (void *)sendQueue) != 0) {
                        	perror("Pthread Create");
                        	exit(1);
                        }
        		        local_user->hasJoinedChat = 1;
        		    }
        		}
        		
        		//if leader-info message, send join message to coordinator
        		if(strcmp(msg_type, "coordinator-info") == 0) {
        		    send_join_msg(msg, local_user);
        		}
        		
        		//if message-request message, add message to queue
        		if(strcmp(msg_type, "message-request") == 0) {
        		    payload = getPayload("message\n", msg);
        		    broadcastQueue = add_msg_to_queue(payload, broadcastQueue);
        		}
        		
        		//if message, print - maybe add to another queue later?
        		if(strcmp(msg_type, "message") == 0) {
        		    handle_msg(msg);
        		}
        		
        		//if message ack - update last acknowledged msg of user
        		if(strcmp(msg_type, "ack-msg") == 0) {
        		    handle_message_ack(msg);
        		}
        		
        		//if ping - update time last alive of user
        		if(strcmp(msg_type, "ping") == 0) {
        		    handle_ping(msg, sender_socket);
        		}
        		
        		//if quit - handle user quit
        		if(strcmp(msg_type, "quit") == 0) {
        		    handle_quit(msg, sendQueue);
        		}
        		
        		//if ping ack - update head alive time
        		if(strcmp(msg_type, "ping-ack") == 0) {
        		    head->userObj->time_last_alive = time(NULL);
        		}
        		
        		//if alive req - respond alive yes
        		if(strcmp(msg_type, "alive-req") == 0) {
        		    handle_alive_req(sender_socket);
        		}
        		
        		//if alive req - respond alive yes
        		if(strcmp(msg_type, "alive-yes") == 0) {
        		    handle_alive_yes(msg, sender_socket);
        		}
    
        	}
        	
        	//something occurred on stdin
        	if(FD_ISSET(0, &readset)) {
        	    
        	    //read text from stdin into buffer
        	    memset(buffer, '\0', BUFFER_SIZE - 1);
                num_bytes = read(0, buffer, sizeof(buffer));
                buffer[num_bytes] = '\0';
                
                if(strcmp(buffer, "\0") == 0) {
                    if(local_user->is_leader != 1) {
                        payload = getPayload("quit\n", getNumAsString(local_user->user_id));
                        add_msg_to_queue(payload, sendQueue);
                    }
                    exit(0);
                }
                else {
        	    //push text into send queue
        	    payload = getPayload(getPayload("message-request\n", getPayload(local_user->user_name, "::")), buffer);
        	    sendQueue = add_msg_to_queue(payload, sendQueue);
                }
        	}
    
        }
        
	}
	
	return 0;
}