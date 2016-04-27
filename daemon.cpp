#include <iostream>
#include <vector>

#include <fcntl.h> //Some C libraries are required
#include <kvm.h>
#include <libutil.h>
#include <signal.h>
#include <stdlib.h>
#include <pthread.h>
#include <sys/event.h>
#include <sys/sysctl.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/queue.h>
#include <vm/vm_param.h>

using namespace std; 

#define SIGTEST 44
#define SIGSEVERE 45
#define SIGMIN 46
#define SIGPAGESNEEDED 47 

static SLIST_HEAD(slisthead, managed_application) head = SLIST_HEAD_INITIALIZER(head);
static struct slisthead *headp;
struct kevent change[1];
struct kevent event[1];



/* Used to hold information about applications which have asked to be informed about low memory conditions */
struct managed_application
{
	int 		pid;
	int 		condition;
	SLIST_ENTRY(managed_application)	next_application;
};

/* This function handles the registration and deregistration of different applic   ations. It is called as part of the signal handling thread */
void monitor_application(int signal_number, siginfo_t *info, void *unused){
	
	struct managed_application *current_application = (managed_application*)malloc(sizeof(struct managed_application));
	struct managed_application *np_temp = (managed_application*)malloc(sizeof(struct managed_application));
	struct managed_application *application = (managed_application*)malloc(sizeof(struct managed_application));
	
	if (SLIST_FIRST(&head) != NULL){
		SLIST_FOREACH_SAFE(current_application, &head, next_application, np_temp){
			if (current_application->pid == info->si_pid){
				SLIST_REMOVE(&head, current_application, managed_application, next_application);
				free(current_application);
				printf("DEREGISTERED\n");
				return;
			}
			if (kill(current_application->pid,0)==-1){
				SLIST_REMOVE(&head, current_application, managed_application, next_application);
				free(current_application);
				printf("TIMED OUT\n");
			}
		}
	}
	application->pid = info->si_pid;	
	application->condition = signal_number;
	SLIST_INSERT_HEAD(&head, application, next_application);
	printf("REGISTERED FOR %d\n", application->condition);

}


/* Sleeps for a random amount of milliseconds between min and max */
void random_millisecond_sleep(int min, int max)
{
	struct timespec sleepFor;
	int randomMilliseconds = ((rand() % max)+min) * 1000 * 1000;
	sleepFor.tv_sec = 0;
	sleepFor.tv_nsec = randomMilliseconds;
	nanosleep(&sleepFor, 0);
}


/* Sends SIGSTOP to all applications which are currently in our list of monitored applications */
void suspend_applications()
{
	struct managed_application *current_application = (managed_application*)malloc(sizeof(struct managed_application));
	SLIST_FOREACH(current_application, &head, next_application){
		int pid = current_application->pid;
		kill(pid, SIGSTOP);
	}
}


/* Secnds SIGCONT to all applications currently in our list of monitored applications. Between each SIGCONT there is a random sleep between 0 and 1 seconds in order to avoid thundering herd issues. */
void resume_applications()
{
	struct managed_application *current_application = (managed_application*)malloc(sizeof(struct managed_application));
	SLIST_FOREACH(current_application, &head, next_application){
		int pid = current_application->pid;
		kill(pid, SIGCONT);
		random_millisecond_sleep(0,1000);
	}
}


/* Block waiting for registration or deregistration signals. This runs on a thread seperate from the main thread as the main thread will be blocking on the char device */
void *monitor_signals(void* unusedParam)
{	
	struct sigaction sig;
	sig.sa_sigaction = monitor_application;
	sig.sa_flags = SA_SIGINFO;
	sigaction(SIGSEVERE, &sig, NULL);
	sigaction(SIGMIN, &sig, NULL);
	sigaction(SIGPAGESNEEDED, &sig, NULL);
	for (;;)
		pause();
}

/* Loops and blocks on the kqueue handled by our char device. On a low memory event we notify all registered applications interested in that event */

int main(int argc, char ** argv)
{
	if (argc != 1){
		printf("Args: %d\n", argc);
		return -1;
	}
	
	SLIST_INIT(&head);
	struct managed_application *current_application = (managed_application*)malloc(sizeof(struct managed_application));
	
	pthread_t signalThread;		
	pthread_create(&signalThread, 0, monitor_signals, (void*)0);	
	int fd=0;
	fd = open("/dev/lowmem", O_RDWR | O_NONBLOCK);
	int kq=kqueue();
	EV_SET(&change[0],fd,EVFILT_READ, EV_ADD,0,0,0);
	for(;;){
		printf("BLOCKING\n");		
		int n=kevent(kq,change,1,event,1,NULL);
		printf("UNBLOCKING\n");
		int flags = 0;
		flags = event[0].data;
		printf("DATA: %d\n", flags);
		SLIST_FOREACH(current_application, &head, next_application){
			int pid = current_application->pid;
			printf("PID %d IS REGISTERED\n", pid);
			if(flags & 0b1000 && current_application->condition == SIGSEVERE){
				kill(pid,SIGTEST);
				printf("KILLED SEVERE: %d\n", pid);					}
			if(flags & 0b10 && current_application->condition == SIGMIN){
				kill(pid,SIGTEST);
				printf("KILLED MIN: %d\n", pid);
			}
			if(flags & 0b100 && current_application->condition == SIGPAGESNEEDED){
				kill(pid,SIGTEST);
				printf("KILLED PAGES NEEDED: %d\n", pid);
			}
			random_millisecond_sleep(0,1000);
		}
		if (flags & 0b1000 || flags & 0b10000){
			suspend_applications();
			resume_applications();	
		}
		struct timespec sleepFor;
		sleepFor.tv_sec = 2;
		sleepFor.tv_nsec = 0;
		nanosleep(&sleepFor, 0);

	}
	return 0;
}
