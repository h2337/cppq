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
  return std::make_shared<cppq::Task>(TypeEmailDelivery, j, 10);
}

std::shared_ptr<cppq::Task> NewImageResizeTask(ImageResizePayload payload) {
  nlohmann::json j = payload;
  return std::make_shared<cppq::Task>(TypeImageResize, j, 10);
}

void HandleEmailDeliveryTask(std::shared_ptr<cppq::Task> task) {
  int userID = task->payload["UserID"];
  std::string templateID = task->payload["TemplateID"];
  nlohmann::json r;
  r["UserIDMultiplied"] = userID * 2;
  r["TemplateID"] = templateID;
  task->result = r;
  return;
}

void HandleImageResizeTask(std::shared_ptr<cppq::Task> task) {
  return;
}

int main(int argc, char *argv[]) {
  cppq::registerHandler(TypeEmailDelivery, &HandleEmailDeliveryTask);
  cppq::registerHandler(TypeImageResize, &HandleImageResizeTask);

  redisOptions options = {0};
  REDIS_OPTIONS_SET_TCP(&options, "127.0.0.1", 6379);

  redisContext *c = redisConnectWithOptions(&options);
  if (c == NULL || c->err) {
    std::cerr << "Failed to connect to Redis" << std::endl;
    return 1;
  }

  std::shared_ptr<cppq::Task> task = NewEmailDeliveryTask(EmailDeliveryPayload{.UserID = 666, .TemplateID = "AH"});
  cppq::enqueue(c, task);

  cppq::runServer(options, 1000000);
}
