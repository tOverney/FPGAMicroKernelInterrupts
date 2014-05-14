#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include "kernel.h"
#include "system_m.h"
#include "interrupt.h"

// Maximum number of processes.
#define MAXPROCESS 10
// Maximum number of monitors
#define MAX_MONITORS 10
// Maximum number of events
#define MAX_EVENTS 10

typedef struct {
    int next;
    Process p;
    int monitors[MAX_MONITORS]; // monitors stack
    int m_sp; // stack pointer of the monitors stack
} ProcessDescriptor;

typedef struct {
    int waitingList; // contains all process that have called wait() and are not yet notified
    int readyList; // contains all process that are waiting for the Monitor to be unlocked (they are ready to run)
    bool locked;
} MonitorDescriptor;

typedef struct {
    int waitingList; // contains all process that are waiting on the event to happen
    bool happened;
} EventDescriptor;


// Global variables

// Pointer to the head of list of ready processes
int readyList = -1;

// list of process descriptors
ProcessDescriptor processes[MAXPROCESS];
int nextProcessId = 0;

/***********************************************************
 ***********************************************************
            Utility functions for list manipulation
            ************************************************************
            * **********************************************************/

// add element to the tail of the list
void addLast(int* list, int processId) {
    if(processId == -1)
    {
        return;
    }
    if (*list == -1){
        // list is empty
        *list = processId;
    }
    else {
        int temp = *list;
        while (processes[temp].next != -1){
            temp = processes[temp].next;
        }
        processes[temp].next = processId;
        processes[processId].next = -1;
    }

}

// add element to the head of list
void addFirst(int* list, int processId){
    if(processId == -1)
    {
        return;
    }
    if (*list == -1){
        *list = processId;
    }
    else {
        processes[processId].next = *list;
        *list = processId;
    }
}

// remove element that is head of the list
int removeHead(int* list){
    if (*list == -1){
        return(-1); // we modified it so that it returns -1 if there is no element to remove
    }
    else {
        int head = *list;
        int next = processes[*list].next;
        processes[*list].next = -1;
        *list = next;
        return head;
    }
}

// returns head of the list
int head(int* list){
    if (*list == -1){
        return(-1);
    }
    else {
        return *list;
    }
}

/***********************************************************
 ***********************************************************
                    Kernel functions
                    ************************************************************
                    * **********************************************************/

void createProcess (void (*f)(), int stackSize) {
    if (nextProcessId == MAXPROCESS){
        printf("Error: Maximum number of processes reached!\n");
        exit(1);
    }

    Process process;
    unsigned int* stack = malloc(stackSize);
    process = newProcess(f, stack, stackSize);
    processes[nextProcessId].next = -1;
    processes[nextProcessId].p = process;
    processes[nextProcessId].m_sp = 0;

    // add process to the list of ready Processes
    addLast(&readyList, nextProcessId);
    nextProcessId++;

}


void yield(){
    int pId = removeHead(&readyList);
    addLast(&readyList, pId);
    Process process = processes[head(&readyList)].p;
    transfer(process);
}

void start(){

    printf("Starting kernel...\n");
    if (readyList == -1){
        printf("Error: No process in the ready list!\n");
        exit(1);
    }
    Process process = processes[head(&readyList)].p;
    transfer(process);
}


/**
 * Monitor related kernel functions
 **/

MonitorDescriptor monitors[MAX_MONITORS];
int nextMonitorID = 0;

/**
 * Returns true if the pid has entered into the monitor with id mid at
 * least once
 **/
bool hasMonitor(int pid, int mid)
{
    int i;
    for(i = 0 ; i < MAX_MONITORS ; i++)
    {
        if(processes[pid].monitors[i] == mid)
        {
            return true;
        }
    }
    return false;
}

/**
 * Returns the element on top of the monitor stack of a process without deleting it from the stack.
 **/
int peekMonitor(ProcessDescriptor *p)
{
    if(p->m_sp == 0)
    {
        return -1;
    }
    return p->monitors[p->m_sp - 1];
}

/**
 * Returns and delete the element on top of a process' monitor's stack.
 **/
int popMonitor(ProcessDescriptor *p)
{
    if(p->m_sp == -1) //Nothing to pop
    {
        return -1;
    }
    return p->monitors[--(p->m_sp)];
}

/**
 * Add an element on at the top of a process' monitor's stack.
 **/
void pushMonitor(ProcessDescriptor *p, int monitorId)
{
    if(p->m_sp == MAX_MONITORS)
    {
        fprintf(stderr, "Process has too many imbricated monitor calls\n");
        exit(EXIT_FAILURE);
    }
    p->monitors[p->m_sp++] = monitorId;
}

/**
 * Returns true if process p is in monitor monitorId
 **/
bool isInMonitor(ProcessDescriptor *p, int monitorId)
{
    int i;
    for(i = p->m_sp ; i >= 0 ; i--)
    {
        if(p->monitors[i] == monitorId)
        {
            return true;
        }
    }
    return false;
}

/**
 * initialize a new monitor if there isn't aready too many of them.
 **/
int createMonitor()
{
    if(nextMonitorID >= MAX_MONITORS)
    {
    	fprintf(stderr, "There is already too many Monitors!\n");
    	exit(1);
    }

    monitors[nextMonitorID].waitingList = -1; // waiting list is yet empty
    monitors[nextMonitorID].readyList = -1; // ready list is yet empty
    monitors[nextMonitorID].locked = false; // there is no process in this event yet

    return nextMonitorID++;
}

/**
 * Tries to enter the given monitor depending if it's locked or not.
 **/
void enterMonitor(int monitorId)
{
    ProcessDescriptor *proc = &(processes[head(&readyList)]);

    bool alreadyLocked;

    if(monitorId < 0 || monitorId >= nextMonitorID)
    {
    	fprintf(stderr, "Invalid monitorId!\n");
        return;
    }

    alreadyLocked = isInMonitor(proc, monitorId);

    pushMonitor(proc, monitorId);

    if(!alreadyLocked) //If we don't already have the lock of this monitor
    {
        if(monitors[monitorId].locked) //And if it's locked somewhere else
        {

            addLast(&(monitors[monitorId].readyList), removeHead(&readyList)); // we put the current process in the readyList
            transfer(processes[head(&readyList)].p); // we transfer control to another process.
        }
        else // else if it's unlocked, we take it for this process and lock it.
        {
            monitors[monitorId].locked = true;
        }
    }
}

/**
 * Wait to be notified by a given monitor
 **/
void wait()
{
    ProcessDescriptor *proc = &(processes[head(&readyList)]);
    int monitorId = peekMonitor(proc);

    if(monitorId == -1)
    {
    	fprintf(stderr, "Error: Process is in no monitors\n");
        return;
    }

    // if there is no other process ready to run in this monitor, we unlock the monitor
    if(head(&(monitors[monitorId].readyList)) == -1)
    {
        monitors[monitorId].locked = false;
    }

    // we add our process to the waiting list of the given monitor
    addLast(&(monitors[monitorId].waitingList), removeHead(&readyList));

    // we transfer control to another process (and add head of this monitor readylist if there is one)
    addLast(&readyList, removeHead(&(monitors[monitorId].readyList)));
    transfer(processes[head(&readyList)].p);
}

/**
 * Notify a process from the waitingList of the current monitor
 **/
void notify()
{
    ProcessDescriptor *proc = &(processes[head(&readyList)]);
    int monitorId = peekMonitor(proc);

    if(monitorId == -1)
    {
    	fprintf(stderr, "Error: Process is in no monitors\n");
        return;
    }

    // we transfer the head of its waitingList to its readyList.
    addLast(&(monitors[monitorId].readyList),
            removeHead(&(monitors[monitorId].waitingList)));
}

/**
 * Notify all processes waiting on the current monitor
 **/
void notifyAll()
{
    ProcessDescriptor *proc = &(processes[head(&readyList)]);
    int monitorId = peekMonitor(proc);

    if(monitorId == -1)
    {
    	fprintf(stderr, "Error: Process is in no monitors\n");
        return;
    }

    // we put every process of the waitingList in the readyList of the current monitor.
    while(head(&(monitors[monitorId].waitingList)) != -1)
    {
        addLast(&(monitors[monitorId].readyList),
                removeHead(&(monitors[monitorId].waitingList)));
    }
}

/**
 * Leaves the current monitor section
 **/
void exitMonitor()
{
    ProcessDescriptor *proc = &(processes[head(&readyList)]);
    int monitorId = popMonitor(proc);

    if(monitorId == -1)
    {
    	fprintf(stderr, "Error: Process is in no monitors\n");
        return;
    }

    // If there is no more ready process for the current monitor
    if(head(&(monitors[monitorId].readyList)) == -1)
    {
        if(!isInMonitor(proc, monitorId)) // And this monitor is no longer in the stack of the current process
        {
            monitors[monitorId].locked = false; // We unlock.
        }
    }
    // If there is still ready process, we put the head of the monitor's readyList in the kernel readyList and we do not unlock the monitor
    else
    {
        addLast(&readyList,
                removeHead(&(monitors[monitorId].readyList)));
    }
}

/**
 * Event related kernel functions
 **/

// list of event descriptors
EventDescriptor events[MAX_EVENTS];
int nextEventID = 0;

/**
 * We create a new event if we haven't reached the max amount of them
 **/
int createEvent()
{
    // We check if we haven't reached the max amount of events yet
    if(nextEventID == MAX_EVENTS)
    {
        printf("Error: No more events available\n");
        exit(1);
    }
    events[nextEventID].waitingList = -1; // no process are waiting yet
    events[nextEventID].happened = false; // event hasn't happened yet

    return nextEventID++; // return the ID of the newly created event
}

/**
 * We put the current process in the waitingList of the current event
 **/
void attendre(int eventID)
{
    if(eventID < 0 || eventID >= nextEventID)
    {
        printf("Error: using invalid event!!\n");
        return;
    }

    if(!events[eventID].happened)
    {
        addLast(&(events[eventID].waitingList), removeHead(&readyList));
        transfer(processes[head(&readyList)].p);
    }
}

/**
 * Take all processes of the waitingList out of it and put them in the kernel readyList
 **/
void declencher(int eventID)
{
    if(eventID < 0 || eventID >= nextEventID)
    {
        printf("Error: using invalid event!!\n");
        return;
    }

    events[eventID].happened = true; // YES IT HAS HAPPENED! Don't forget to state it or it will deadlock

    while(head(&(events[eventID].waitingList)) != -1)
    {
        addLast(&readyList, removeHead(&(events[eventID].waitingList)));
    }
}

/**
 * We turn the given event back to untriggered.
 **/
void reinitialiser(int eventID)
{
    if(eventID < 0 || eventID >= nextEventID)
    {
        printf("Error: using invalid event!!\n");
        return;
    }

    events[eventID].happened = false;
}
