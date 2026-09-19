#ifndef QUEUE_H
#define QUEUE_H

#include <pthread.h>
#include <stddef.h>

#define DEFAULT_QUEUE_CAPACITY 32

/* Bounded circular queue used as the producer-consumer buffer. */
typedef struct {
    char **items;
    size_t capacity;
    size_t head;
    size_t tail;
    size_t count;
    pthread_mutex_t mutex;
    pthread_cond_t not_empty;
    pthread_cond_t not_full;
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