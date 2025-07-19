# cppq

**Warning**
cppq is still alpha and stability is not guaranteed.

cppq is a simple, reliable & efficient distributed task queue for C++17.

cppq is a C++ library for queueing tasks and processing them asynchronously with workers. It's backed by Redis and is designed to be scalable yet easy to get started.

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
- [ ] Scheduling of tasks
- [ ] Weighted priority queues
- [ ] Allow timeout and deadline per task
- [ ] Ability to pause queue to stop processing tasks from the queue
- [ ] Periodic tasks
- [ ] Web UI to inspect and remote-control queues and tasks
- [ ] CLI to inspect and remote-control queues and tasks

## Quickstart

cppq is a header-only library with 3 dependencies: `libuuid`, `hiredis`, `nlohmann_json`.

Just include the header: `#include "cppq.h"` and add these flags to your build `-luuid -lhiredis`.

`libuuid`, `hiredis` and `nlohmann_json` can be installed using your distro's package manager.

For Arch Linux that'd be: `sudo pacman -S hiredis util-linux-libs nlohmann-json`

## Example

```c++
#include "cppq.hpp"

const std::string TypeEmailDelivery = "email:deliver";
const std::string TypeImageResize = "image:resize";

typedef struct {
  int UserID;
  std::string TemplateID;
} EmailDeliveryPayload;

typedef struct {
  std::string SourceURL;
} ImageResizePayload;

void to_json(nlohmann::json& j, const EmailDeliveryPayload& p) {
  j = nlohmann::json{{"UserID", p.UserID}, {"TemplateID", p.TemplateID}};
}

void to_json(nlohmann::json& j, const ImageResizePayload& p) {
  j = nlohmann::json{{"SourceURL", p.SourceURL}};
}

std::shared_ptr<cppq::Task> NewEmailDeliveryTask(EmailDeliveryPayload payload) {
  nlohmann::json j = payload;
  // "10" is maxRetry -- the number of times the task will be retried on exception
  return std::make_shared<cppq::Task>(TypeEmailDelivery, j, 10);
}

std::shared_ptr<cppq::Task> NewImageResizeTask(ImageResizePayload payload) {
  nlohmann::json j = payload;
  // "10" is maxRetry -- the number of times the task will be retried on exception
  return std::make_shared<cppq::Task>(TypeImageResize, j, 10);
}

void HandleEmailDeliveryTask(std::shared_ptr<cppq::Task> task) {
  // Fetch the parameters
  int userID = task->payload["UserID"];
  std::string templateID = task->payload["TemplateID"];

  // Send the email...

  // Return a result
  nlohmann::json r;
  r["UserIDMultiplied"] = userID * 2;
  r["Sent"] = true;
  task->result = r;
  return;
}

void HandleImageResizeTask(std::shared_ptr<cppq::Task> task) {
  // Handle the task here...
  return;
}

int main(int argc, char *argv[]) {
  // Register task types and handlers
  cppq::registerHandler(TypeEmailDelivery, &HandleEmailDeliveryTask);
  cppq::registerHandler(TypeImageResize, &HandleImageResizeTask);

  // Create a Redis connection for enquing, you can reuse this for subsequent enqueues
  redisOptions options = {0};
  REDIS_OPTIONS_SET_TCP(&options, "127.0.0.1", 6379);
  redisContext *c = redisConnectWithOptions(&options);
  if (c == NULL || c->err) {
    std::cerr << "Failed to connect to Redis" << std::endl;
    return 1;
  }

  // Create a task
  std::shared_ptr<cppq::Task> task = NewEmailDeliveryTask(EmailDeliveryPayload{.UserID = 666, .TemplateID = "AH"});

  // Enqueue the task
  cppq::enqueue(c, task);

  // Second argument is time in millisecond that task can be alive in active queue before being pushed back to pending queue
  // This call will not return
  // It will loop forever checking the pending queue and processing tasks in thread pool
  cppq::runServer(options, 1000000);
}
```

## License

cppq is MIT-licensed.

Thread pooling functionality is retrofitted from https://github.com/bshoshany/thread-pool
