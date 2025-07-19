#include "cppq.hpp"

const std::string TypeEmailDelivery = "email:deliver";

typedef struct {
  int UserID;
  std::string TemplateID;
} EmailDeliveryPayload;

void to_json(nlohmann::json& j, const EmailDeliveryPayload& p) {
  j = nlohmann::json{{"UserID", p.UserID}, {"TemplateID", p.TemplateID}};
}

cppq::Task NewEmailDeliveryTask(EmailDeliveryPayload payload) {
  nlohmann::json j = payload;
  // "10" is maxRetry -- the number of times the task will be retried on exception
  return cppq::Task{TypeEmailDelivery, j, 10};
}

void HandleEmailDeliveryTask(cppq::Task& task) {
  // Fetch the parameters
  int userID = task.payload["UserID"];
  std::string templateID = task.payload["TemplateID"];

  // Send the email...

  // Return a result
  nlohmann::json r;
  r["Sent"] = true;
  task.result = r;
  return;
}

int main(int argc, char *argv[]) {
  // Register task types and handlers
  cppq::registerHandler(TypeEmailDelivery, &HandleEmailDeliveryTask);

  // Create a Redis connection for enqueuing, you can reuse this for subsequent enqueues
  redisOptions options = {0};
  REDIS_OPTIONS_SET_TCP(&options, "127.0.0.1", 6379);
  redisContext *c = redisConnectWithOptions(&options);
  if (c == NULL || c->err) {
    std::cerr << "Failed to connect to Redis" << std::endl;
    return 1;
  }

  // Create a task
  cppq::Task task = NewEmailDeliveryTask(EmailDeliveryPayload{.UserID = 666, .TemplateID = "AH"});

  // Enqueue the task
  cppq::enqueue(c, task);

  // Second argument is time in seconds that task can be alive in active queue before being pushed back to pending queue (i.e. when worker dies in middle of execution)
  // This call will not return
  // It will loop forever checking the pending queue and processing tasks in thread pool
  cppq::runServer(options, 1000000);
}
