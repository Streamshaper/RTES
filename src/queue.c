#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "queue.h"

queue_t *queue_create(size_t capacity)
{
    queue_t *queue = NULL;

    if (capacity == 0) {
        return NULL;
    }

    queue = (queue_t *)malloc(sizeof(queue_t));
    if (queue == NULL) {
        return NULL;
    }

    // Allocate array for string pointers.
    queue->items = (char **)calloc(capacity, sizeof(char *));
    if (queue->items == NULL) {
        free(queue);
        return NULL;
    }

    queue->capacity = capacity;
    queue->head = 0;
    queue->tail = 0;
    queue->count = 0;

    // Initialize synchronization primitives.
    if (pthread_mutex_init(&queue->mutex, NULL) != 0) {
        free(queue->items);
        free(queue);
        return NULL;
    }

    if (pthread_cond_init(&queue->not_empty, NULL) != 0) {
        pthread_mutex_destroy(&queue->mutex);
        free(queue->items);
        free(queue);
        return NULL;
    }

    if (pthread_cond_init(&queue->not_full, NULL) != 0) {
        pthread_cond_destroy(&queue->not_empty);
        pthread_mutex_destroy(&queue->mutex);
        free(queue->items);
        free(queue);
        return NULL;
    }

    return queue;
}

void queue_destroy(queue_t *queue)
{
    size_t i;

    if (queue == NULL) {
        return;
    }

    // Free all pending string payloads before destroying the queue.
    pthread_mutex_lock(&queue->mutex);
    for (i = 0; i < queue->count; ++i) {
        size_t index = (queue->head + i) % queue->capacity;
        free(queue->items[index]);
        queue->items[index] = NULL;
    }
    pthread_mutex_unlock(&queue->mutex);

    pthread_cond_destroy(&queue->not_empty);
    pthread_cond_destroy(&queue->not_full);
    pthread_mutex_destroy(&queue->mutex);
    free(queue->items);
    free(queue);
}

int queue_push(queue_t *queue, const char *message)
{
    char *copy = NULL;

    if (queue == NULL || message == NULL) {
        return -1;
    }

    copy = strdup(message);
    if (copy == NULL) {
        return -1;
    }

    pthread_mutex_lock(&queue->mutex);
    
    // Block until there is space in the queue.
    while (queue->count == queue->capacity) {
        pthread_cond_wait(&queue->not_full, &queue->mutex);
    }

    // Insert item and advance tail.
    queue->items[queue->tail] = copy;
    queue->tail = (queue->tail + 1) % queue->capacity;
    queue->count++;

    // Wake up a waiting consumer.
    pthread_cond_signal(&queue->not_empty);
    pthread_mutex_unlock(&queue->mutex);
    return 0;
}

int queue_pop(queue_t *queue, char *buffer, size_t buffer_size)
{
    char *item = NULL;

    if (queue == NULL || buffer == NULL || buffer_size == 0) {
        return -1;
    }

    pthread_mutex_lock(&queue->mutex);
    
    // Block until an item is available.
    while (queue->count == 0) {
        pthread_cond_wait(&queue->not_empty, &queue->mutex);
    }

    // Extract item and advance head.
    item = queue->items[queue->head];
    queue->items[queue->head] = NULL;
    queue->head = (queue->head + 1) % queue->capacity;
    queue->count--;

    // Wake up a waiting producer.
    pthread_cond_signal(&queue->not_full);
    pthread_mutex_unlock(&queue->mutex);

    if (item == NULL) {
        buffer[0] = '\0';
        return -1;
    }

    // Copy data to caller's buffer and free the internal copy.
    snprintf(buffer, buffer_size, "%s", item);
    free(item);
    return 0;
}

size_t queue_count_locked(const queue_t *queue)
{
    const queue_t *q = queue;
    if (q == NULL) {
        return 0;
    }

    return q->count; // Assumes caller holds the mutex.
}

size_t queue_size(const queue_t *queue)
{
    size_t count = 0;
    queue_t *q = (queue_t *)queue;

    if (q == NULL) {
        return 0;
    }

    // Thread-safe count check.
    pthread_mutex_lock(&q->mutex);
    count = q->count;
    pthread_mutex_unlock(&q->mutex);
    return count;
}

size_t queue_capacity(const queue_t *queue)
{
    return queue == NULL ? 0 : queue->capacity;
}

double queue_occupancy_pct(const queue_t *queue)
{
    queue_t *q = (queue_t *)queue;
    size_t count = 0;

    if (q == NULL || q->capacity == 0) {
        return 0.0;
    }

    pthread_mutex_lock(&q->mutex);
    count = q->count;
    pthread_mutex_unlock(&q->mutex);

    return ((double)count / (double)q->capacity) * 100.0;
}