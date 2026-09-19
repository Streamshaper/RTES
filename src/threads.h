#ifndef THREADS_H
#define THREADS_H

#include <pthread.h>
#include <stddef.h>

#include <libwebsockets.h>

#include "queue.h"

typedef enum {
    MESSAGE_KIND_COMMIT = 0,
    MESSAGE_KIND_IDENTITY,
    MESSAGE_KIND_ACCOUNT,
    MESSAGE_KIND_INFO,
    MESSAGE_KIND_UNKNOWN
} message_kind_t;

typedef struct {
    queue_t *queue;
    pthread_mutex_t mutex;
    int synthetic_burst_active;
    int stop_requested;
    struct timespec start_time;
    struct lws_context *lws_context;
    struct lws *lws_wsi;
    unsigned long commit_count;
    unsigned long identity_count;
    unsigned long account_count;
    unsigned long info_count;
    unsigned long long prev_total_jiffies;
    unsigned long long prev_idle_jiffies;
    long duration_seconds;
} telemetry_context_t;

int telemetry_init(telemetry_context_t *ctx, queue_t *queue, long duration_seconds);
void status_log(const char *format, ...);
void telemetry_reset_window(telemetry_context_t *ctx);
void telemetry_record_message(telemetry_context_t *ctx, message_kind_t kind);
message_kind_t parse_message_kind(const char *json);
void *producer_thread(void *args);
void *consumer_thread(void *args);
void *monitor_thread(void *args);
double read_cpu_usage_percent(telemetry_context_t *ctx);

#endif