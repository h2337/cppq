#include <string>
#include <cstdint>
#include <nlohmann/json.hpp>
#include <hiredis/hiredis.h>
#include <uuid/uuid.h>

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
}

std::string uuidToString(uuid_t uuid) {
  char uuid_str[37];
  uuid_unparse_lower(uuid, uuid_str);
  return uuid_str;
}

class Task {
  public:
    Task(std::string type, nlohmann::json payload, uint64_t maxRetry, uint64_t timeoutMs) {
      uuid_generate(this->uuid);
      this->type = type;
      this->payload = payload;
      this->maxRetry = maxRetry;
      this->timeoutMs = timeoutMs;
      this->retried = 0;
      this->dequeuedAtMs = 0;
      this->state = TaskState::Unknown;
    }
    uuid_t uuid;
    std::string type;
    nlohmann::json payload;
    TaskState state;
    uint64_t maxRetry;
    uint64_t timeoutMs;
    uint64_t retried;
    uint64_t dequeuedAtMs;
};

std::shared_ptr<Task> NewEmailDeliveryTask(EmailDeliveryPayload payload) {
  nlohmann::json j = payload;
  return std::make_shared<Task>(TypeEmailDelivery, j, 10, 100000);
}

std::shared_ptr<Task> NewImageResizeTask(ImageResizePayload payload) {
  nlohmann::json j = payload;
  return std::make_shared<Task>(TypeImageResize, j, 10, 100000);
}

void HandleEmailDeliveryTask(Task *task) {
  return;
}

void HandleImageResizeTask(Task *task) {
  return;
}

using Handler = void (*)(Task *);
auto handlers = std::unordered_map<std::string, Handler>();

void enqueue(redisContext *c, std::shared_ptr<Task> task) {
  task->state = TaskState::Pending;

  redisCommand(c, "MULTI");
  redisCommand(c, "LPUSH cppq:pending %s", uuidToString(task->uuid).c_str());
  redisCommand(c, "HSET cppq:task:%s type %s payload %s state %s maxRetry %d timeoutMs %d retried %d dequeuedAtMs %d", uuidToString(task->uuid).c_str(), task->type.c_str(), task->payload.dump().c_str(), stateToString(task->state).c_str(), task->maxRetry, task->timeoutMs, task->retried, task->dequeuedAtMs);
  redisReply *reply = (redisReply *)redisCommand(c, "EXEC");

  if (reply->type == REDIS_REPLY_ERROR)
    throw std::runtime_error("Failed to enqueue task");
}

int main(int argc, char *argv[]) {
  handlers[TypeEmailDelivery] = &HandleEmailDeliveryTask;
  handlers[TypeImageResize] = &HandleImageResizeTask;

  redisContext *c = redisConnect("127.0.0.1", 6379);
  if (c == NULL || c->err) {
    if (c) {
      printf("Error: %s\n", c->errstr);
      return 1;
    } else {
      printf("Can't allocate redis context\n");
      return 1;
    }
  }

  std::shared_ptr<Task> task = NewEmailDeliveryTask(EmailDeliveryPayload{.UserID = 666, .TemplateID = "AH"});
  enqueue(c, task);
}
