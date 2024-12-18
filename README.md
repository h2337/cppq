<p align="center">
  <img src="https://github.com/jafarlihi/file-hosting/blob/87315e66c91576a8098cd43fc169c52cad4d44bf/cppq.png">
</p>

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
- [x] Weighted priority queues
- [x] Scheduling of tasks
- [ ] Periodic tasks
- [ ] Timeout and deadline per task
- [ ] Ability to pause queue to stop processing tasks from the queue
- [x] Web UI to inspect and control queues and tasks
- [ ] CLI to inspect and control queues and tasks

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

  // This call will loop forever checking the pending queue and processing tasks in the thread pool.
  // Second argument defines queues and their priorities.
  // Third argument is time in seconds that task can be alive in active queue
  // before being pushed back to pending queue (i.e. when worker dies in middle of execution).
  cppq::runServer(redisOpts, {{"low", 5}, {"default", 10}, {"high", 20}}, 1000);
}
```

## Web UI

If you are on Linux then web UI can be started by running: `cd web && ./start.sh`

Web UI is made with React/TypeScript and Flask/Python. It is still work-in-progress.

![Web UI demo](https://github.com/jafarlihi/file-hosting/blob/1bb913da5701a8ec13ba541bbfd3b413910f19d8/cppq-web-ui.gif?raw=true)

## License

cppq is MIT-licensed.

Thread pooling functionality is retrofitted from https://github.com/bshoshany/thread-pool
