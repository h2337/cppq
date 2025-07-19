#include "cppq.hpp"

#include <nlohmann/json.hpp>

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
  return cppq::Task{TypeEmailDelivery, j.dump(), 10};
}

void HandleEmailDeliveryTask(cppq::Task& task) {
  nlohmann::json parsedPayload = nlohmann::json::parse(task.payload);
  int userID = parsedPayload["UserID"];
  std::string templateID = parsedPayload["TemplateID"];

  nlohmann::json r;
  r["Sent"] = true;
  task.result = r.dump();
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
  cppq::Task task2 = NewEmailDeliveryTask(EmailDeliveryPayload{.UserID = 606, .TemplateID = "BH"});
  cppq::Task task3 = NewEmailDeliveryTask(EmailDeliveryPayload{.UserID = 666, .TemplateID = "CH"});

  cppq::enqueue(c, task, "default");
  cppq::enqueue(c, task2, "high");
  cppq::enqueue(c, task3, "default", cppq::scheduleOptions(std::chrono::system_clock::now() + std::chrono::minutes(1)));

  cppq::pause(c, "default");
  cppq::unpause(c, "default");

  cppq::runServer(redisOpts, {{"low", 5}, {"default", 10}, {"high", 20}}, 1000);
}
