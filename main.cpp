#include <string>
#include <cstdint>
#include <nlohmann/json.hpp>

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

class Task {
  public:
    Task(std::string type, nlohmann::json payload) {
      this->type = type;
      this->payload = payload;
    }
  private:
    std::string type;
    nlohmann::json payload;
};

std::unique_ptr<Task> NewEmailDeliveryTask(EmailDeliveryPayload payload) {
  nlohmann::json j = payload;
  return std::make_unique<Task>(TypeEmailDelivery, j);
}

std::unique_ptr<Task> NewImageResizeTask(ImageResizePayload payload) {
  nlohmann::json j = payload;
  return std::make_unique<Task>(TypeImageResize, j);
}

int main(int argc, char *argv[]) {

}
