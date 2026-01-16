// timer.c - A timer system
// 
// Copyright (c) 2024-2025 University of Illinois
// SPDX-License-identifier: NCSA
//


#ifdef TIMER_TRACE
#define TRACE
#endif

#ifdef TIMER_DEBUG
#define DEBUG
#endif

#include "timer.h"
#include <stddef.h>
#include "thread.h"
#include "riscv.h"
#include "misc.h"
#include "intr.h"
#include "conf.h"
#include "see.h" // for set_stcmp

// EXPORTED GLOBAL VARIABLE DEFINITIONS
// 

char timer_initialized = 0;

// INTERNVAL GLOBAL VARIABLE DEFINITIONS
//

static struct alarm * sleep_list;

//we need to initialize the preemption alarm for periodic context switching
static struct alarm preempt_alarm;
//we need to define the time quantum for preemptive multitasking (10ms)
#define PREEMPT_INTERVAL_MS 10
#define PREEMPT_INTERVAL_TICKS (PREEMPT_INTERVAL_MS * (TIMER_FREQ / 1000))

// INTERNAL FUNCTION DECLARATIONS
//

// EXPORTED FUNCTION DEFINITIONS
//

void timer_init(void) {
    set_stcmp(UINT64_MAX);
    //we need to initialize the preemption alarm for preemptive multitasking    
    alarm_init(&preempt_alarm, "preempt");
    preempt_alarm.twake = rdtime() + PREEMPT_INTERVAL_TICKS;
    preempt_alarm.next = NULL;
    //we need to schedule the first preemption interrupt
    sleep_list = &preempt_alarm;
    set_stcmp(preempt_alarm.twake);
    //we need to enable timer interrupts
    csrs_sie(RISCV_SIE_STIE);
    timer_initialized = 1;
}

// void alarm_init(struct alarm * al, const char * name)
// Inputs: struct alarm * al - Pointer to the alarm structure to initialize
//         const char * name - Optional name for the alarm (NULL for default)
// Outputs: None (void)
// Description: Initializes an alarm structure with condition variable and current time
// Side Effects: Initializes condition variable, sets next pointer to NULL, sets twake to current time
void alarm_init(struct alarm * al, const char * name) {
    // we initialize the condition variable with the given name
    if (name == NULL)
        name = "alarm";
    // we initialize the condition variable with the given name
    condition_init(&al->cond, name);
    // we initialize the next pointer to NULL
    al->next = NULL;
    // we initialize twake to current time in ticks
    // we know that rdtime() returns the current time in ticks
    al->twake = rdtime();
}

// void alarm_sleep(struct alarm * al, unsigned long long tcnt)
// Inputs: struct alarm * al - Pointer to the alarm structure
//         unsigned long long tcnt - Number of timer ticks to sleep
// Outputs: None (void)
// Description: Puts the current thread to sleep for tcnt timer ticks using the alarm system
// Side Effects: Modifies sleep_list, enables timer interrupts, blocks current thread until wake time
void alarm_sleep(struct alarm * al, unsigned long long tcnt) {
    unsigned long long now;
    struct alarm * prev;
    int pie;

    now = rdtime();

    // If the tcnt is so large it wraps around, set it to UINT64_MAX

    if (UINT64_MAX - al->twake < tcnt)
        al->twake = UINT64_MAX;
    else
        al->twake += tcnt;
    
    // If the wake-up time has already passed, return

    if (al->twake < now)
        return;
    // we disable interrupts to avoid race conditions while modifying sleep_list
    pie = csrrci_sstatus_SIE();
    // we insert the alarm into sleep_list in sorted order (by twake)
    if (sleep_list == NULL || al->twake < sleep_list->twake) {
        // we insert at the head of the list
        al->next = sleep_list;
        sleep_list = al;
        // we update mtimecmp since this is now the earliest alarm
        set_stcmp(al->twake);
    } 
    else {
        // we find the correct position in the sorted list
        prev = sleep_list;
        while (prev->next != NULL && prev->next->twake <= al->twake) {
            prev = prev->next;
        }
        // we insert after prev
        al->next = prev->next;
        prev->next = al;
    }
    // we enable timer interrupts
    csrs_sie(RISCV_SIE_STIE);
    // we put the thread to sleep, waiting for the alarm condition
    condition_wait(&al->cond);
    // we restore the previous interrupt enable state
    csrwi_sstatus_SIE(pie);
}

// Resets the alarm so that the next sleep increment is relative to the time
// alarm_reset is called.

void alarm_reset(struct alarm * al) {
    al->twake = rdtime();
}

void alarm_sleep_sec(struct alarm * al, unsigned int sec) {
    alarm_sleep(al, sec * TIMER_FREQ);
}

void alarm_sleep_ms(struct alarm * al, unsigned long ms) {
    alarm_sleep(al, ms * (TIMER_FREQ / 1000));
}

void alarm_sleep_us(struct alarm * al, unsigned long us) {
    alarm_sleep(al, us * (TIMER_FREQ / 1000 / 1000));
}

void sleep_sec(unsigned int sec) {
    sleep_ms(1000UL * sec);
}

void sleep_ms(unsigned long ms) {
    sleep_us(1000UL * ms);
}

void sleep_us(unsigned long us) {
    struct alarm al;

    alarm_init(&al, "sleep");
    alarm_sleep_us(&al, us);
}

// void handle_timer_interrupt(void)
// Inputs: None
// Outputs: None (void)
// Description: Timer interrupt service routine that processes expired alarms and updates timer state
// Side Effects: Removes expired alarms from sleep_list, wakes waiting threads, updates mtimecmp register
void handle_timer_interrupt(void) {
    struct alarm * head = sleep_list;
    struct alarm * next;
    struct alarm * prev;
    uint64_t now;
    int preempt_alarm_fired = 0;
    now = rdtime();
    trace("[%lu] %s()", now, __func__);
    debug("[%lu] mtcmp = %lu", now, rdtime());
    // we remove all expired alarms from sleep_list and wake their waiting threads
    while (head != NULL && head->twake <= now) {
        // we save pointer to next alarm
        next = head->next;
        //we need to check if this is the preemption alarm
        if (head == &preempt_alarm) {
            preempt_alarm_fired = 1;
            //we need to remove the preemption alarm from the list but don't broadcast (no one is waiting on it)
            sleep_list = next;
        } else {
            // we remove this alarm from the list
            sleep_list = next;
            // we wake up all threads waiting on this alarm
            condition_broadcast(&head->cond);
        }
        // we move to next alarm
        head = next;
    }
    //we need to reschedule the preemption alarm if it fired
    if (preempt_alarm_fired) {
        preempt_alarm.twake = now + PREEMPT_INTERVAL_TICKS;
        preempt_alarm.next = NULL;
        //we need to insert the preemption alarm back into sleep_list in sorted order
        if (sleep_list == NULL || preempt_alarm.twake < sleep_list->twake) {
            preempt_alarm.next = sleep_list;
            sleep_list = &preempt_alarm;
        } else {
            prev = sleep_list;
            while (prev->next != NULL && prev->next->twake <= preempt_alarm.twake) {
                prev = prev->next;
            }
            preempt_alarm.next = prev->next;
            prev->next = &preempt_alarm;
        }
    }
    // we update mtimecmp for the next alarm, or set to max if list is empty
    if (sleep_list != NULL) {
        // we set the timer interrupt threshold to the next alarm's wake time
        set_stcmp(sleep_list->twake);
    } else {
        //we need to set the timer interrupt threshold to the next alarm's wake time
        set_stcmp(UINT64_MAX);
        //we need to clear the timer interrupt enable bit
        csrc_sie(RISCV_SIE_STIE);
    }
}