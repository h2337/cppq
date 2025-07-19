#include <string>
#include <cstdint>
#include <nlohmann/json.hpp>
#include <hiredis/hiredis.h>
#include <uuid/uuid.h>
#include <chrono>
#include <thread>
#include "BS_thread_pool.hpp"

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

enum class TaskState {
  Unknown,
  Pending,
  Active,
  Failed,
  Completed
};

std::string stateToString(TaskState state) {
  switch (state) {
    case TaskState::Unknown: return "Unknown";
    case TaskState::Pending: return "Pending";
    case TaskState::Active: return "Active";
    case TaskState::Failed: return "Failed";
    case TaskState::Completed: return "Completed";
  }
  return "Unknown";
}

TaskState stringToState(std::string state) {
  if (state.compare("Unknown") == 0) return TaskState::Unknown;
  if (state.compare("Pending") == 0) return TaskState::Pending;
  if (state.compare("Active") == 0) return TaskState::Active;
  if (state.compare("Failed") == 0) return TaskState::Failed;
  if (state.compare("Completed") == 0) return TaskState::Completed;
  return TaskState::Unknown;
}

std::string uuidToString(uuid_t uuid) {
  char uuid_str[37];
  uuid_unparse_lower(uuid, uuid_str);
  return uuid_str;
}

class Task {
  public:
    Task(std::string type, nlohmann::json payload, uint64_t maxRetry) {
      uuid_generate(this->uuid);
      this->type = type;
      this->payload = payload;
      this->state = TaskState::Unknown;
      this->maxRetry = maxRetry;
      this->retried = 0;
      this->dequeuedAtMs = 0;
    }
    Task(std::string uuid, std::string type, std::string payload, std::string state, uint64_t maxRetry, uint64_t retried, uint64_t dequeuedAtMs) {
      uuid_t uuid_parsed;
      uuid_parse(uuid.c_str(), uuid_parsed);
      uuid_copy(this->uuid, uuid_parsed);
      this->type = type;
      this->payload = nlohmann::json::parse(payload);
      this->maxRetry = maxRetry;
      this->retried = retried;
      this->dequeuedAtMs = dequeuedAtMs;
      this->state = stringToState(state);
    }
    uuid_t uuid;
    std::string type;
    nlohmann::json payload;
    TaskState state;
    uint64_t maxRetry;
    uint64_t retried;
    uint64_t dequeuedAtMs;
    nlohmann::json result;
};

std::shared_ptr<Task> NewEmailDeliveryTask(EmailDeliveryPayload payload) {
  nlohmann::json j = payload;
  return std::make_shared<Task>(TypeEmailDelivery, j, 10);
}

std::shared_ptr<Task> NewImageResizeTask(ImageResizePayload payload) {
  nlohmann::json j = payload;
  return std::make_shared<Task>(TypeImageResize, j, 10);
}

void HandleEmailDeliveryTask(std::shared_ptr<Task> task) {
  int userID = task->payload["UserID"];
  std::string templateID = task->payload["TemplateID"];
  nlohmann::json r;
  r["UserIDMultiplied"] = userID * 2;
  r["TemplateID"] = templateID;
  task->result = r;
  return;
}

void HandleImageResizeTask(std::shared_ptr<Task> task) {
  return;
}

using Handler = void (*)(std::shared_ptr<Task>);
auto handlers = std::unordered_map<std::string, Handler>();

void enqueue(redisContext *c, std::shared_ptr<Task> task) {
  task->state = TaskState::Pending;

  redisCommand(c, "MULTI");
  redisCommand(c, "LPUSH cppq:pending %s", uuidToString(task->uuid).c_str());
  redisCommand(c, "HSET cppq:task:%s type %s payload %s state %s maxRetry %d retried %d dequeuedAtMs %d", uuidToString(task->uuid).c_str(), task->type.c_str(), task->payload.dump().c_str(), stateToString(task->state).c_str(), task->maxRetry, task->retried, task->dequeuedAtMs);
  redisReply *reply = (redisReply *)redisCommand(c, "EXEC");

  if (reply->type == REDIS_REPLY_ERROR)
    throw std::runtime_error("Failed to enqueue task");
}

std::shared_ptr<Task> dequeue(redisContext *c) {
  redisReply *reply = (redisReply *)redisCommand(c, "LRANGE cppq:pending -1 -1");
  if (reply->type != REDIS_REPLY_ARRAY)
    return nullptr;
  if (reply->elements == 0)
    return nullptr;
  reply = reply->element[0];
  std::string uuid = reply->str;

  uint64_t dequeuedAtMs = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();

  redisCommand(c, "MULTI");
  redisCommand(c, "LREM cppq:pending 1 %s", uuid.c_str());
  redisCommand(c, "HGET cppq:task:%s type", uuid.c_str());
  redisCommand(c, "HGET cppq:task:%s payload", uuid.c_str());
  redisCommand(c, "HGET cppq:task:%s state", uuid.c_str());
  redisCommand(c, "HGET cppq:task:%s maxRetry", uuid.c_str());
  redisCommand(c, "HGET cppq:task:%s retried", uuid.c_str());
  redisCommand(c, "HGET cppq:task:%s dequeuedAtMs", uuid.c_str());
  redisCommand(c, "HSET cppq:task:%s dequeuedAtMs %lu", uuid.c_str(), dequeuedAtMs);
  redisCommand(c, "LPUSH cppq:active %s", uuid.c_str());
  reply = (redisReply *)redisCommand(c, "EXEC");

  if (reply->type != REDIS_REPLY_ARRAY || reply->elements != 9)
    return nullptr;

  std::shared_ptr<Task> task = std::make_shared<Task>(uuid, reply->element[1]->str, reply->element[2]->str, reply->element[3]->str, strtoull(reply->element[4]->str, NULL, 0), strtoull(reply->element[5]->str, NULL, 0), dequeuedAtMs);

  assert(task->state == TaskState::Pending);

  return task;
}

void taskRunner(redisOptions options, std::shared_ptr<Task> task) {
  redisContext *c = redisConnectWithOptions(&options);
  if (c == NULL || c->err) {
    std::cerr << "Faile to connect to Redis" << std::endl;
    return;
  }

  Handler handler = handlers[task->type];

  try {
    handler(task);
  } catch(const std::exception &e) {
    task->retried++;
    redisCommand(c, "MULTI");
    redisCommand(c, "LREM cppq:active 1 %s", uuidToString(task->uuid).c_str());
    redisCommand(c, "HSET cppq:task:%s retried %d", uuidToString(task->uuid).c_str(), task->retried);
    if (task->retried >= task->maxRetry) {
      task->state = TaskState::Failed;
      redisCommand(c, "HSET cppq:task:%s state %s", uuidToString(task->uuid).c_str(), stateToString(task->state).c_str());
      redisCommand(c, "LPUSH cppq:failed %s", uuidToString(task->uuid).c_str());
    } else {
      task->state = TaskState::Pending;
      redisCommand(c, "HSET cppq:task:%s state %s", uuidToString(task->uuid).c_str(), stateToString(task->state).c_str());
      redisCommand(c, "LPUSH cppq:pending %s", uuidToString(task->uuid).c_str());
    }
    redisCommand(c, "EXEC");
    redisFree(c);
    return;
  }

  task->state = TaskState::Completed;
  redisCommand(c, "MULTI");
  redisCommand(c, "LREM cppq:active 1 %s", uuidToString(task->uuid).c_str());
  redisCommand(c, "HSET cppq:task:%s state %s", uuidToString(task->uuid).c_str(), stateToString(task->state).c_str());
  redisCommand(c, "HSET cppq:task:%s result %s", uuidToString(task->uuid).c_str(), task->result.dump().c_str());
  redisCommand(c, "LPUSH cppq:completed %s", uuidToString(task->uuid).c_str());
  redisCommand(c, "EXEC");
  redisFree(c);
}

void runServer(redisOptions options) {
  redisContext *c = redisConnectWithOptions(&options);
  if (c == NULL || c->err) {
    std::cerr << "Faile to connect to Redis" << std::endl;
    return;
  }

  BS::thread_pool pool;

  while (true) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    std::shared_ptr<Task> task = dequeue(c);
    if (task != nullptr)
      pool.push_task(taskRunner, options, task);
  }
}

void recovery(redisOptions options, uint64_t timeoutMs) {
  redisContext *c = redisConnectWithOptions(&options);
  if (c == NULL || c->err) {
    std::cerr << "Faile to connect to Redis" << std::endl;
    return;
  }

  while (true) {
    std::this_thread::sleep_for(std::chrono::milliseconds(10000));
    redisReply *reply = (redisReply *)redisCommand(c, "LRANGE cppq:active 0 -1");
    for (int i = 0; i < reply->elements; i++) {
      // if dequeuedAtMs + timeoutMs < now then put to pending queue
    }
  }
}

int main(int argc, char *argv[]) {
  handlers[TypeEmailDelivery] = &HandleEmailDeliveryTask;
  handlers[TypeImageResize] = &HandleImageResizeTask;

  redisOptions options = {0};
  REDIS_OPTIONS_SET_TCP(&options, "127.0.0.1", 6379);

  redisContext *c = redisConnectWithOptions(&options);
  if (c == NULL || c->err) {
    std::cerr << "Faile to connect to Redis" << std::endl;
    return 1;
  }

  std::shared_ptr<Task> task = NewEmailDeliveryTask(EmailDeliveryPayload{.UserID = 666, .TemplateID = "AH"});
  enqueue(c, task);

  runServer(options);
}
