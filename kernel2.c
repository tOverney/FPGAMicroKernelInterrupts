#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "Leds.h"
#include "system_m.h"
#include "interrupt.h"
#include "kernel2.h"

/************* Symbolic constants and macros ************/
#define MAX_PROC 10
#define MAX_MONITORS 10

#define INTERRUPT_COUNT 2
#define TIME_SLICE 20
#define STACK_SIZE 10000

#define DPRINTA(text, ...) printf("[%d] " text "\n", head(&readyList), __VA_ARGS__)
#define DPRINT(text) DPRINTA(text, 0)
#define ERRA(text, ...) fprintf(stderr, "[%d] Error: " text "\n", head(&readyList), __VA_ARGS__)
#define ERR(text) ERRA(text, 0)


/************* Data structures **************/
typedef struct {
    int next;
    Process p;
    int currentMonitor;/* points to the monitors array */
    int monitors[MAX_MONITORS + 1]; /* used for nested calls;
                                     * monitors[0] is always -1 */
    int time_ct;
} ProcessDescriptor;

typedef struct {
    int timesTaken;
    int takenBy;
    int entryList;
    int waitingList;
    int timedWaitList;
} MonitorDescriptor;

/********************** Global variables **********************/

/* Pointer to the head of the ready list */
static int readyList = -1;

/* Pointer to the head of sleeping processes */
static int sleepingList = -1;

/* List of process descriptors */
ProcessDescriptor processes[MAX_PROC];
static int nextProcessId = 0;

/* List of monitor descriptors */
MonitorDescriptor monitors[MAX_MONITORS];
static int nextMonitorId = 0;

/*************** Functions for process list manipulation **********/

/** Kernel processes **/
static Process idle;
static Process clk;

/* add element to the tail of the list */
static void addLast(int* list, int processId) {
    if(processId == -1) {
        return;
    }

    if (*list == -1) {
        *list = processId;
    }
    else {
        int temp = *list;
        while (processes[temp].next != -1){
            temp = processes[temp].next;
        }
        processes[temp].next = processId;
    }
    processes[processId].next = -1;
}

/* add element to the head of list */
static void addFirst(int* list, int processId){
    if(processId == -1) {
        return;
    }
    processes[processId].next = *list;
    *list = processId;
}

/* remove an element from the head of the list */
static int removeHead(int* list){
    if (*list == -1){
        return(-1);
    }
    else {
        int head = *list;
        int next = processes[*list].next;
        processes[*list].next = -1;
        *list = next;
        return head;
    }
}

/* returns the head of the list */
static int head(int* list){
    return *list;
}

/* checks if the list is empty */
static int isEmpty(int* list) {
    return *list < 0;
}

/***********************************************************
 ***********************************************************
                    Kernel functions
                    ************************************************************
                    *
                    * **********************************************************/

void createProcess (void (*f)(), int stackSize) {
    if (nextProcessId == MAX_PROC){
        ERR("Maximum number of processes reached!");
        exit(1);
    }
    unsigned int* stack = malloc(stackSize);
    if (stack==NULL) {
        ERR("Could not allocate stack. Exiting...");
        exit(1);
    }
    processes[nextProcessId].p = newProcess(f, stack, stackSize);
    processes[nextProcessId].next = -1;
    processes[nextProcessId].currentMonitor = 0;
    processes[nextProcessId].monitors[0] = -1;

    addLast(&readyList, nextProcessId);
    nextProcessId++;
}

static void checkAndTransfer() {
    if(isEmpty(&readyList)) {
        transfer(idle);
    }
    else {
        Process process = processes[head(&readyList)].p;
        transfer(process);
    }
}

static void idleFunc() {
    allowInterrupts();
    while(1);
}

static void clockHandler() {
    static int counter = TIME_SLICE;
    size_t i;

    DPRINT("Starting clock process");

    maskInterrupts();

    init_clock();

    while(1) {
        if(isEmpty(&readyList)) {
            iotransfer(idle, 0);
        }
        else {
            iotransfer(processes[head(&readyList)].p, 0);
        }
        counter--;
        if(counter == 0) {
            counter = TIME_SLICE;
            addLast(&readyList, removeHead(&readyList));
        }

        int pid = head(&sleepingList);
        if(pid != -1) {
            do {
                //Pour faire ça correctement il faudrait supporter la
                //suppression dans la liste au milieu parce que ce
                //n'est pas forcément le process en tête de liste qui
                //a fini de sleep !
                int npid = processes[pid].next;
                processes[pid].time_ct--;
                if(processes[pid].time_ct <= 0) {
                    addLast(&readyList, removeHead(&sleepingList));
                }
                pid = npid;
            } while(pid != -1);
        }

        for(i = 0 ; i < nextMonitorId ; i++) {
            int pid = head(&(monitors[i].timedWaitList));
            if(pid != -1) {
                do {
                    int npid = processes[pid].next ;
                    processes[pid].time_ct--;
                    if(processes[pid].time_ct <= 0) {
                        //Même commentaire qu'au dessus
                        if (monitors[i].takenBy != -1) {
                            addLast(&(monitors[i].entryList), removeHead(&(monitors[i].timedWaitList)));
                        }
                        else {
                            monitors[i].takenBy = pid;
                            monitors[i].timesTaken++;
                            addLast(&readyList, removeHead(&(monitors[i].timedWaitList)));
                        }
                    }
                    pid = npid;
                } while(pid != -1);
            }
        }
    }
}

void start() {
    LedInit();
    DPRINT("Starting kernel...");

    init_button();

    unsigned int* temp_sp = NULL;

    temp_sp = malloc(STACK_SIZE);

    if(temp_sp == NULL)  {
        ERR("Failed to allocate stack for idle process!");
        exit(1);
    }

    idle = newProcess(idleFunc, temp_sp, STACK_SIZE);

    temp_sp = malloc(STACK_SIZE);

    if(temp_sp == NULL) {
        ERR("Failed to allocate stack for clock process!");
        exit(1);
    }

    clk = newProcess(clockHandler, temp_sp, STACK_SIZE);

    transfer(clk);
}

void yield(){
    int pid = removeHead(&readyList);
    addLast(&readyList, pid);
    checkAndTransfer();
}

int createMonitor(){
    if (nextMonitorId == MAX_MONITORS){
        ERR("Maximum number of monitors reached!\n");
        exit(1);
    }
    monitors[nextMonitorId].timesTaken = 0;
    monitors[nextMonitorId].takenBy = -1;
    monitors[nextMonitorId].entryList = -1;
    monitors[nextMonitorId].waitingList = -1;
    monitors[nextMonitorId].timedWaitList = -1;
    return nextMonitorId++;
}

static int getCurrentMonitor(int pid) {
    return processes[pid].monitors[processes[pid].currentMonitor];
}

void enterMonitor(int monitorID) {
    maskInterrupts();

    int myID = head(&readyList);

    if (monitorID > nextMonitorId || monitorID < 0) {
        ERRA("Monitor %d does not exist.", nextMonitorId);
        exit(1);
    }

    if (processes[myID].currentMonitor >= MAX_MONITORS) {
        ERR("Too many nested calls.");
        exit(1);
    }

    if (monitors[monitorID].timesTaken > 0 && monitors[monitorID].takenBy != myID) {
        removeHead(&readyList);
        addLast(&(monitors[monitorID].entryList), myID);
        checkAndTransfer();

        /* I am woken up by exitMonitor -- check if the monitor state
         * is consistent */
        if ((monitors[monitorID].timesTaken != 1) || (monitors[monitorID].takenBy != myID)) {
            ERR("The kernel has performed an illegal operation. Please contact customer support.");
            exit(1);
        }
    }
    else {
        monitors[monitorID].timesTaken++;
        monitors[monitorID].takenBy = myID;
    }

    /* push the new call onto the call stack */
    processes[myID].monitors[++processes[myID].currentMonitor] = monitorID;
    allowInterrupts();
}

void exitMonitor() {
    maskInterrupts();

    int myID = head(&readyList);
    int myMonitor = getCurrentMonitor(myID);


    if (myMonitor < 0) {
        ERRA("Process %d called exitMonitor outside of a monitor.", myID);
        exit(1);
    }

    /* go backwards in the stack of called monitors */
    processes[myID].currentMonitor--;

    if (--monitors[myMonitor].timesTaken == 0) {
        /* see if someone is waiting, and if yes, let the next process
         * in */
        if (!isEmpty(&(monitors[myMonitor].entryList))) {
            int pid = removeHead(&(monitors[myMonitor].entryList));
            addLast(&readyList, pid);
            monitors[myMonitor].timesTaken = 1;
            monitors[myMonitor].takenBy = pid;
        } else {
            monitors[myMonitor].takenBy = -1;
        }
    }
    allowInterrupts();
}

void wait() {
    int myID = head(&readyList);
    int myMonitor = getCurrentMonitor(myID);
    int myTaken;

    maskInterrupts();

    if (myMonitor < 0) {
        ERRA("Process %d called wait outside of a monitor.", myID);
        exit(1);
    }

    removeHead(&readyList);
    addLast(&monitors[myMonitor].waitingList, myID);

    /* save timesTaken so we can restore it later */
    myTaken = monitors[myMonitor].timesTaken;

    /* let the next process in, if any */
    if (!isEmpty(&(monitors[myMonitor].entryList))) {
        int pid = removeHead(&(monitors[myMonitor].entryList));
        addLast(&readyList, pid);
        monitors[myMonitor].timesTaken = 1;
        monitors[myMonitor].takenBy = pid;
    } else {
        monitors[myMonitor].timesTaken = 0;
        monitors[myMonitor].takenBy = -1;
    }
    checkAndTransfer();

    /* I am woken up by exitMonitor -- check if the monitor state is
     * consistent */
    if ((monitors[myMonitor].timesTaken != 1) || (monitors[myMonitor].takenBy != myID)) {
        ERR("The kernel has performed an illegal operation. Please contact customer support.");
        exit(1);
    }

    /* we're back, restore timesTaken */
    monitors[myMonitor].timesTaken = myTaken;
    allowInterrupts();
}

void notify() {
    maskInterrupts();

    int myID = head(&readyList);
    int myMonitor = getCurrentMonitor(myID);

    if (myMonitor < 0) {
        ERRA("Process %d called notify outside of a monitor.", myID);
        exit(1);
    }

    if (!isEmpty(&(monitors[myMonitor].timedWaitList))) {
        int pid = removeHead(&monitors[myMonitor].timedWaitList);
        addLast(&monitors[myMonitor].entryList, pid);
    }
    else if (!isEmpty(&(monitors[myMonitor].waitingList))) {
        int pid = removeHead(&monitors[myMonitor].waitingList);
        addLast(&monitors[myMonitor].entryList, pid);
    }
    allowInterrupts();
}

void notifyAll() {
    maskInterrupts();

    int myID = head(&readyList);
    int myMonitor = getCurrentMonitor(myID);

    if (myMonitor < 0) {
        ERRA("Process %d called notify outside of a monitor.", myID);
        exit(1);
    }


    while(!isEmpty(&monitors[myMonitor].timedWaitList)) {
        int pid = removeHead(&monitors[myMonitor].timedWaitList);
        addLast(&monitors[myMonitor].entryList, pid);
    }

    while (!isEmpty(&(monitors[myMonitor].waitingList))) {
        int pid = removeHead(&monitors[myMonitor].waitingList);
        addLast(&monitors[myMonitor].entryList, pid);
    }

    allowInterrupts();
}

int timedWait(int time) {
    maskInterrupts();

    if (time == 0) { //If time is 0 just wait
        wait();
        return 1;
    }

    int myID = removeHead(&readyList);
    int myMonitor = getCurrentMonitor(myID);

    if(myMonitor < 0) {
        ERRA("Process %d called timedWait outside of a monitor.", myID);
        exit(1);
    }

    processes[myID].time_ct = time;

    if (!isEmpty(&(monitors[myMonitor].entryList))) {
        int pid = removeHead(&(monitors[myMonitor].entryList));
        addLast(&readyList, pid);
        monitors[myMonitor].timesTaken = 1;
        monitors[myMonitor].takenBy = pid;
    } else {
        monitors[myMonitor].timesTaken = 0;
        monitors[myMonitor].takenBy = -1;
    }

    addLast(&monitors[myMonitor].timedWaitList, myID);

    checkAndTransfer();

    allowInterrupts();

    return processes[head(&readyList)].time_ct > 0;
}

void sleep(int msec){
    maskInterrupts();

    int myID = removeHead(&readyList);

    processes[myID].time_ct = msec; //Update the wait time for this process

    addLast(&sleepingList, myID); // Put it in the sleeping list

    checkAndTransfer(); //Transfer control

    allowInterrupts();
}

void waitInterrupt(int per){
    if(per < 0 || per >= INTERRUPT_COUNT){
        ERRA("Waiting for invalid interrupt %d!\n", per);
        exit(1);
    }

    maskInterrupts();

    int pid = removeHead(&readyList);
    Process p;

    if(per != 0) {
        //On ne peut avoir que clk qui attend sur les interruptions du timer
        if(isEmpty(&readyList)) {
            p = idle;
        }
        else {
            p = processes[head(&readyList)].p;
        }
        iotransfer(p, per);
        addFirst(&readyList, pid);
    }
    allowInterrupts();
}
