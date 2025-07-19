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
  return cppq::Task{TypeEmailDelivery, j, 10};
}

void HandleEmailDeliveryTask(cppq::Task& task) {
  int userID = task.payload["UserID"];
  std::string templateID = task.payload["TemplateID"];

  nlohmann::json r;
  r["Sent"] = true;
  task.result = r;
  return;
}

int main(int argc, char *argv[]) {
  cppq::registerHandler(TypeEmailDelivery, &HandleEmailDeliveryTask);

  redisOptions redisOpts = {0};
  REDIS_OPTIONS_SET_TCP(&redisOpts, "127.0.0.1", 6379);
  redisContext *c = redisConnectWithOptions(&redisOpts);
  if (c == NULL || c->err) {
    std::cerr << "Failed to connect to Redis" << std::endl;
    return 1;
  }

  cppq::Task task = NewEmailDeliveryTask(EmailDeliveryPayload{.UserID = 666, .TemplateID = "AH"});

  cppq::enqueue(c, task);

  cppq::runServer(redisOpts, 1000);
}
