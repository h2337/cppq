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
  int userID = task->payload["UserID"];
  std::string templateID = task->payload["TemplateID"];
  nlohmann::json r;
  r["UserIDMultiplied"] = userID * 2;
  r["Sent"] = true;
  task->result = r;
  return;
}

void HandleImageResizeTask(std::shared_ptr<cppq::Task> task) {
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

  // Check task result
  while (!task->result.contains("Sent")) {
    std::cout << task->result << std::endl;
  }
}
