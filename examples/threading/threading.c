#include "threading.h"
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>

// Optional: use these functions to add debug or error prints to your application
#define DEBUG_LOG(msg,...)
//#define DEBUG_LOG(msg,...) printf("threading: " msg "\n" , ##__VA_ARGS__)
#define ERROR_LOG(msg,...) printf("threading ERROR: " msg "\n" , ##__VA_ARGS__)

void* threadfunc(void* thread_param)
{
    struct thread_data* thread_func_args = (struct thread_data *) thread_param;
    bool* result = &thread_func_args->thread_complete_success;
    int ret;

    // wait for wait_to_obtain_ms
    ret = usleep(thread_func_args->wait_to_obtain_ms * 1000);
    if (ret != 0) {
        *result = false;
        ERROR_LOG("usleep #1 failed");
        return thread_param;
    }

    // obtain mutex
    ret = pthread_mutex_lock(thread_func_args->mutex);
    if (ret != 0) {
        *result = false;
        ERROR_LOG("pthread_mutex_lock failed");
        return thread_param;
    }

    // wait for wait_to_release_ms
    ret = usleep(thread_func_args->wait_to_release_ms * 1000);
    if (ret != 0) {
        *result = false;
        ERROR_LOG("usleep #2 failed");
        return thread_param;
    }

    // release mutex
    ret = pthread_mutex_unlock(thread_func_args->mutex);
    if (ret != 0) {
        *result = false;
        ERROR_LOG("pthread_mutex_unlock failed");
        return thread_param;
    }

    *result = true;
    return thread_param;
}


bool start_thread_obtaining_mutex(pthread_t *thread, pthread_mutex_t *mutex,int wait_to_obtain_ms, int wait_to_release_ms)
{
    struct thread_data *thread_data = malloc(sizeof(struct thread_data));
    if (thread_data == NULL) {
        return false;
    }
    thread_data->wait_to_obtain_ms = wait_to_obtain_ms;
    thread_data->wait_to_release_ms = wait_to_release_ms;
    thread_data->mutex = mutex;
    thread_data->thread_complete_success = false;

    int ret = pthread_create(thread, NULL, threadfunc, (void *)thread_data);
    if (ret != 0) {
        return false;
    }

    return true;
}

