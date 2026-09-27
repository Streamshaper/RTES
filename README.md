# Bluesky Jetstream Telemetry Logger

A lightweight, high-performance C application designed to ingest and monitor real-time events from the Bluesky Jetstream network. Built using POSIX threads, it employs a highly efficient producer-consumer architecture to process continuous WebSocket streams and log telemetry data at exactly 1Hz.

## Architecture

The logger is divided into three primary concurrent threads communicating via a thread-safe, bounded circular queue:

* **Producer Thread**: Establishes a secure WebSocket connection to the Bluesky Jetstream network (`wss://jetstream1.us-east.bsky.network`) using `libwebsockets`. It captures incoming JSON frames and pushes them into the shared queue. If the connection drops, an atomic flag triggers an automatic reconnection loop.


* **Consumer Thread**: Continuously drains the queue, utilizing `cJSON` to parse the payloads. It categorizes messages by their `kind` field (e.g., `commit`, `identity`, `account`, `info`) and safely increments rolling performance counters.


* **Monitor Thread**: Awakens exactly once per second using a monotonic clock to record telemetry. It calculates system CPU utilization by reading jiffies from `/proc/stat`, tracks the percentage of queue occupancy to monitor backpressure, and writes a CSV row to the disk. It also acts as a watchdog, forcing a network reset if five consecutive seconds pass without any metrics.



## Dependencies

To build and run this project, you will need a C11-compatible compiler and the following libraries installed on your system:

* `libwebsockets`

* `cJSON`

* `pthread` (Standard POSIX threads)



On Debian/Ubuntu systems, you can typically install the required development headers via:

```bash
sudo apt-get install build-essential libwebsockets-dev libcjson-dev

```

## Building the Project

The project includes a `Makefile` with several targets for easy compilation.

* **Build the standard executable**:
```bash
make

```


*(Alternatively, run `make all`)*

* **Build with synthetic load testing**:
```bash
make demo

```


This compiles the application with `SYNTHETIC_BURST_DEMO=1`, which injects simulated bursts of messages to test queue backpressure and consumer processing speeds.


* **Clean build artifacts**:
```bash
make clean

```


* **View available make commands**:
```bash
make help

```



## Usage and Logging

Run the compiled executable directly from the terminal:

```bash
./main

```

Ensure that a `logs/` directory exists in the same directory as the executable, or the file outputs will fail to open.

The application generates two log files:

* **`logs/metrics_log.txt`**: A CSV file updated every second containing the rolling window metrics.


* *Columns*: Seconds, Nanoseconds, Commit_Count, Identity_Count, Account_Count, Info_Count, Buffer_Occupancy_Pct, CPU_Pct.




* **`logs/status_log.txt`**: A thread-safe diagnostic log for tracking connection events, thread initialization failures, and watchdog reconnects.



The application is configured to run for 48 hours by default before exiting cleanly, automatically destroying the queue and joining all threads.

## 24-Hour Scheduled Logging via Systemd

You can run this program on a strict 24-hour schedule by creating two systemd services: one for starting the logger and one for terminating it. By pairing these services with two systemd timers set exactly 24 hours apart, you can create a perfectly aligned daily logging time window.

### 1. Start Service and Timer

Create the start service to launch the application.

**/etc/systemd/system/bluesky-logger.service**

```ini
[Unit]
Description=Bluesky Jetstream Telemetry Logger

[Service]
Type=simple
WorkingDirectory=/path/to/your/project
ExecStart=/path/to/your/project/main

```

Create the timer to trigger the start service (e.g., every day at 00:00).

**/etc/systemd/system/bluesky-logger.timer**

```ini
[Unit]
Description=Timer to start the Bluesky Logger daily

[Timer]
OnCalendar=*-*-* 00:00:00
Persistent=true

[Install]
WantedBy=timers.target

```

### 2. Stop Service and Timer

Create the stop service to gracefully terminate the running application.

**/etc/systemd/system/bluesky-logger-stop.service**

```ini
[Unit]
Description=Terminate Bluesky Jetstream Telemetry Logger

[Service]
Type=oneshot
ExecStart=/usr/bin/systemctl stop bluesky-logger.service

```

Create the timer to trigger the termination right before the next run (e.g., 23:59:59).

**/etc/systemd/system/bluesky-logger-stop.timer**

```ini
[Unit]
Description=Timer to stop the Bluesky Logger

[Timer]
OnCalendar=*-*-* 23:59:59
Persistent=true

[Install]
WantedBy=timers.target

```

Once the files are created, reload the systemd daemon and enable the timers:

```bash
sudo systemctl daemon-reload
sudo systemctl enable --now bluesky-logger.timer
sudo systemctl enable --now bluesky-logger-stop.timer

```
