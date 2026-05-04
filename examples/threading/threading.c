#include "threading.h"
#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>

// Optional: use these functions to add debug or error prints to your application
#define DEBUG_LOG(msg,...)
//#define DEBUG_LOG(msg,...) printf("threading: " msg "\n" , ##__VA_ARGS__)
#define ERROR_LOG(msg,...) printf("threading ERROR: " msg "\n" , ##__VA_ARGS__)

// sleep for @param ms milliseconds
static void mssleep(int ms) {
    usleep(ms*1000);
}

void* threadfunc(void* thread_param)
{
    // TODO: wait, obtain mutex, wait, release mutex as described by thread_data structure
    // hint: use a cast like the one below to obtain thread arguments from your parameter
    struct thread_data *data = (struct thread_data *)thread_param;
    data->thread_complete_success = false;

    mssleep(data->wait_to_obtain_ms);

    if (pthread_mutex_lock(data->mutex) != 0)
    {
        perror("pthread_mutex_lock");
        return thread_param;
    }

    mssleep(data->wait_to_release_ms);

    if (pthread_mutex_unlock(data->mutex) != 0)
    {
        perror("pthread_mutex_unlock");
        return thread_param;
    }

    data->thread_complete_success = true;
    return thread_param;
}


bool start_thread_obtaining_mutex(pthread_t *thread, pthread_mutex_t *mutex,int wait_to_obtain_ms, int wait_to_release_ms)
{
    /**
     * TODO: allocate memory for thread_data, setup mutex and wait arguments, pass thread_data to created thread
     * using threadfunc() as entry point.
     *
     * return true if successful.
     *
     * See implementation details in threading.h file comment block
     */
    bool threadStartedSuccessfully = false;
    struct thread_data *data = malloc(sizeof(struct thread_data));
    data->wait_to_obtain_ms = wait_to_obtain_ms;
    data->wait_to_release_ms = wait_to_release_ms;
    data->thread_complete_success = false;
    data->mutex = mutex;

    // Create the thread, passing data as argument
    if (pthread_create(thread, NULL, threadfunc, data) != 0) {
        perror("pthread_create");
        pthread_mutex_destroy(mutex);
        free(data);
        threadStartedSuccessfully = false;
    }
    else
    {
        threadStartedSuccessfully = true;
    }

    return threadStartedSuccessfully;
}

