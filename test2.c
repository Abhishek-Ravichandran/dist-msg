#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <assert.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <semaphore.h>
#include <time.h>
#include <fcntl.h>
#include <pthread.h>

#define BUFFER_SIZE 2000

char *connectingIp;
int connectingPort;
int myPort;
int sequenceNumber;
struct user* my_user;
int our_socket_fd, size_of_socket, num_bytes;
int hasJoinedChat = 0;

// 
sem_t empty;
sem_t full;
sem_t mutex;
sem_t last_alive_mutex;

pthread_t awake_thread; // server checks client timestamps of last ping 
pthread_t send_thread;  // server checks hold back queue 
pthread_t ping_thread;  // client pings server ever 1 sec to indicate he's still awake 
pthread_t connect_thread;

clock_t start,end,diff;
struct user {
    char user_name[20];
    struct sockaddr_in user_addr;
    int is_leader;
    int id;
    clock_t lastAlive;
};

struct userListObj {				//linked list holding user info
    struct user* userObj;
    struct userListObj* next;
};

struct messageListObj {				//Hold back queue
	char message[BUFFER_SIZE];
	struct messageListObj* next;
};

struct userListObj *head = NULL;

char* getJoinPayload(struct user *my_user) {
  char *payload;

  asprintf(&payload,  "join\n"                     
                      "%s\n" \
                      "%s\n" \
                      "%d\n",  
                      my_user->user_name, inet_ntoa(my_user->user_addr.sin_addr), myPort);

  return payload;
}

char* getMessagePayload(char *message, char* username) {				//the message sent by the leader to the participants
  char *payload;

  asprintf(&payload,  "message\n"                     
                      "%s::%s" \
                      "%d\n",  
                      username, message, sequenceNumber);

  return payload;
}

char* getBulkMessagePayload(char *message) {
  char *payload;

  asprintf(&payload,  "message\n"                     
                      "%d\n",  
                      message, sequenceNumber);

  return payload;
}

char* getMessageRequestPayload(char *message, char* username) {		//message being sent to the leader
  char *payload;

  asprintf(&payload,  "message-request\n"                     
                      "%s::%s",
                      username, message);

  return payload;
}

char* convertUsertoString(struct user *userobj) {
  char *payload;

  asprintf(&payload,  "%s\n"                     
                      "%s\n" \
                      "%d\n" \
                      "%d\n" \
                      "%d\n",  
                      userobj->user_name, inet_ntoa(userobj->user_addr.sin_addr), ntohs(userobj->user_addr.sin_port), userobj->is_leader, userobj->id);

  return payload;
}

void updateLastAlive(struct user *obj) {
	sem_wait(&last_alive_mutex);
    obj->lastAlive = clock();
    sem_post(&last_alive_mutex);
}

void sendUserLeftMulticast(struct user *user) {
	struct userListObj *curr;
	curr = head->next;
	while(curr != NULL) {
		char *payload;
		asprintf(&payload, "notification\nNOTICE %s LEFT THE CHAT OR CRASHED", user->user_name);
        if(sendto(our_socket_fd, payload, strlen(payload), 0, (struct sockaddr *) &(curr->userObj->user_addr), sizeof(curr->userObj->user_addr) ) == -1) {
            perror("Send error");
            exit(1);
        }
        curr = curr-> next;
    }
}

void removeUserFromList(struct userListObj *userObj) {
	struct userListObj *curr;
	char *users,*temp_user;
	curr = head;
	
	while (curr != NULL) {
		if (curr->next == userObj) {
			curr->next = userObj->next;
			free(userObj);
		}
		curr = curr->next;
	}
	
	
	strcat(users, "list\n");
	curr = head;
	while(curr != NULL) {
	    temp_user = convertUsertoString(curr->userObj);
	    strcat(users,temp_user);
	    curr = curr->next;
	}
	
	printf("%s", users);
	
	curr = head->next;
	
	while(curr != NULL) {				//sending the list when a user joins the chat
	//	printf("Sending to: %s on %d\n", curr->userObj->user_name, curr->userObj->user_addr.sin_port);
	    if(sendto(our_socket_fd, users, strlen(users), 0, (struct sockaddr *) &(curr->userObj->user_addr), sizeof(curr->userObj->user_addr) ) == -1) {
	        perror("Send error");
	        exit(1);
	    }
	    curr = curr-> next;
	}
	
	// curr = head;
	// while (curr != NULL) {
	// 	printf("user name: %s\n", curr->userObj->user_name);
	// 	curr = curr->next;
	// }
	
}


void *checkConnected() {
	sleep(5);
	if (hasJoinedChat == 0) {
		printf("Sorry, no chat is active on %s:%d, try again later.\n Bye.\n",connectingIp,connectingPort);
		exit(0);
	}
	
}

void *checkClientTimeStamps() {
 // printf("starting client check\n");
  while(1) {
  //	printf("CHECKING USER STAMPS\n");
    struct userListObj* curr = head;
    while(curr != NULL) {
    	if(curr->userObj->is_leader == 1) {
    		curr = curr->next;
    		continue;
    	}
    	sem_wait(&last_alive_mutex);
    	double delay = (double) (clock() - (curr->userObj->lastAlive)) / 100;
    	sem_post(&last_alive_mutex);
    //	printf("LAST DIFFERENCE %s at %f\n", curr->userObj->user_name, delay);
     if (delay > 10) {
        printf("USER %s HAS DIED\n", curr->userObj->user_name);
        sendUserLeftMulticast(curr->userObj);
        removeUserFromList(curr);
     }
     curr = curr->next;
    }
    sleep(2);
  }
  
  pthread_exit(NULL);
}

void *pingServer() {
//	printf("starting server pings\n");
	while(1) {
	// create payload
	char *payload;
	asprintf(&payload, "ping\n%d", my_user->id);
	// send to server
	//making sure you don't ping yourself
	if (my_user->id != 1) {
		if(sendto(our_socket_fd, payload, strlen(payload), 0, (struct sockaddr *) &(head->userObj->user_addr), sizeof(head->userObj->user_addr) ) == -1) {
	            perror("Send error");
	            exit(1);
	    }
	}
	  sleep(3);
	}
  
   pthread_exit(NULL);
}




int main(int argc, char *argv[]) {
	
	int user_id = 1, yes = 1;
	char buffer[BUFFER_SIZE];
	char* own_ip;
	struct sockaddr_in server_socket, connectingSocket, client_socket;
	struct ifreq ifr;
	struct userListObj* List = malloc(sizeof(struct userListObj));
	char payload[1000];
	
	
	my_user = (struct user*)malloc(sizeof(struct user));
	sequenceNumber = 0;
	
	if(argc != 3 && argc != 4) {
		printf("Error! Incorrect number of arguments!");
		exit(1);
	}
	
	// set user attributes
	strcpy(my_user->user_name, argv[1]);
	
	sem_init(&last_alive_mutex, 0, 1);

	if (argc == 3) {
		myPort = atoi(argv[2]);
		my_user->is_leader = 1;
		pthread_create(&awake_thread, NULL, checkClientTimeStamps, NULL);
		
	} else {
		connectingIp = strtok(argv[2],":");
		connectingPort = atoi(strtok(NULL,"\0"));
		my_user->is_leader = 0;
		myPort = atoi(argv[3]);
		pthread_create(&ping_thread, NULL, pingServer, NULL);
		pthread_create(&connect_thread, NULL, checkConnected, NULL);
	}
	
	// create socket
	if((our_socket_fd = socket(AF_INET, SOCK_DGRAM, 0)) == -1) {
		printf("Socket Error");
		exit(1);
	}
	
	if(setsockopt(our_socket_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) == -1) {
		perror("Socket Error");
		exit(1);
	}
	
	ifr.ifr_addr.sa_family = AF_INET;
    strncpy(ifr.ifr_name, "eth0", IFNAMSIZ-1);
    ioctl(our_socket_fd, SIOCGIFADDR, &ifr);
    own_ip = inet_ntoa(((struct sockaddr_in *)&ifr.ifr_addr)->sin_addr);
    
    
    // my_user->user_addr = server_socket;
    my_user->user_addr.sin_addr = ((struct sockaddr_in *)&ifr.ifr_addr)->sin_addr;
    my_user->user_addr.sin_port = htons(myPort);
    my_user->user_addr.sin_family = AF_INET;
	bzero(&(my_user->user_addr.sin_zero), 8);
	size_of_socket = sizeof(struct sockaddr);
    my_user->id = user_id;
    
    List->userObj = my_user;
    List->next = NULL;
    head = List;
    
	/*printf("User name %s\n",my_user->user_name);
	printf("Address %s\n",inet_ntoa(my_user->user_addr.sin_addr));
	printf("Port %d\n", myPort);*/
	
	if (my_user->is_leader) {
		printf("%s started a new chat, listening on %s:%d\n", my_user->user_name,inet_ntoa(my_user->user_addr.sin_addr),myPort);
		printf("Succeeded, current users :\n");
		printf("%s %s:%d (Leader) \n", my_user->user_name, inet_ntoa(my_user->user_addr.sin_addr),myPort);
		printf("Waiting for others to join...\n");
	}

	// bind socket	//
	if(bind(our_socket_fd, (struct sockaddr *)&(my_user->user_addr), sizeof(struct sockaddr)) == -1) {
		perror("Bind Error");
		exit(1);
	}
	
	if (my_user->is_leader == 0) {
		connectingSocket.sin_family = AF_INET;
		connectingSocket.sin_port = htons(connectingPort);
		inet_aton(connectingIp, &(connectingSocket.sin_addr));
		bzero(&(connectingSocket.sin_zero), 8);
		
		strcpy(payload,getJoinPayload(my_user));
	
		if (sendto(our_socket_fd, payload, strlen(payload), 0, (struct sockaddr *) &connectingSocket, sizeof(connectingSocket))==-1) {
	      perror("error sending data in change password socket\n");
	      return -1;
	    }
	}
	
	fd_set readset;
	char str[BUFFER_SIZE], users[1000];
	int n;
	int select_retval;
	FD_ZERO(&readset);
    
    char* newUserName;
    char* newUserIP;
    int newUserPort;
    
    char* temp_user;
    //Starting the clock
    start = clock();	
	while(1) {
        
        memset(users, '\0', BUFFER_SIZE - 1);
        memset(buffer, '\0', BUFFER_SIZE - 1);
        FD_SET(0, &readset);
        FD_SET(our_socket_fd, &readset);
        select_retval = select(our_socket_fd + 1, &readset, NULL, NULL, NULL);
        
        if (select_retval < 0) {
            perror("Select");
            exit(1);
        }
       
       
        if (select_retval > 0) {
        
            if (FD_ISSET(our_socket_fd, &readset)) {
               
                if ((num_bytes = recvfrom(our_socket_fd, buffer, BUFFER_SIZE - 1, 0, (struct sockaddr *)&client_socket, &size_of_socket)) == -1) {
        			perror("Recv Error");
        			exit(1);
        		}
        		
        		/*printf("Packet received from %s\n", inet_ntoa(client_socket.sin_addr));
        		printf("Port received from %d\n", ntohs(client_socket.sin_port));
        		printf("Size of packet received: %d bytes\n", num_bytes);*/
        		
        	
        		buffer[num_bytes] = '\0';
        		//printf("The packet is: %s\n", buffer);
        		char buffCopy[BUFFER_SIZE];
        		strcpy(buffCopy,buffer);
        		
        		char* msg_type = strtok(buffer, "\n");
        		//If join notification, add to user list and broadcast join message
        		if((strcmp(msg_type, "join") == 0)) {
        			
        			// if self is leader
        			if(my_user->is_leader) {
        				struct userListObj* curr = head;
	        		    struct user* newUser = (struct user*)malloc(sizeof(struct user));
	        		    newUserName = strtok(NULL, "\n");
	        		    strcpy(newUser->user_name, newUserName);
	        		    newUserIP = strtok(NULL, "\n");
	        		    inet_aton(newUserIP, &(newUser->user_addr.sin_addr));
	        		    newUserPort = atoi(strtok(NULL, "\n"));
	        		    newUser->user_addr.sin_port = htons(newUserPort);
	        		    newUser->user_addr.sin_family = AF_INET;
	        		    newUser->is_leader = 0; 
	        		  	updateLastAlive(newUser);
	        		    newUser->id = ++user_id;
	        		    struct userListObj* newUserListObj = (struct userListObj*)malloc(sizeof(struct userListObj));
	        		    newUserListObj -> userObj = newUser;
	        		    newUserListObj -> next = NULL;
	        		    
	        		    
	        		    while(curr-> next != NULL) {
	        		        curr = curr->next;
	        		    }
	        		    
	        		    curr->next = newUserListObj;
	        		    
	        		    strcat(users, "list\n");
	        		    
	        		    curr = head;
	        		    
	        		    while(curr != NULL) {
	        		        temp_user = convertUsertoString(curr->userObj);
	        		        strcat(users,temp_user);
	        		        curr = curr->next;
	        		    }
	        		    
	        		    printf("%s", users);
	        		    
	        		    curr = head->next;
	        		    
	        		    while(curr != NULL) {				//sending the list when a user joins the chat
	        		    //	printf("Sending to: %s on %d\n", curr->userObj->user_name, curr->userObj->user_addr.sin_port);
	        		        if(sendto(our_socket_fd, users, strlen(users), 0, (struct sockaddr *) &(curr->userObj->user_addr), sizeof(curr->userObj->user_addr) ) == -1) {
	        		            perror("Send error");
	        		            exit(1);
	        		        }
	        		        curr = curr-> next;
	        		    }
        			} else { // else send leader info 
        				char leadInfo[1000];
        				strcat(leadInfo, "lead-info\n");
        				strcat(leadInfo, convertUsertoString(head->userObj));	
        				
        				if (sendto(our_socket_fd, leadInfo, strlen(leadInfo), 0, (struct sockaddr *) &client_socket, sizeof(client_socket))==-1) {
	      					perror("error in sending leader info\n");
	      					return -1;
	    				}
        				
        			}
        			
        			
        			
        		    
        		}
        		
        		if(strcmp(msg_type, "ping") == 0) {
        			int received_id = atoi(strtok(NULL, "\n"));
        			struct userListObj* curr = head;
        			while(curr != NULL) {
        				if(curr->userObj->id == received_id) {
        					updateLastAlive(curr->userObj);
        				}
        				curr = curr->next;
        			}				
        		}
        		
         		if(strcmp(msg_type, "lead-info") == 0) {
         			struct user* newUser = (struct user*)malloc(sizeof(struct user));
        		    newUserName = strtok(NULL, "\n");
        		    strcpy(newUser->user_name, newUserName);
        		    newUserIP = strtok(NULL, "\n");
        		    inet_aton(newUserIP, &(newUser->user_addr.sin_addr));
        		    newUserPort = atoi(strtok(NULL, "\n"));
        		    newUser->user_addr.sin_port = htons(newUserPort);
        		    newUser->user_addr.sin_family = AF_INET;
        		    newUser->is_leader = 1;
        		    
        		    // char *payload = getJoinPayload(my_user);
	
					if (sendto(our_socket_fd, payload, strlen(payload), 0, (struct sockaddr *) &(newUser->user_addr), sizeof(newUser->user_addr))==-1) {
				      perror("error sending leader info to leader\n");
				      return -1;
				    }
         		}
        		
        		if(strcmp(msg_type, "list") == 0) {
        			//printf("incoming list: %s\n", buffCopy);
        			
        			if (hasJoinedChat == 0) {
        				hasJoinedChat = 1;
        				printf("%s joining a new chat on %s:%d, listening on %s:%d\n", my_user->user_name, connectingIp,connectingPort,inet_ntoa(my_user->user_addr.sin_addr),myPort);
        			}
        			
        			struct userListObj* curr = NULL;
        			newUserName = strtok(NULL, "\n");
        		    while(newUserName != NULL) {
        		    		
        		        struct user* newUser = (struct user*)malloc(sizeof(struct user));
        		        strcpy(newUser->user_name, newUserName);
        		        newUserIP = strtok(NULL, "\n");
        		        inet_aton(newUserIP, &(newUser->user_addr.sin_addr));
            		    newUserPort = atoi(strtok(NULL, "\n"));
            		    newUser->user_addr.sin_port = htons(newUserPort);
            		    newUser->user_addr.sin_family = AF_INET;
            		    
            		    int newIsLeader;
            		    newIsLeader = atoi(strtok(NULL, "\n"));
            		    newUser->is_leader = newIsLeader;
            		    
            		    
            		    struct userListObj* userobj = (struct userListObj*)malloc(sizeof(struct userListObj));
            		    userobj -> userObj = newUser;
            		    if (curr != NULL) curr->next = userobj;
            		    curr = userobj;
            		    
            		   	if (newUser->is_leader) {
            		   		head = userobj;
            		   	} 	
            		   	
            		   	newUser->id = atoi(strtok(NULL,"\n"));
            		    
          
            		    if (strcmp(newUserName, my_user->user_name) == 0) {
        		    		my_user->id = newUser->id;
        		    	} 
        		    	
        		    	newUserName= strtok(NULL, "\n");
            		}
        		    
        		    /*curr = head;
        		    while(curr != NULL) {
        		    	printf("%s\n", curr->userObj ->user_name);
        		        curr = curr->next;
        		        
        		    }*/
        		}
        		
        		if(strcmp(msg_type, "message-request") == 0) {
        			char* received_message = strtok(NULL, "\n");			//message needs to go into the hold back queue
        			//printf("This is the buffer: %s\n", received_message);
        			struct userListObj* curr = head->next;
        			sequenceNumber++;
                	while(curr != NULL) {
                		char payload[BUFFER_SIZE] = "message\n";
                		strcat(payload, received_message);
                		sprintf(payload, "%s\n%d\n", payload, sequenceNumber);
	       		        if(sendto(our_socket_fd, payload, strlen(payload), 0, (struct sockaddr *) &(curr->userObj->user_addr), sizeof(curr->userObj->user_addr) ) == -1) {
        		            perror("Send error");
        		            exit(1);
        		        }
        		        curr = curr-> next;
        		    }
        		}
        	}
            
            if (FD_ISSET(0, &readset)) {
                memset(str, '\0', BUFFER_SIZE - 1);
                n = read(0,str,sizeof(str));
                str[n]='\0';
                struct userListObj* curr = head;
                
                //the leader is multicasting the message
                if (my_user->is_leader) { 
                	// create message payload with sequence number
                	curr = head->next;
                	sequenceNumber++;
                	while(curr != NULL) {
                		char *payload = getMessagePayload(str, my_user->user_name);
        		        if(sendto(our_socket_fd, payload, strlen(payload), 0, (struct sockaddr *) &(curr->userObj->user_addr), sizeof(curr->userObj->user_addr) ) == -1) {
        		            perror("Send error");
        		            exit(1);
        		        }
        		        curr = curr-> next;
        		    }
                } else {						//participant sends chat message to the leader
                	curr = head;			//head points to the leader
                	char *request = getMessageRequestPayload(str, my_user->user_name);
                	if(sendto(our_socket_fd, request, strlen(request), 0, (struct sockaddr *) &(curr->userObj->user_addr), sizeof(curr->userObj->user_addr) ) == -1) {
        		            perror("Send error");
        		            exit(1);
        		    }
                }
        		    
        		    
               // printf("%s was typed\n",str);
               // FD_CLR(0,&master);
                
            }
            
        }
		
			
	}
	close(our_socket_fd);
	return 0;		
}
