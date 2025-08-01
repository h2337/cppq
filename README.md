<img align="right" src="https://raw.githubusercontent.com/h2337/cppq/refs/heads/master/logo.svg">

## TOC

* [Overview](#overview)
* [Features](#features)
* [Quickstart](#quickstart)
* [Example](#example)
* [Web UI](#web-ui)
* [CLI](#cli)
* [License](#license)

## Overview

cppq is a simple, reliable & efficient distributed task queue for C++17.

cppq is a C++ library for queueing tasks and processing them asynchronously with workers. It's backed by Redis and is designed to be scalable and easy to get started with.

Highlevel overview of how cppq works:

- Client puts tasks on a queue
- Server pulls tasks off queues and starts a thread for each task
- Tasks are processed concurrently by multiple workers

Task queues are used as a mechanism to distribute work across multiple machines. A system can consist of multiple worker servers and brokers, giving way to high availability and horizontal scaling.

## Features
- [x] Guaranteed at least one execution of a task
- [x] Retries of failed tasks
- [x] Automatic recovery of tasks in the event of a worker crash
- [x] Low latency to add a task since writes are fast in Redis
- [x] Queue priorities
- [x] Scheduling of tasks
- [ ] Periodic tasks
- [x] Ability to pause queue to stop processing tasks from the queue
- [x] Web UI to inspect and control queues and tasks
- [x] CLI to inspect and control queues and tasks

## Quickstart

cppq is a header-only library with 2 dependencies: `libuuid` and `hiredis`.

Just include the header: `#include "cppq.h"` and add these flags to your build `-luuid -lhiredis`.

`libuuid` and `hiredis` can be installed using your distro's package manager.

For Arch Linux that'd be: `sudo pacman -S hiredis util-linux-libs`

## Example

```c++
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
  return;
}

int main(int argc, char *argv[]) {
  // Register task types and handlers
  cppq::registerHandler(TypeEmailDelivery, &HandleEmailDeliveryTask);

  // Create a Redis connection for enqueuing, you can reuse this for subsequent enqueues
  redisOptions redisOpts = {0};
  REDIS_OPTIONS_SET_TCP(&redisOpts, "127.0.0.1", 6379);
  redisContext *c = redisConnectWithOptions(&redisOpts);
  if (c == NULL || c->err) {
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
    cppq::scheduleOptions(std::chrono::system_clock::now() + std::chrono::minutes(1))
  );

  // Pause queue to stop processing tasks from it
  cppq::pause(c, "default");
  // Unpause queue to continue processing tasks from it
  cppq::unpause(c, "default");

  // This call will loop forever checking the pending queue
  // and processing tasks in the thread pool.
  // Second argument defines queues and their priorities.
  // Third argument is time in seconds that task can be alive in active queue
  // before being pushed back to pending queue (i.e. when worker dies in middle of execution).
  cppq::runServer(redisOpts, {{"low", 5}, {"default", 10}, {"high", 20}}, 1000);
}
```

## Web UI

The web UI provides a modern dashboard to monitor and control your cppq queues and tasks.

### Features
- Real-time queue monitoring with auto-refresh
- Queue statistics and performance metrics visualization
- Task inspection by state (pending, scheduled, active, completed, failed)
- Queue pause/unpause functionality
- Task search and filtering
- Export queue and task data to CSV
- Dark mode support
- Responsive design

### Running the Web UI

1. Navigate to the web directory:
   ```bash
   cd web
   ```

2. Install dependencies:
   ```bash
   npm install
   ```

3. Start the development server:
   ```bash
   npm run dev
   ```

4. Open http://localhost:3000 in your browser

5. Connect to your Redis instance (default: `redis://localhost:6379`)

### Technology Stack
- **Frontend**: Next.js 15, React 19, TypeScript
- **Styling**: Tailwind CSS v4
- **Backend**: Next.js API routes
- **Database**: Redis (via node-redis)

For detailed documentation, see [web/README.md](web/README.md).

## CLI

A modern, feature-rich command-line interface for managing cppq queues and tasks.

### Features
- Modern CLI framework with intuitive commands
- Rich formatted output with color support
- Multiple output formats (table, JSON, pretty-print)
- Configuration file and environment variable support
- Comprehensive error handling and logging
- Type-safe with full type hints

### Quick Start

1. Navigate to the CLI directory:
   ```bash
   cd cli
   ```

2. Install dependencies:
   ```bash
   pip3 install -r requirements.txt
   ```

3. Run the CLI:
   ```bash
   python3 main.py --help
   ```

### Usage Examples

```bash
# List all queues with colored status
python3 main.py queues

# Get queue statistics
python3 main.py stats myqueue

# List tasks in different states
python3 main.py list myqueue pending
python3 main.py list myqueue active --limit 10

# Get task details
python3 main.py task myqueue 123e4567-e89b-12d3-a456-426614174000

# Pause/unpause queues
python3 main.py pause myqueue
python3 main.py unpause myqueue

# Different output formats
python3 main.py queues --format json
python3 main.py stats myqueue --format table

# Enable debug logging
python3 main.py --debug queues

# Use custom Redis URI
python3 main.py --redis-uri redis://myserver:6379 queues
```

### Configuration

The CLI supports configuration through:
- Command-line arguments (highest priority)
- Environment variables (e.g., `REDIS_URI`, `CPPQ_OUTPUT_FORMAT`)
- Configuration file (`~/.config/cppq/config.json`)
- Default values

Create a configuration file:
```bash
python3 main.py config --create
```

For detailed documentation, see [cli/README.md](cli/README.md).

## License

cppq is MIT-licensed.

Thread pooling functionality is retrofitted from https://github.com/bshoshany/thread-pool
