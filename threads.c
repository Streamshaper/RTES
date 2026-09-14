#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <cjson/cJSON.h>

#include "threads.h"

#define MESSAGE_BUFFER_SIZE 512
#define LOG_FILE_NAME "metrics_log.txt"
#define TIMING_FILE_NAME "timing_metrics.txt"

#if SYNTHETIC_BURST_DEMO
#define SYNTHETIC_BURST_START_MS 2000LL
#define SYNTHETIC_BURST_HOLD_MS 3000LL
#endif

static int read_proc_stat_jiffies(unsigned long long *total_jiffies,
                                 unsigned long long *idle_jiffies)
{
    FILE *stat_file = fopen("/proc/stat", "r");
    char line[256];
    unsigned long long user, nice, system, idle, iowait, irq, softirq, steal;
    unsigned long long guest, guest_nice;

    if (stat_file == NULL) {
        return -1;
    }

    if (fgets(line, sizeof(line), stat_file) == NULL) {
        fclose(stat_file);
        return -1;
    }
    fclose(stat_file);

    if (sscanf(line,
               "cpu %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu",
               &user, &nice, &system, &idle, &iowait, &irq, &softirq,
               &steal, &guest, &guest_nice) != 10) {
        return -1;
    }

    *idle_jiffies = idle + iowait;
    *total_jiffies = user + nice + system + idle + iowait + irq + softirq + steal + guest + guest_nice;
    return 0;
}

int telemetry_init(telemetry_context_t *ctx, queue_t *queue, long duration_seconds)
{
    if (ctx == NULL || queue == NULL) {
        return -1;
    }

    memset(ctx, 0, sizeof(*ctx));
    ctx->queue = queue;
    ctx->stop_requested = 0;
    ctx->duration_seconds = duration_seconds;
    ctx->last_deadline_ns = 0;
    ctx->drift_ns = 0;
    clock_gettime(CLOCK_MONOTONIC, &ctx->start_time);
    pthread_mutex_init(&ctx->mutex, NULL);
    return 0;
}

void telemetry_reset_window(telemetry_context_t *ctx)
{
    if (ctx == NULL) {
        return;
    }

    pthread_mutex_lock(&ctx->mutex);
    ctx->commit_count = 0;
    ctx->identity_count = 0;
    ctx->account_count = 0;
    ctx->info_count = 0;
    pthread_mutex_unlock(&ctx->mutex);
}

void telemetry_record_message(telemetry_context_t *ctx, message_kind_t kind)
{
    if (ctx == NULL) {
        return;
    }

    pthread_mutex_lock(&ctx->mutex);
    switch (kind) {
        case MESSAGE_KIND_COMMIT:
            ctx->commit_count++;
            break;
        case MESSAGE_KIND_IDENTITY:
            ctx->identity_count++;
            break;
        case MESSAGE_KIND_ACCOUNT:
            ctx->account_count++;
            break;
        case MESSAGE_KIND_INFO:
            ctx->info_count++;
            break;
        default:
            break;
    }
    pthread_mutex_unlock(&ctx->mutex);
}

/* Parse the "kind" field from a JSON frame and map it to a message category. */
message_kind_t parse_message_kind(const char *json)
{
    cJSON *root = NULL;
    cJSON *kind = NULL;
    message_kind_t message_kind = MESSAGE_KIND_UNKNOWN;

    if (json == NULL) {
        return MESSAGE_KIND_UNKNOWN;
    }

    root = cJSON_Parse(json);
    if (root == NULL || !cJSON_IsObject(root)) {
        cJSON_Delete(root);
        return MESSAGE_KIND_UNKNOWN;
    }

    kind = cJSON_GetObjectItemCaseSensitive(root, "kind");
    if (cJSON_IsString(kind) && kind->valuestring != NULL) {
        if (strcmp(kind->valuestring, "commit") == 0) {
            message_kind = MESSAGE_KIND_COMMIT;
        } else if (strcmp(kind->valuestring, "identity") == 0) {
            message_kind = MESSAGE_KIND_IDENTITY;
        } else if (strcmp(kind->valuestring, "account") == 0) {
            message_kind = MESSAGE_KIND_ACCOUNT;
        } else if (strcmp(kind->valuestring, "info") == 0) {
            message_kind = MESSAGE_KIND_INFO;
        }
    }

    cJSON_Delete(root);
    return message_kind;
}

static queue_t *g_queue = NULL;

static int jetstream_callback(struct lws *wsi,
                             enum lws_callback_reasons reason,
                             void *user,
                             void *in,
                             size_t len)
{
    (void)wsi;
    (void)user;

    switch (reason) {
        case LWS_CALLBACK_CLIENT_RECEIVE: {
            char *payload = (char *)malloc(len + 1);
            if (payload == NULL) {
                return 0;
            }

            memcpy(payload, in, len);
            payload[len] = '\0';

            if (g_queue != NULL) {
                while (queue_push(g_queue, payload) != 0) {
                    usleep(1000);
                }
            }

            free(payload);
            break;
        }
        case LWS_CALLBACK_CLIENT_CONNECTION_ERROR:
            fprintf(stderr, "WebSocket connection error: %s\n", (char *)in);
            break;
        case LWS_CALLBACK_CLOSED:
            break;
        default:
            break;
    }

    return 0;
}

static const struct lws_protocols jetstream_protocols[] = {
    { "ws", jetstream_callback, 0, 1024, 0, NULL, 0 },
    { NULL, NULL, 0, 0, 0, NULL, 0 }
};

/* Producer thread: connects to the Bluesky Jetstream WebSocket and pushes each JSON frame into the bounded queue. */
void *producer_thread(void *args)
{
    telemetry_context_t *ctx = (telemetry_context_t *)args;
    struct lws_context_creation_info info;
    struct lws_client_connect_info connect_info;
    struct timespec start_time;
    const char *uri = "wss://jetstream1.us-east.bsky.network/subscribe?wantedCollections=app.bsky.feed.post";

    if (ctx == NULL || ctx->queue == NULL) {
        return NULL;
    }

    g_queue = ctx->queue;
    memset(&info, 0, sizeof(info));
    info.port = CONTEXT_PORT_NO_LISTEN;
    info.protocols = jetstream_protocols;
    info.options = LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT;
    info.gid = -1;
    info.uid = -1;

    ctx->lws_context = lws_create_context(&info);
    if (ctx->lws_context == NULL) {
        fprintf(stderr, "Failed to create libwebsockets context.\n");
        return NULL;
    }

    memset(&connect_info, 0, sizeof(connect_info));
    connect_info.context = ctx->lws_context;
    connect_info.address = "jetstream1.us-east.bsky.network";
    connect_info.port = 443;
    connect_info.path = "/subscribe?wantedCollections=app.bsky.feed.post";
    connect_info.host = "jetstream1.us-east.bsky.network";
    connect_info.origin = "https://bsky.app";
    connect_info.protocol = "ws";
    connect_info.ietf_version_or_minus_one = -1;
    connect_info.ssl_connection = 1;

    clock_gettime(CLOCK_MONOTONIC, &start_time);
    ctx->lws_wsi = lws_client_connect_via_info(&connect_info);
    if (ctx->lws_wsi == NULL) {
        fprintf(stderr, "Failed to connect to %s\n", uri);
        lws_context_destroy(ctx->lws_context);
        ctx->lws_context = NULL;
        return NULL;
    }

    while (1) {
        lws_service(ctx->lws_context, 100);
    }

    return NULL;
}

/* Consumer thread: drains the queue, parses the JSON, and updates message counters. */
void *consumer_thread(void *args)
{
    telemetry_context_t *ctx = (telemetry_context_t *)args;
    char buffer[MESSAGE_BUFFER_SIZE];
#if SYNTHETIC_BURST_DEMO
    int synthetic_active = 0;
#endif

    if (ctx == NULL || ctx->queue == NULL) {
        return NULL;
    }

    while (1) {
#if SYNTHETIC_BURST_DEMO
        pthread_mutex_lock(&ctx->mutex);
        synthetic_active = ctx->synthetic_burst_active;
        pthread_mutex_unlock(&ctx->mutex);
#endif

        if (queue_pop(ctx->queue, buffer, sizeof(buffer)) != 0) {
#if SYNTHETIC_BURST_DEMO
            if (synthetic_active) {
                usleep(20000);
            }
#endif
            continue;
        }

#if SYNTHETIC_BURST_DEMO
        if (synthetic_active) {
            usleep(20000);
        }
#endif

        telemetry_record_message(ctx, parse_message_kind(buffer));
    }

    return NULL;
}

double read_cpu_usage_percent(telemetry_context_t *ctx)
{
    unsigned long long current_total_jiffies = 0ULL;
    unsigned long long current_idle_jiffies = 0ULL;
    unsigned long long total_delta = 0ULL;
    unsigned long long idle_delta = 0ULL;
    double usage_pct = 0.0;

    if (ctx == NULL) {
        return 0.0;
    }

    if (read_proc_stat_jiffies(&current_total_jiffies, &current_idle_jiffies) != 0) {
        return 0.0;
    }

    if (ctx->prev_total_jiffies == 0ULL && ctx->prev_idle_jiffies == 0ULL) {
        ctx->prev_total_jiffies = current_total_jiffies;
        ctx->prev_idle_jiffies = current_idle_jiffies;
        return 0.0;
    }

    total_delta = current_total_jiffies - ctx->prev_total_jiffies;
    idle_delta = current_idle_jiffies - ctx->prev_idle_jiffies;

    ctx->prev_total_jiffies = current_total_jiffies;
    ctx->prev_idle_jiffies = current_idle_jiffies;

    if (total_delta == 0ULL) {
        return 0.0;
    }

    usage_pct = 100.0 * (1.0 - ((double)idle_delta / (double)total_delta));
    if (usage_pct < 0.0) {
        usage_pct = 0.0;
    }
    if (usage_pct > 100.0) {
        usage_pct = 100.0;
    }

    return usage_pct;
}

#if SYNTHETIC_BURST_DEMO
static void enqueue_synthetic_burst(telemetry_context_t *ctx, size_t burst_count)
{
    char payload[128];
    size_t i = 0;

    if (ctx == NULL || ctx->queue == NULL) {
        return;
    }

    for (i = 0; i < burst_count; ++i) {
        snprintf(payload, sizeof(payload),
                 "{\"kind\":\"commit\",\"id\":\"synthetic-%zu\",\"demo\":\"burst\"}",
                 i);
        while (queue_push(ctx->queue, payload) != 0) {
            usleep(1000);
        }
    }
}

static void *synthetic_burst_thread(void *args)
{
    telemetry_context_t *ctx = (telemetry_context_t *)args;

    if (ctx == NULL || ctx->queue == NULL) {
        return NULL;
    }

    enqueue_synthetic_burst(ctx, queue_capacity(ctx->queue));
    usleep(SYNTHETIC_BURST_HOLD_MS * 1000);

    pthread_mutex_lock(&ctx->mutex);
    ctx->synthetic_burst_active = 0;
    pthread_mutex_unlock(&ctx->mutex);

    return NULL;
}

static void trigger_synthetic_burst(telemetry_context_t *ctx)
{
    pthread_t burst_thread;

    if (ctx == NULL || ctx->queue == NULL) {
        return;
    }

    pthread_mutex_lock(&ctx->mutex);
    ctx->synthetic_burst_active = 1;
    pthread_mutex_unlock(&ctx->mutex);

    if (pthread_create(&burst_thread, NULL, synthetic_burst_thread, ctx) == 0) {
        pthread_detach(burst_thread);
    } else {
        pthread_mutex_lock(&ctx->mutex);
        ctx->synthetic_burst_active = 0;
        pthread_mutex_unlock(&ctx->mutex);
    }
}
#endif

/* Monitor thread: writes one CSV row every second using a monotonic timer. */
void *monitor_thread(void *args)
{
    telemetry_context_t *ctx = (telemetry_context_t *)args;
    FILE *log_file = NULL;
    struct timespec next_tick;
    struct timespec real_ts;
    struct timespec mono_now;
    const char *header = "Seconds,Nanoseconds,Commit_Count,Identity_Count,Account_Count,Info_Count,Buffer_Occupancy_Pct,CPU_Pct";

    if (ctx == NULL) {
        return NULL;
    }

    log_file = fopen(LOG_FILE_NAME, "a");
    if (log_file == NULL) {
        return NULL;
    }

    if (access(LOG_FILE_NAME, F_OK) == 0) {
        long file_size = 0;
        FILE *size_check = fopen(LOG_FILE_NAME, "rb");
        if (size_check != NULL) {
            fseek(size_check, 0, SEEK_END);
            file_size = ftell(size_check);
            fclose(size_check);
        }
        if (file_size == 0L) {
            fprintf(log_file, "%s\n", header);
        }
    } else {
        fprintf(log_file, "%s\n", header);
    }
    fflush(log_file);

    FILE *timing_file = fopen(TIMING_FILE_NAME, "a");
#if SYNTHETIC_BURST_DEMO
    struct timespec burst_start;
    int synthetic_burst_triggered = 0;
#endif
    if (timing_file != NULL) {
        if (access(TIMING_FILE_NAME, F_OK) == 0) {
            long timing_size = 0;
            FILE *timing_probe = fopen(TIMING_FILE_NAME, "rb");
            if (timing_probe != NULL) {
                fseek(timing_probe, 0, SEEK_END);
                timing_size = ftell(timing_probe);
                fclose(timing_probe);
            }
            if (timing_size == 0L) {
                fprintf(timing_file, "Seconds,Nanoseconds,Jitter_ns,Drift_ns\n");
            }
        } else {
            fprintf(timing_file, "Seconds,Nanoseconds,Jitter_ns,Drift_ns\n");
        }
        fflush(timing_file);
    }

    clock_gettime(CLOCK_MONOTONIC, &next_tick);
#if SYNTHETIC_BURST_DEMO
    clock_gettime(CLOCK_MONOTONIC, &burst_start);
#endif
    next_tick.tv_sec += 1;
    ctx->last_deadline_ns = ((long long)next_tick.tv_sec * 1000000000LL) + (long long)next_tick.tv_nsec;

    while (1) {
        unsigned long commit_count = 0UL;
        unsigned long identity_count = 0UL;
        unsigned long account_count = 0UL;
        unsigned long info_count = 0UL;
        double occupancy_pct = 0.0;
        double cpu_pct = 0.0;
        long long expected_ns = 0LL;
        long long actual_ns = 0LL;
        long long jitter_ns = 0LL;
    #if SYNTHETIC_BURST_DEMO
        struct timespec now_ts;
        long long elapsed_ms = 0LL;
    #endif

        clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &next_tick, NULL);

    #if SYNTHETIC_BURST_DEMO
        clock_gettime(CLOCK_MONOTONIC, &now_ts);
        elapsed_ms = ((long long)now_ts.tv_sec - (long long)burst_start.tv_sec) * 1000LL +
                     ((long long)now_ts.tv_nsec - (long long)burst_start.tv_nsec) / 1000000LL;

        if (!synthetic_burst_triggered && elapsed_ms >= SYNTHETIC_BURST_START_MS) {
            trigger_synthetic_burst(ctx);
            synthetic_burst_triggered = 1;
        }
    #endif

        clock_gettime(CLOCK_REALTIME, &real_ts);
        clock_gettime(CLOCK_MONOTONIC, &mono_now);
        cpu_pct = read_cpu_usage_percent(ctx);
        occupancy_pct = queue_occupancy_pct(ctx->queue);

        pthread_mutex_lock(&ctx->mutex);
        commit_count = ctx->commit_count;
        identity_count = ctx->identity_count;
        account_count = ctx->account_count;
        info_count = ctx->info_count;
        ctx->commit_count = 0UL;
        ctx->identity_count = 0UL;
        ctx->account_count = 0UL;
        ctx->info_count = 0UL;
        pthread_mutex_unlock(&ctx->mutex);

        expected_ns = ctx->last_deadline_ns;
        actual_ns = ((long long)mono_now.tv_sec * 1000000000LL) + (long long)mono_now.tv_nsec;
        jitter_ns = llabs(actual_ns - expected_ns);
        ctx->drift_ns += (actual_ns - expected_ns);
        ctx->last_deadline_ns += 1000000000LL;

        fprintf(log_file,
                "%ld,%ld,%lu,%lu,%lu,%lu,%.2f,%.2f\n",
                (long)real_ts.tv_sec,
                (long)real_ts.tv_nsec,
                commit_count,
                identity_count,
                account_count,
                info_count,
                occupancy_pct,
                cpu_pct);
        fflush(log_file);

        if (timing_file != NULL) {
            fprintf(timing_file,
                    "%ld,%ld,%lld,%lld\n",
                    (long)real_ts.tv_sec,
                    (long)real_ts.tv_nsec,
                    jitter_ns,
                    ctx->drift_ns);
            fflush(timing_file);
        }

        next_tick.tv_sec += 1;
    }

    if (timing_file != NULL) {
        fclose(timing_file);
    }
    fclose(log_file);
    return NULL;
}
