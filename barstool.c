#include <linux/init.h>
#include <linux/module.h>
#include <linux/list.h>
#include <linux/string.h>
#include <linux/proc_fs.h> //proc file system
#include <linux/kthread.h> //waiter thread
#include <linux/delay.h>   //sleep function for thread
#include <linux/uaccess.h> //copy_from_user
#include <linux/slab.h>    //kmalloc
#include <linux/time.h>    //time functions
#include <linux/seq_file.h>
#include <linux/mutex.h>
#include <linux/ktime.h>

MODULE_LICENSE("Dual BSD/GPL");

#define PROC_NAME "majorsbar"
#define ENTRY_SIZE 1000
static char bar_status_types[]="FOJSP";
struct mutex WaiterWorks_mutex;
struct mutex procTimer;
static struct timespec64 start_time; // struct for proc entry 

enum state
{
    OFFLINE,
    IDLE,
    LOADING,
    CLEANING,
    MOVING
};

int timerTracker = 0;
typedef struct timer
{
    struct timespec64 time_entered;
    int tableNumb;
    int seatNumb;
    int groupSize;
    int entryType;
    bool done;
    bool containsData;
    int time_id;
    struct list_head time;  //list of times
} timer;
//struct timer times[5];

// static struct semaphore in_queue = 1; // semaphore for queue
typedef struct group
{
    // time_t time_entered;
    int group_id;             // what group are they with?
    int type;                 // Freshman, Sophomore, Junior, Senior, Graduate
    int size;                 // how many people in group?
    struct list_head in_line; // node of customer in queue
} group;
typedef struct seat
{
    struct timer the_time_entered;
    bool status;          // occupied or empty?
    bool dirty;           // is the seat dirty?
    int id;               // id of seat
    int type;             // type of customer
    struct group student; // student in seat
} seat;
typedef struct table
{
    struct seat seats[8];    // 8 seats per table
    bool dirty;              // is the table dirty?
    struct list_head groups; // list of group_ids at table

    struct list_head times;  // list of times at table
    int group_ids[8]; // number of groups at table
    int gid_index;           // index of group_ids
    int num_times;          // number of times at table
} table;
static struct bar
{
    // HOW TO MAKE A QUEUE THAT DEALS WITH N NUMBER OF STUDENTS?
    struct list_head queue; // list of students in queue
    struct list_head waiting; // list of students waiting
    int queue_size;         // size of queue
    int num_groups;         // number of groups in queue
    int num_in_bar;         // number of people in bar


    int total_groups;
    struct table tables[4]; // 4 tables in the bar
    // count of each type at the bar
    int customer_num[5];  // 0-4 are types of customer respectively.
    int num_ids;          // number of group ids
    enum state bar_state; // state of the bar


    int customer_serviced;// this if for the proc file it will tell the # of cutomers serviced

} bar;
static struct waiter_params // waiter thread
{
    int current_table;           // current table the waiter is at
    int current_seat;            // current seat the waiter is at
    struct table serving;        // table the waiter is serving
    struct task_struct *kthread; // waiter thread
    struct mutex Qmutex;         // mutex for queue
    // struct waiter *waiter; // waiter thread
};
static struct waiter_params waiter;
static struct proc_dir_entry *proc_entry; // proc entry
// enum state for proc???
const char* getState(enum state s) {
    switch (s) {
        case IDLE:
            return "IDLE";
        case OFFLINE:
            return "OFFLINE";
        case MOVING: 
            return "MOVING";
        case LOADING:
            return "LOADING";
        case CLEANING:
            return "CLEANING";
        default:
            return "UNKNOWN";
    }
}
// proc show being filled out
static int proc_show(struct seq_file *m, void *v)
{

    //delete comments once format string is inserted into seq_printf
    struct timespec64 current_time, elapsed_time;

    seq_printf(m, "Waiter State: %s\n", getState(bar.bar_state)); // print waiter state 
    seq_printf(m, "Current Table: %d\n", waiter.current_table); // print current table

    ktime_get_real_ts64(&current_time);

    if (current_time.tv_sec >= start_time.tv_sec)
    {
        elapsed_time.tv_sec = current_time.tv_sec - start_time.tv_sec;
        elapsed_time.tv_nsec = current_time.tv_nsec - start_time.tv_nsec;
        if (elapsed_time.tv_nsec < 0) {
            elapsed_time.tv_nsec += 1000000000;
            elapsed_time.tv_sec--;
        }
        while(elapsed_time.tv_nsec >= 10)
        {
        elapsed_time.tv_nsec = elapsed_time.tv_nsec / 10;
        }

        if (elapsed_time.tv_nsec >= 5)
        {
            elapsed_time.tv_sec++;
        }
        else
        {
            elapsed_time.tv_nsec = 0;
        }

        seq_printf(m, "Elapsed Time: %lld  seconds\n",
                   (long long)elapsed_time.tv_sec);
    }

    // seq_printf(m, "Elapsed Time:  seconds\n" ); // print elapsed time
    seq_printf(m, "Current Occupancy:%d \n",bar.num_in_bar ); // print current occupancy

    seq_printf(m, "Bar Status: "); // print bar status );

    int i, non_zero_groups, counter = 0;
    for (i = 0; i < 5; i++) //count how many types of groups at the bar have at least one person 
    {
        if (bar.customer_num[i] != 0) //0th: Freshman, 1st: Sophomore, 
            non_zero_groups++;        //2nd: Junior, 3rd: Senior, 4th: Graduate
        else
            continue;
    }

    seq_printf(m, "NON-ZERO GROUPS: %d", non_zero_groups); // print bar status );


    for (i = 0; i < 5; i++) //print number of each type of group at the bar
    {

        if (bar.customer_num[i] == 0)
            continue;
        else
        {
            counter++;
            if (counter != non_zero_groups)
            {
                switch (i)
                {
                case 0:                       
                    seq_printf(m, "%d F, ", i); 
                case 1:
                    seq_printf(m, "%d O, ", i);
                case 2:
                    seq_printf(m, "%d J, ", i);
                case 3:
                    seq_printf(m, "%d S, ", i);
                case 4:
                    seq_printf(m, "%d P, ", i);
                }
            }
            else 
            {

                switch (i)
                {
                case 0:                      
                    seq_printf(m, "%d F\n", i); 
                case 1:
                    seq_printf(m, "%d O\n", i); 
                case 2:
                    seq_printf(m, "%d J\n", i);
                case 3:
                    seq_printf(m, "%d S\n", i);
                case 4:
                    seq_printf(m, "%d P\n", i);
                }
                break;
            }
        }
    }

    seq_printf(m, "Number of Customers Waiting:%d \n",bar.queue_size); //print number of customers waiting
    seq_printf(m, "Number of Groups Waiting: %d \n", bar.num_groups); //print number of groups waiting
    seq_printf(m, "Content of Queue: \n" ); //print content of queue
   /* for (i = 0; i < bar.queue_size; i++) //replace 5 with queue.size
    {
         for (int j = 0; j < queue[i].size; j++)// I'm unsure what varuble should be here.
         {
             seq_printf(m, "%d ", queue[i].type); // prints char type of each member in the group
         }
         seq_printf(m, "{group id: %d} ", groups[i]); // prints id
    }*/
    seq_printf(m, "Number of customers serviced:%d \n\n", bar.customer_serviced); // prints number of customers serviced

   /* for (i = 0; i < bar.queue_size; i++) //replace 5 with table.size
    {
        // // determine where the waiter is so that the correct [*] is printed
        if (waiter.current_table == i)///will this work
             seq_printf(m, "[*]", ); // prints char type of each member in the group
         else
             seq_printf(m, "[ ]", ); // prints char type of each member in the group
         seq_printf(m, " Table %d: ", i);
         for (int j = 0; j < table[i].seat[j]; j++)
         {
            // needs to deal with dirty and clean?
            seq_printf(m, "%d ", bar.tables[i].seats[j].type); // prints char type of each member in the group
         }
    }*/

    return 0;
}



static int proc_open(struct inode *inode, struct file *file)
{
    return single_open(file, proc_show, NULL);
}

static struct proc_ops timer_fops = {
    .proc_open = proc_open,
    .proc_read = seq_read, // this needs to match the name of the function that prints to the proc file
    .proc_lseek = seq_lseek,
    .proc_release = single_release,

};


bool isEmpty(void) // checks if table is empty
{
    // mutex_lock(&waiter.Qmutex); // LOCK
    int i;
    for (i = 0; i < 8; i++)
    {
        if (bar.tables[waiter.current_table].seats[i].status == true)
        {
            // mutex_unlock(&waiter.Qmutex); // LOCK
            return false;
        }
    }
    // mutex_unlock(&waiter.Qmutex); // LOCK
    return true;
}

bool isDirty(void) // checks if table is dirty
{
    // mutex_lock(&waiter.Qmutex); // LOCK
    int i;
    for (i = 0; i < 8; i++)
    {
        if (bar.tables[waiter.current_table].seats[i].dirty == true)
        {
            // mutex_unlock(&waiter.Qmutex); // LOCK
            return true;
        }
    }
    // mutex_unlock(&waiter.Qmutex); // LOCK
    return false;
}
int waiter_cleans(void) // waiter cleans table
{
    // mutex_lock(&waiter.Qmutex); // LOCK
    //bar.bar_state = CLEANING; // bar state is set to cleaning

    // mutex_unlock(&WaiterWorks_mutex); // UNLOCK
    // ssleep(10); // takes 10 second to clean
    // mutex_lock(&WaiterWorks_mutex); // LOCK
    int i;
    for (i = 0; i < 8; i++)
    {
        if (bar.tables[waiter.current_table].seats[i].dirty == true) // seat is dirty
            bar.tables[waiter.current_table].seats[i].dirty = false; // seat is now clean
    }
    // mutex_unlock(&waiter.Qmutex); // LOCK
    return 0;
}
static int where[2];         // where to seat customers. 0 is table number, 1 is index to start seating
int hasSpace(int group_size) // checks if tables have space for group
{                            // retrurns table number table number and index to start seating
    // need to count consecutive seats not total seats
    //    mutex_lock(&waiter.Qmutex);
    printk(KERN_EMERG "group size in hasSpace is: %d", group_size);
    int i, j;
    int count = 0;          // free consecutive seats at current table
    bool free = false;      // helps determine consecutive seats
    bool set = false;       // has free already been set?
    int freeIndex;          // index of first free seat
    for (i = 0; i < 4; i++) // go through tables
    {

        for (j = 0; j < 8; j++)
        {
            // printk(KERN_EMERG "TABLE ITER NUMB:  %d   SEAT ITER NUMB: %d", i, j);
            // CHECK IF EMPTY AND NOT DIRTY
            if ((bar.tables[i].seats[j].status == false) && (bar.tables[i].seats[j].dirty == false))
            {
                if (set == false) // if free has not been set
                {
                    free = true;   // seat is free
                    set = true;    // free has been set
                    freeIndex = j; // set freeIndex to j
                }
                if (free == true)        // if seat is free
                    count++;             // increment count
                if (count == group_size) // if there is enough space for the group
                {
                    where[0] = i;         // set table number
                    where[1] = freeIndex; // set index to start seating
                    return 0;             // where is set
                }
            }
            else // seat is occupied
            {
                free = false; // seat is not free
                set = false;  // free has been set to false
                count = 0;    // reset count
            }
        }
        // changed tables so reset variables
        free = false;
        set = false;
        count = 0;
    }
    // mutex_unlock(&waiter.Qmutex);
    return -1; // did not set
}

bool isTimeUp(int entry_type, struct timespec64 time_entered) // checks if time is up
{
    // create current time variable

    struct timespec64 current_time;
    ktime_get_real_ts64(&current_time);

    if (entry_type < 0)
    {

        printk(KERN_EMERG "TYPE -1 INSTANCE... type is: %d", entry_type);
        return false;
    }

    if (entry_type == 0) // check if time is up for group
    {
        if (current_time.tv_sec - time_entered.tv_sec >= 5) // time is up
        {

            printk(KERN_EMERG "Student of type %d time is DONE. They took %llu seconds ", entry_type, (unsigned long long)current_time.tv_sec - time_entered.tv_sec);

            return true;
        }
        else // time is not up
        {
            return false;
        }
    }
    else if (entry_type == 1) // check if time is up for table
    {
        if (current_time.tv_sec - time_entered.tv_sec >= 10) // time is up
        {

            printk(KERN_EMERG "Student of type %d time is DONE. They took %llu seconds ", entry_type, (unsigned long long)current_time.tv_sec - time_entered.tv_sec);

            return true;
        }
        else // time is not up
        {
            return false;
        }
    }

    else if (entry_type == 2) // check if time is up for bar
    {
        if (current_time.tv_sec - time_entered.tv_sec >= 15) // time is up
        {
            printk(KERN_EMERG "Student of type %d time is DONE. They took %llu seconds ", entry_type, (unsigned long long)current_time.tv_sec - time_entered.tv_sec);

            return true;
        }
        else // time is not up
        {
            return false;
        }
    }
    else if (entry_type == 3)
    {

        if (current_time.tv_sec - time_entered.tv_sec >= 20) // time is up
        {

            printk(KERN_EMERG "Student of type %d time is DONE. They took %llu seconds ", entry_type, (unsigned long long)current_time.tv_sec - time_entered.tv_sec);

            return true;
        }
        else // time is not up
        {
            return false;
        }
    }
    else if (entry_type == 4)
    {

        if (current_time.tv_sec - time_entered.tv_sec >= 25) // time is up
        {

            printk(KERN_EMERG "Student of type %d time is DONE. They took %llu seconds ", entry_type, (unsigned long long)current_time.tv_sec - time_entered.tv_sec);

            return true;
        }
        else // time is not up
        {
            return false;
        }
    }
    return false;

}

int waiter_works(void *data) // main thread function
{
    mutex_init(&WaiterWorks_mutex);

    // struct waiter_params *waiter = data;
    int x;
    int i = 0;
    struct group *entry;

    struct timer *entryTime; 
    struct list_head *dummy;
    struct list_head *temp;
    //struct list_head waiting;
    //INIT_LIST_HEAD(&waiting);

    while (!kthread_should_stop())
    {

        //ADD CONDITION TO DO NOTHINBG IF THERE IS NO ONE AT THE BAR
        // if (bar.num_groups == 0 && bar.num_in_bar == 0)
        // {
        //     //DO NOTHING
        // }
        if (bar.bar_state != OFFLINE && bar.num_groups > 0)
        {
            // printk(KERN_EMERG "BEFORE LOCK");
            mutex_lock(&WaiterWorks_mutex); // LOCK
            // printk(KERN_EMERG "LOCK SET");
            //  printk(KERN_EMERG "lx------------------------------------------------lx");
            //             printk(KERN_EMERG "bar.num_groups before: %d", bar.num_groups);

            list_for_each_safe(temp, dummy, &bar.queue) // moving to waiting area
            {
                // printk(KERN_EMERG "first loop\n");
                entry = list_entry(temp, struct group, in_line); // grab a group from the queue
                // printk(KERN_EMERG "grabbed entry size is: %d\n", entry->size);
                x = hasSpace(entry->size); // check for space
                // printk(KERN_EMERG "x in first loop: %d\n", x);
                if (x == 0)
                {
                    list_move_tail(temp, &bar.waiting); // remove from queue, place in waiting area
                    // printk(KERN_EMERG "group moved to waiting area\n");
                    break; // break out of loop to avoid moving more than one group
                }
                break; // didnt find seat so break and try again later
            }

            list_for_each_safe(temp, dummy, &bar.waiting) // setting bar values based on waiting area
            {
                // printk(KERN_EMERG "second loop\n");
                bar.bar_state = LOADING;
                entry = list_entry(temp, struct group, in_line); // grab a group from the queue
                // SEAT GROUP
                int i;
                // ssleep(1); // takes 1 second to seat group
                // printk(KERN_EMERG "seating from waiting area\n");

                bar.num_in_bar += entry->size; // increment number of people in bar

                for (i = 0; i < entry->size; i++) // seat group
                {
                    mutex_unlock(&WaiterWorks_mutex); // UNLOCK
                    ssleep(1); // takes 1 second to seat group
                    mutex_lock(&WaiterWorks_mutex); // LOCK
                    // printk(KERN_EMERG "iteration %d\n", i);
                    bar.customer_num[entry->type]++;
                    printk(KERN_EMERG "group type entering customer num is : %d", entry->type);
                    bar.queue_size--;
                    bar.tables[where[0]].seats[where[1] + i].status = true;                           // seat is occupied
                    bar.tables[where[0]].seats[where[1] + i].type = entry->type;                      // set student in seat
                    bar.tables[where[0]].seats[where[1] + i].id = entry->group_id;                    // set id of seat
                    bar.tables[where[0]].group_ids[bar.tables[where[0]].gid_index] = entry->group_id; // set id of seat
                    // printk(KERN_EMERG "Individual %d seated at table %d seat %d\n", i, where[0], where[1] + i);
                }
                if (bar.tables[where[0]].gid_index < 7)
                    bar.tables[where[0]].gid_index++; // increment index
                //struct timer *entryTime; //entry time of group
                // printk(KERN_EMERG "Allocating memory for timer entry\n");
                entryTime = kmalloc(sizeof(struct timer) * 1, __GFP_RECLAIM);
                if(entryTime == NULL)
                    return -ENOMEM;
                // printk(KERN_EMERG "Memory for timer entry allocated\n");
                ktime_get_real_ts64(&entryTime->time_entered); // setting the time for only the first person in the seat since they are a group
                // printk(KERN_EMERG "time for entry is: %llu\n", (unsigned long long)entryTime->time_entered.tv_sec);
                entryTime->tableNumb = where[0];
                entryTime->seatNumb = where[1];
                entryTime->done = false;
                entryTime->groupSize = entry->size;
                entryTime->entryType = entry->type;
                entryTime->time_id = entry->group_id;
                entryTime->containsData = true;
                // printk(KERN_EMERG "Time variables set\n");
                list_add_tail(&entryTime->time, &bar.tables[where[0]].times); // add to list of times
                //times[timerTracker++] = entryTime;
                // printk(KERN_EMERG "added time to times list\n");
                bar.tables[where[0]].num_times++;
            }
            // printk(KERN_EMERG "finsihed seating groups\n");
            // delete all groups in waiting area (AKA all nodes)
            list_for_each_safe(temp, dummy, &bar.waiting) // delete from waiting area
            {
                // ssleep(1); // takes 1 second to remove group
                // printk(KERN_EMERG "third loop\n");
                entry = list_entry(temp, struct group, in_line); // grab a group from the queue
                // printk(KERN_EMERG "grabbed entry to delete size is: %d\n", entry->size);
                list_del(temp);   // remove from waiting area
                kfree(entry);     // free memory
                bar.num_groups--; // decrement number of groups
                // REDUCE QUEUE SIZE BY GROUP SIZE
                //  printk(KERN_EMERG "Entry deleted from waiting area\n");
            }

            // printk(KERN_EMERG "lx------------------------------------------------lx");
            // printk(KERN_EMERG "UNLOCKING");
            // mutex_unlock(&WaiterWorks_mutex);
            // printk(KERN_EMERG "UNLOCKED");
            // printk(KERN_EMERG "table[0] seat[0] status: %d", bar.tables[0].seats[0].status);

            // CHECK TIME AND REMOVE STUDENTS

            list_for_each_safe(temp, dummy, &bar.tables[waiter.current_table].times) // iterate through times at table
            {
                int j;
                entryTime = list_entry(temp, struct timer, time); // grab a time from the list
                if (!entryTime->containsData || entryTime->done) // if time is null or time is done
                {
                    // printk(KERN_EMERG "Time is null or time is done\n");
                    continue; // skip
                }
                else
                {

                    if(isTimeUp(entryTime->entryType, entryTime->time_entered)) //if time is up
                    {
                        bar.num_in_bar -= entryTime->groupSize; // decrement number of people in bar
                        bar.customer_num[entryTime->entryType] -= entryTime->groupSize; // decrement number of people in bar
                        for (j = 0; j < entryTime->groupSize; j++) //iterate through seats
                        {
                            bar.tables[entryTime->tableNumb].seats[entryTime->seatNumb + j].status = false; //set seat to unoccupied
                            bar.tables[entryTime->tableNumb].seats[entryTime->seatNumb + j].dirty = true; //set seat to dirty
                            bar.tables[entryTime->tableNumb].seats[entryTime->seatNumb + j].id = -1; //set seat id to -1 for unoccupied

                            bar.customer_serviced++; // this increments the number of customers serviced
                            entryTime->done = true; //set time to done
                            // printk(KERN_EMERG "Student %d removed from table %d seat %d\n", j, entryTime->tableNumb, entryTime->seatNumb + j);
                        }
                        // printk(KERN_EMERG "Time being deleted from list\n");
                        list_del(temp); // remove from list
                        kfree(entryTime); // free memory
                        bar.tables[entryTime->tableNumb].num_times--; // decrement number of times
                        // printk(KERN_EMERG "Time deleted from list\n");
                    }
                    // else
                    // {
                    //     // printk(KERN_EMERG "Time not up yet\n");
                    // }
                }
            }
            
            bar.bar_state = MOVING;
            mutex_unlock(&WaiterWorks_mutex); // UNLOCK
            ssleep(2); // takes 1 second to seat group
            mutex_lock(&WaiterWorks_mutex); // LOCK

            waiter.current_table = (waiter.current_table + 1) % 4; // move to next table
            waiter.current_seat = 0;                               // set seat to 0
            waiter.serving = bar.tables[waiter.current_table];     // set table to current table
            printk(KERN_EMERG "Waiter moved to table %d\n", waiter.current_table);
            
            if (isEmpty() && isDirty())   // if table is empty && dirty
            {
                printk(KERN_EMERG "CLEANING TABLE");
                mutex_unlock(&WaiterWorks_mutex);                    // UNLOCK
                bar.bar_state = CLEANING;                            // bar state is set to cleaning
                ssleep(10);                                           // takes 10 seconds to clean table
                mutex_lock(&WaiterWorks_mutex);                      // LOCK
                waiter_cleans();                                     // clean table
                printk(KERN_EMERG "TABLE CLEANED");
            }
            mutex_unlock(&WaiterWorks_mutex);
        }

    }
    return 0;
}

extern int (*STUB_initialize_bar)(void);
int initialize_bar(void) // main? constructor?
{
    printk(KERN_EMERG "Initializing bar\n");
    if (bar.bar_state != OFFLINE) // if the state of the bar is active return 1
        return 1;
    else if (bar.bar_state == OFFLINE)
    {
        // set timer to 0!!!!!!!!!
        waiter.current_table = 0;
        waiter.current_seat = 0;
        waiter.serving = bar.tables[0];
        mutex_init(&waiter.Qmutex);
        bar.queue_size = 0;
        bar.num_groups = 0;
        bar.num_in_bar = 0;

        bar.bar_state = IDLE;

        waiter.kthread = kthread_run(waiter_works, NULL, "waiter");
        if (IS_ERR(waiter.kthread))
        {
            printk(KERN_EMERG "Error creating thread\n");
            return PTR_ERR(waiter.kthread);
        }
        else
        {
            printk(KERN_EMERG "Thread created successfully\n");
            printk(KERN_EMERG "Waiter's current table: %d\n", waiter.current_table);
            printk(KERN_EMERG "Waiter's current seat: %d\n", waiter.current_seat);
        }

        return 0;
    }
    else
        return -1;
    return 0;
}
extern int (*STUB_close_bar)(void);
static int close_bar(void) // destructor?
{
    // DEALLOCATE MEMORY
    if (bar.bar_state == OFFLINE) // if the close bar request is redundant
        return 1;
    else
    {
        int t;
        t = kthread_stop(waiter.kthread); // stop waiter thread
        if (t != -EINTR)
        {
            printk(KERN_EMERG "Stopped thread");
            // unload the customers
            bar.bar_state = OFFLINE;
        }
        mutex_destroy(&waiter.Qmutex);
    }
    return 0;
}
extern int (*STUB_customer_arrival)(int, int);
static int customer_arrival(int number_of_customers, int type)
{
    mutex_lock(&waiter.Qmutex); // LOCK
    printk(KERN_EMERG "Group of size %d of type %d passed into customer_arrival\n", number_of_customers, type);
    struct group *g;                 // group of students

    if (number_of_customers > 8 || number_of_customers < 0 || type < 0 || type > 4) // if group is too large
    {
        printk(KERN_EMERG "Group is too large/small or type is invalid\n");
        return 0;
    }

    if (bar.queue_size > ENTRY_SIZE) // if queue is full
        return 0;
    g = kmalloc(sizeof(struct group) * 1, __GFP_RECLAIM); // allocate memory for group
    if (g == NULL)                                        // check if memory was allocated
        return -ENOMEM;
    g->size = number_of_customers; // set size of group
    printk(KERN_EMERG "number_of_customers is: %d\n", number_of_customers);
    printk(KERN_EMERG "Size of group is: %d\n", g->size);
    g->type = type; // set type of group
    printk(KERN_EMERG "Type of group is: %d\n", g->type);
    g->group_id = bar.num_ids; // set group id
    printk(KERN_EMERG "ID of group is: %d\n", g->group_id);
    bar.num_ids++;                          // increment number of ids
    list_add_tail(&g->in_line, &bar.queue); // add group to queue
    bar.queue_size += number_of_customers;  // increment size of queue
    bar.num_groups++;                       // increment number of groups
    bar.total_groups++;

    struct group *p;
    struct list_head *temp;
    list_for_each(temp, &bar.queue)
    {
        p = list_entry(temp, struct group, in_line);
        printk(KERN_EMERG "Group of %d students of type %d has entered the bar\n", p->size, p->type);
    }
    mutex_unlock(&waiter.Qmutex); // UNLOCK
    return 0;
}

static int bar_init(void)
{
    // Set the pointers to the STUB functions
    STUB_initialize_bar = initialize_bar;
    STUB_customer_arrival = customer_arrival;
    STUB_close_bar = close_bar;

    ktime_get_real_ts64(&start_time);

    // Create the proc file
    proc_entry = proc_create(PROC_NAME, 0666, NULL, &timer_fops); // create proc entry
    if (proc_entry == NULL)
        return -ENOMEM;

    bar.bar_state = OFFLINE; // set bar state to offline
    bar.num_ids = 0;         // set number of ids to 0
    // printk(KERN_INFO "Bar_init state: %d\n", bar.bar_state); //TEST PASSED!
    INIT_LIST_HEAD(&bar.queue); // initialize the queue
    INIT_LIST_HEAD(&bar.waiting);

    int i, j;
    for (i = 0; i < 4; i++) // for every table
    {
        INIT_LIST_HEAD(&bar.tables[i].groups); // initialize the groups list for every table

        INIT_LIST_HEAD(&bar.tables[i].times); // initialize the times list for every table
        bar.tables[i].gid_index = 0;           // set the index of the group id to 0
        bar.tables[i].num_times = 0;           // set the number of times to 0
            // printk(KERN_ALERT "Bar table: %d initialized\n", i);
            for (j = 0; j < 8; j++)
        {
            bar.tables[i].seats[j].dirty = false;  // set all seats to clean
            bar.tables[i].seats[j].status = false; // set all seats to empty
        }
    }

    // int index;
    // for (index = 0; index < 5; index++)
    // {
    //     times[i].containsData = false;
    // }
    return 0;
}
static void bar_exit(void)
{
    proc_remove(proc_entry);    // remove proc entry
    STUB_initialize_bar = NULL; // set STUB functions to NULL
    STUB_customer_arrival = NULL;
    STUB_close_bar = NULL;
    bar.bar_state = OFFLINE;                                 // set bar state to offline
    printk(KERN_INFO "Bar_exit state: %d\n", bar.bar_state); // TEST PASSED!

    struct list_head *temp, *dummy;
    struct group *g;
    mutex_lock(&waiter.Qmutex); // lock mutex
    list_for_each_safe(temp, dummy, &bar.queue)
    { /* forwards */
        g = list_entry(temp, struct group, in_line);
        printk(KERN_EMERG "grabbed queue entry in bar_exit to delete size is: %d\n", g->size);
        list_del(temp); /* removes entry from list */
        kfree(g);
    }
    list_for_each_safe(temp, dummy, &bar.waiting) // delete from waiting area
    {
        g = list_entry(temp, struct group, in_line); // grab a group from the queue
        printk(KERN_EMERG "grabbed waiting entry in bar_exit to delete size is: %d\n", g->size);
        list_del(temp); // remove from waiting area
        kfree(g);     // free memory
    }
    //CHECKING IF QUEUE IS EMPTY
    list_for_each_safe(temp, dummy, &bar.queue)
    { /* forwards */
        g = list_entry(temp, struct group, in_line);
        printk(KERN_EMERG "Entries in queue after deletion: %d\n", g->size);
    }
    list_for_each_safe(temp, dummy, &bar.waiting)
    { /* forwards */
        g = list_entry(temp, struct group, in_line);
        printk(KERN_EMERG "Entries in waiting after deletion: %d\n", g->size);
    }
    mutex_unlock(&waiter.Qmutex); // unlock mutex
    // kthread_stop(waiter.kthread); // stop waiter thread
    return;
}

module_init(bar_init);
module_exit(bar_exit);
