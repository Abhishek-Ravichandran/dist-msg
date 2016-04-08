/*
  Sample timers for elections / acks using threads and sleep
*/

#include  <unistd.h>
#include  <signal.h>
#include  <sys/time.h>
#include  <pthread.h>
#include  <stdio.h>

#define ELECTION_WAIT_TIME 5
#define ACK_WAIT_TIME 2

int election_cntr = 1;
int ack_cntr = 1;

pthread_t election_thread;
pthread_t ack_thread;

struct election_struct {
    int id;
};

struct ack_struct {
    int id;
};

void startAnElection(struct election_struct *es);
void *startElectionTimer(void *args);
void multiCastMessage(struct ack_struct *as);
void *startMulticastAckTimer(void *args);

int main(int argc, char *argv[]) {

  // start an election
  struct election_struct es;
  es.id = election_cntr++;
  startAnElection(&es);

  // start a multicast
  struct ack_struct as;
  as.id = ack_cntr++;
  multiCastMessage(&as);

  int i;
  for (i = 0; i < 10; i++) {
    printf("DOING WORK\n");
  }

  pthread_join(election_thread, NULL);
  pthread_join(ack_thread, NULL);
}


void startAnElection(struct election_struct *es) {
  // code to start election


  pthread_create(&election_thread, NULL, startElectionTimer, (void *)es);
}

void multiCastMessage(struct ack_struct *as) {
  // code to send multicast message

  pthread_create(&ack_thread, NULL, startMulticastAckTimer, (void *)as);
}

void *startElectionTimer(void *args) {
  struct election_struct *es = args;
  printf("STARTED ELECTION TIMER for election %d\n", es->id);
  sleep(ELECTION_WAIT_TIME);
  printf("FINISHED ELECTION TIMER for election %d\n", es->id);

  // code to check election responses

  pthread_exit(NULL);
}


void *startMulticastAckTimer(void *args) {
  struct ack_struct *as = args;
  printf("STARTED MULTICAST TIMER Acks for message  %d\n", as->id);
  sleep(ACK_WAIT_TIME);
  printf("FINISHED MULTICAST TIMER Acks for message %d\n", as->id);

  // code to check if ack received

  pthread_exit(NULL);
}







