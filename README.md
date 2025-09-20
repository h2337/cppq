<img align="right" src="https://raw.githubusercontent.com/h2337/cppq/refs/heads/master/logo.svg">

# cppq

Distributed, Redis-backed task queue for modern C++17 applications with worker orchestration, scheduling, and tooling built-in.

## Table of Contents
- [Overview](#overview)
- [Features](#features)
- [Requirements](#requirements)
- [Quickstart](#quickstart)
- [Example](#example)
- [Running the Example](#running-the-example)
- [Testing](#testing)
- [CLI](#cli)
- [Web UI](#web-ui)
- [Project Layout](#project-layout)
- [License](#license)

## Overview
cppq is a header-only task queue that lets you enqueue work from C++ and execute it asynchronously via Redis. Workers pull jobs, run user supplied handlers, and update task state in Redis. The library focuses on predictable behaviour and ease of adoption—drop the header in your project, link a couple of dependencies, and you have a resilient queue.

### Architecture at a Glance
- Producers enqueue `Task` objects into Redis lists and hashes.
- `runServer` polls queues by priority, hands tasks to a thread pool, and dispatches registered handlers.
- Recovery logic returns stuck tasks to the pending list after configurable timeouts.
- Auxiliary tooling (CLI, web dashboard) surfaces queue state and provides pause / resume controls.

## Features
- At-least-once delivery with retry tracking and task recovery.
- Scheduled execution (time-based today, cron support planned).
- Queue-level priorities and pause / unpause switches.
- Header-only API; only Redis, libuuid, and hiredis are required.
- Companion CLI and web dashboard for operations teams.
- Extensive Catch2 test suite covering queue flows and thread pool behaviour.

## Requirements
### Runtime
- Redis 6.0+ available on `redis://127.0.0.1:6379` (configurable).

### C++ Dependencies
- C++17-capable compiler (`g++`, `clang++`).
- [`hiredis`](https://github.com/redis/hiredis) client library.
- `libuuid` for UUID generation.

Install dependencies with your package manager, e.g.
```bash
# Debian / Ubuntu
sudo apt install g++ libhiredis-dev uuid-dev

# Arch Linux
sudo pacman -S hiredis util-linux-libs

# macOS (Homebrew)
brew install hiredis ossp-uuid
```

## Quickstart
1. Copy `cppq.hpp` into your include path.
2. Include the header and link against hiredis, uuid, and pthread on POSIX systems:
   ```bash
   g++ -std=c++17 your_app.cpp -I/path/to/cppq -lhiredis -luuid -lpthread
   ```
3. Register task handlers before calling `runServer`.
4. Start Redis and execute your application.

## Example
```cpp
#include "cppq.hpp"

#include <nlohmann/json.hpp>

// Specify task type name
const std::string TypeEmailDelivery = "email:deliver";

// Define a payload type for your task
typedef struct {
  int UserID;
  std::string TemplateID;
} EmailDeliveryPayload;

// Provide conversion to JSON (optional, you can use any kind of payload)
void to_json(nlohmann::json& j, const EmailDeliveryPayload& p) {
  j = nlohmann::json{{"UserID", p.UserID}, {"TemplateID", p.TemplateID}};
}

// Helper function to create a new task with the given payload
cppq::Task NewEmailDeliveryTask(EmailDeliveryPayload payload) {
  nlohmann::json j = payload;
  // "10" is maxRetry -- the number of times the task will be retried on exception
  return cppq::Task{TypeEmailDelivery, j.dump(), 10};
}

// The actual task code
void HandleEmailDeliveryTask(cppq::Task& task) {
  // Fetch the parameters
  nlohmann::json parsedPayload = nlohmann::json::parse(task.payload);
  int userID = parsedPayload["UserID"];
  std::string templateID = parsedPayload["TemplateID"];

  // Send the email...

  // Return a result
  nlohmann::json r;
  r["Sent"] = true;
  task.result = r.dump();
}

int main(int argc, char* argv[]) {
  // Register task types and handlers
  cppq::registerHandler(TypeEmailDelivery, &HandleEmailDeliveryTask);

  // Create a Redis connection for enqueuing, you can reuse this for subsequent enqueues
  redisOptions redisOpts = {0};
  REDIS_OPTIONS_SET_TCP(&redisOpts, "127.0.0.1", 6379);
  redisContext* c = redisConnectWithOptions(&redisOpts);
  if (c == nullptr || c->err) {
    std::cerr << "Failed to connect to Redis" << std::endl;
    return 1;
  }

  // Create tasks
  cppq::Task task = NewEmailDeliveryTask(EmailDeliveryPayload{.UserID = 666, .TemplateID = "AH"});
  cppq::Task task2 = NewEmailDeliveryTask(EmailDeliveryPayload{.UserID = 606, .TemplateID = "BH"});
  cppq::Task task3 = NewEmailDeliveryTask(EmailDeliveryPayload{.UserID = 666, .TemplateID = "CH"});

  // Enqueue a task on default queue
  cppq::enqueue(c, task, "default");
  // Enqueue a task on high priority queue
  cppq::enqueue(c, task2, "high");
  // Enqueue a task on default queue to be run at exactly 1 minute from now
  cppq::enqueue(
      c,
      task3,
      "default",
      cppq::scheduleOptions(std::chrono::system_clock::now() + std::chrono::minutes(1)));

  // Pause queue to stop processing tasks from it
  cppq::pause(c, "default");
  // Unpause queue to continue processing tasks from it
  cppq::unpause(c, "default");

  // This call will loop forever checking the pending queue
  // before being pushed back to pending queue (i.e. when worker dies in middle of execution).
  cppq::runServer(redisOpts, {{"low", 5}, {"default", 10}, {"high", 20}}, 1000);
}
```

## Running the Example
```bash
g++ -std=c++17 example.cpp -I. -lhiredis -luuid -lpthread -o example
./example
```
Ensure Redis is running locally before executing the binary.

## Testing
The repository ships with Catch2 tests that exercise queue operations, scheduling, recovery, pause logic, and the internal thread pool.

1. Install dependencies (besides hiredis/uuid you also need Catch2 headers and nlohmann-json).
2. Start a local Redis instance: `redis-server --port 6379`.
3. Build the tests:
   ```bash
   ./build_tests.sh
   ```
4. Run them:
   ```bash
   ./tests
   ```

The script emits useful tags such as `[queue]`, `[recovery]`, or `[threadpool]` that you can pass to Catch2 to focus on specific areas.

## CLI
The `cli/` directory contains a Python-based management tool built with Click, Rich, and Redis-py.

```bash
cd cli
pip install -r requirements.txt
python3 main.py --help
python3 main.py queues --format json
```

Key capabilities:
- Inspect queues, task states, and statistics.
- Pause or resume queues.
- Dump task metadata for debugging.
- Load configuration from environment variables, CLI flags, or `~/.config/cppq/config.json`.

For deeper usage details see [`cli/README.md`](cli/README.md).

## Web UI
A Next.js dashboard lives in `web/`. It offers real-time monitoring, task inspection, and queue controls via a modern interface.

```bash
cd web
npm install
npm run dev
```
Visit http://localhost:3000/ and connect to your Redis instance (default `redis://localhost:6379`). Read the dedicated [web/README.md](web/README.md) for screenshots, API routes, and deployment hints.

## Project Layout
```
.
├── cppq.hpp          # Header-only queue library
├── example.cpp       # Minimal producer/worker demonstration
├── tests.cpp         # Catch2 regression suite
├── build_tests.sh    # Helper script for building tests
├── cli/              # Python CLI for operations
└── web/              # Next.js dashboard
```

## License
cppq is released under the MIT License. The bundled thread pool implementation is adapted from https://github.com/bshoshany/thread-pool.
