#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>

#include "queue.h"
#include "threads.h"

#define MONITOR_RUNTIME_SECONDS (48 * 60 * 60)

/* Entry point for the real-time telemetry example. */
int main(void)
{
    queue_t *queue = NULL;
    telemetry_context_t telemetry;
    pthread_t producer_tid;
    pthread_t consumer_tid;
    pthread_t monitor_tid;

    queue = queue_create(DEFAULT_QUEUE_CAPACITY);
    if (queue == NULL) {
        status_log("main: queue initialization failed");
        return EXIT_FAILURE;
    }

    if (telemetry_init(&telemetry, queue, MONITOR_RUNTIME_SECONDS) != 0) {
        status_log("main: telemetry initialization failed");
        queue_destroy(queue);
        return EXIT_FAILURE;
    }

    if (pthread_create(&producer_tid, NULL, producer_thread, &telemetry) != 0) {
        status_log("main: producer thread creation failed");
        queue_destroy(queue);
        return EXIT_FAILURE;
    }

    if (pthread_create(&consumer_tid, NULL, consumer_thread, &telemetry) != 0) {
        status_log("main: consumer thread creation failed");
        queue_destroy(queue);
        return EXIT_FAILURE;
    }

    if (pthread_create(&monitor_tid, NULL, monitor_thread, &telemetry) != 0) {
        status_log("main: monitor thread creation failed");
        queue_destroy(queue);
        return EXIT_FAILURE;
    }

    pthread_join(producer_tid, NULL);
    pthread_join(consumer_tid, NULL);
    pthread_join(monitor_tid, NULL);

    queue_destroy(queue);
    return EXIT_SUCCESS;
}


