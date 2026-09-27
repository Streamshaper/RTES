#ifndef QUEUE_H
#define QUEUE_H

#include <pthread.h>
#include <stddef.h>

#define DEFAULT_QUEUE_CAPACITY 32

/* Bounded circular queue used as the producer-consumer buffer. */
typedef struct {
    char **items;              // Array of dynamically allocated string payloads.
    size_t capacity;           // Maximum item limit.
    size_t head;               // Read index for consumers.
    size_t tail;               // Write index for producers.
    size_t count;              // Current number of items.
    pthread_mutex_t mutex;     // Protects queue state.
    pthread_cond_t not_empty;  // Signals consumer when data arrives.
    pthread_cond_t not_full;   // Signals producer when space clears.
} queue_t;

queue_t *queue_create(size_t capacity);
void queue_destroy(queue_t *queue);
int queue_push(queue_t *queue, const char *message);
int queue_pop(queue_t *queue, char *buffer, size_t buffer_size);
size_t queue_size(const queue_t *queue);
size_t queue_capacity(const queue_t *queue);
double queue_occupancy_pct(const queue_t *queue);
size_t queue_count_locked(const queue_t *queue);

#endif // QUEUE_H