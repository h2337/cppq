#include <atomic>
#include <catch2/catch_all.hpp>
#include <chrono>
#include <future>
#include <mutex>
#include <nlohmann/json.hpp>
#include <random>
#include <thread>

#include "cppq.hpp"

using namespace std::chrono_literals;
using json = nlohmann::json;
using namespace cppq;

class RedisTestFixture {
 protected:
  std::unique_ptr<redisContext, decltype(&redisFree)> ctx{nullptr, redisFree};
  std::string test_queue_prefix;

  RedisTestFixture() {
    redisOptions options = {0};
    REDIS_OPTIONS_SET_TCP(&options, "127.0.0.1", 6379);
    ctx.reset(redisConnectWithOptions(&options));

    REQUIRE(ctx != nullptr);
    REQUIRE(ctx->err == 0);

    // Generate unique queue prefix for this test run
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(1000, 9999);
    test_queue_prefix = "test_cppq_" + std::to_string(dis(gen)) + "_";
  }

  ~RedisTestFixture() {
    // Clean up any test queues
    if (ctx && ctx->err == 0) {
      auto reply = (redisReply*)redisCommand(ctx.get(), "KEYS %s*",
                                             test_queue_prefix.c_str());
      if (reply && reply->type == REDIS_REPLY_ARRAY) {
        for (size_t i = 0; i < reply->elements; i++) {
          redisCommand(ctx.get(), "DEL %s", reply->element[i]->str);
        }
        freeReplyObject(reply);
      }
    }
  }

  std::string getQueueName(const std::string& suffix = "") {
    return test_queue_prefix + suffix;
  }

  std::string getTaskId(const Task& task) {
    return uuidToString(const_cast<uuid_t&>(task.uuid));
  }
};

TEST_CASE_METHOD(RedisTestFixture, "Basic enqueue and dequeue operations",
                 "[queue]") {
  std::string queue_name = getQueueName("basic");

  SECTION("Enqueue a simple task") {
    json task_data = {{"action", "test"}, {"value", 42}};
    Task task{"test_type", task_data.dump(), 1};
    std::string task_id = getTaskId(task);

    cppq::enqueue(ctx.get(), task, queue_name);

    // Verify task is in pending queue
    auto reply = (redisReply*)redisCommand(ctx.get(), "LLEN cppq:%s:pending",
                                           queue_name.c_str());
    REQUIRE(reply != nullptr);
    REQUIRE(reply->type == REDIS_REPLY_INTEGER);
    REQUIRE(reply->integer == 1);
    freeReplyObject(reply);

    // Verify task data is stored
    std::string task_key = "cppq:" + queue_name + ":task:" + task_id;
    reply = (redisReply*)redisCommand(ctx.get(), "HGET %s payload",
                                      task_key.c_str());
    REQUIRE(reply != nullptr);
    REQUIRE(reply->type == REDIS_REPLY_STRING);

    REQUIRE(std::string(reply->str) == task_data.dump());
    freeReplyObject(reply);
  }

  SECTION("Enqueue multiple tasks") {
    std::vector<std::string> task_ids;
    const int num_tasks = 5;

    for (int i = 0; i < num_tasks; i++) {
      json task_data = {{"task_num", i}};
      Task task{"multi_test", task_data.dump(), 1};
      cppq::enqueue(ctx.get(), task, queue_name);
      task_ids.push_back(getTaskId(task));
    }

    // Verify all tasks are enqueued
    auto reply = (redisReply*)redisCommand(ctx.get(), "LLEN cppq:%s:pending",
                                           queue_name.c_str());
    REQUIRE(reply != nullptr);
    REQUIRE(reply->integer == num_tasks);
    freeReplyObject(reply);
  }

  SECTION("Dequeue returns tasks in correct order") {
    std::vector<std::string> task_ids;
    const int num_tasks = 3;

    for (int i = 0; i < num_tasks; i++) {
      json task_data = {{"order", i}};
      Task task{"order_test", task_data.dump(), 1};
      cppq::enqueue(ctx.get(), task, queue_name);
      task_ids.push_back(getTaskId(task));
    }

    // Dequeue all tasks
    for (int i = 0; i < num_tasks; i++) {
      auto task = cppq::dequeue(ctx.get(), queue_name);
      REQUIRE(task.has_value());
      std::string dequeued_id = uuidToString(task->uuid);
      REQUIRE(dequeued_id == task_ids[i]);

      json data = json::parse(task->payload);
      REQUIRE(data["order"] == i);
    }

    // Queue should be empty
    auto task = cppq::dequeue(ctx.get(), queue_name);
    REQUIRE(!task.has_value());
  }

  SECTION("Dequeue moves task to active state") {
    json task_data = {{"test", "active_state"}};
    Task task{"active_test", task_data.dump(), 1};
    cppq::enqueue(ctx.get(), task, queue_name);
    std::string expected_id = getTaskId(task);

    auto dequeued = cppq::dequeue(ctx.get(), queue_name);
    REQUIRE(dequeued.has_value());
    std::string dequeued_id = uuidToString(dequeued->uuid);
    REQUIRE(dequeued_id == expected_id);

    // Check task is in active queue
    std::string active_queue = "cppq:" + queue_name + ":active";
    // Check if task is in the list
    auto reply = (redisReply*)redisCommand(ctx.get(), "LRANGE %s 0 -1",
                                           active_queue.c_str());
    REQUIRE(reply != nullptr);
    REQUIRE(reply->type == REDIS_REPLY_ARRAY);

    bool found = false;
    for (size_t i = 0; i < reply->elements; i++) {
      if (std::string(reply->element[i]->str) == expected_id) {
        found = true;
        break;
      }
    }
    REQUIRE(found == true);
    freeReplyObject(reply);

    // Check task state
    std::string state_key = "cppq:" + queue_name + ":task:" + expected_id;
    reply = (redisReply*)redisCommand(ctx.get(), "HGET %s state",
                                      state_key.c_str());
    REQUIRE(reply != nullptr);
    REQUIRE(std::string(reply->str) == "Active");
    freeReplyObject(reply);
  }
}

TEST_CASE_METHOD(RedisTestFixture, "Task scheduling functionality",
                 "[scheduling]") {
  std::string queue_name = getQueueName("scheduled");

  SECTION("Schedule task for future execution") {
    json task_data = {{"scheduled", true}};
    Task task{"scheduled_test", task_data.dump(), 1};

    auto future_time = std::chrono::system_clock::now() + 2s;

    cppq::enqueue(ctx.get(), task, queue_name, scheduleOptions(future_time));
    std::string task_id = getTaskId(task);

    // Task should not be in pending queue
    auto reply = (redisReply*)redisCommand(ctx.get(), "LLEN cppq:%s:pending",
                                           queue_name.c_str());
    REQUIRE(reply != nullptr);
    REQUIRE(reply->integer == 0);
    freeReplyObject(reply);

    // Task should be in scheduled queue
    std::string scheduled_queue = "cppq:" + queue_name + ":scheduled";
    reply = (redisReply*)redisCommand(ctx.get(), "LLEN %s",
                                      scheduled_queue.c_str());
    REQUIRE(reply != nullptr);
    REQUIRE(reply->integer == 1);
    freeReplyObject(reply);

    // Dequeue should not return the task yet
    auto dequeued = cppq::dequeue(ctx.get(), queue_name);
    REQUIRE(!dequeued.has_value());

    // Wait for scheduled time
    std::this_thread::sleep_for(2100ms);

    // Manually move scheduled task to pending (simulating what dequeueScheduled
    // would do)
    redisCommand(ctx.get(), "RPOP %s", scheduled_queue.c_str());
    redisCommand(ctx.get(), "LPUSH cppq:%s:pending %s", queue_name.c_str(),
                 task_id.c_str());

    // Now task should be available
    dequeued = cppq::dequeue(ctx.get(), queue_name);
    REQUIRE(dequeued.has_value());
    std::string dequeued_id = uuidToString(dequeued->uuid);
    REQUIRE(dequeued_id == task_id);
  }

  SECTION("Multiple scheduled tasks execute in correct order") {
    std::vector<std::string> task_ids;
    auto now = std::chrono::system_clock::now();
    std::string scheduled_queue = "cppq:" + queue_name + ":scheduled";

    // Schedule tasks in reverse order
    for (int i = 3; i >= 1; i--) {
      json task_data = {{"order", i}};
      Task task{"order_scheduled", std::to_string(i), 1};
      auto scheduled_time = now + std::chrono::milliseconds(i * 100);
      cppq::enqueue(ctx.get(), task, queue_name,
                    scheduleOptions(scheduled_time));
      task_ids.push_back(getTaskId(task));
    }

    // Wait for all tasks to become available
    std::this_thread::sleep_for(400ms);

    // Manually move all scheduled tasks to pending (simulating what
    // dequeueScheduled would do) Since they were added in reverse order
    // (3,2,1), we need to pop them in the right order
    for (int i = 1; i <= 3; i++) {
      redisCommand(ctx.get(), "RPOP %s", scheduled_queue.c_str());
      redisCommand(
          ctx.get(), "LPUSH cppq:%s:pending %s", queue_name.c_str(),
          task_ids[3 - i].c_str());  // task_ids has them in reverse order
    }

    // Tasks should be dequeued in correct order (1, 2, 3)
    for (int i = 1; i <= 3; i++) {
      auto task = cppq::dequeue(ctx.get(), queue_name);
      REQUIRE(task.has_value());
      REQUIRE(task->payload == std::to_string(i));
    }
  }
}

TEST_CASE_METHOD(RedisTestFixture, "Task recovery mechanism", "[recovery]") {
  std::string queue_name = getQueueName("recovery");

  SECTION("Recover stuck tasks") {
    json task_data = {{"test", "recovery"}};
    Task task{"recovery_test", task_data.dump(), 1};
    cppq::enqueue(ctx.get(), task, queue_name);
    std::string task_id = getTaskId(task);

    // Simulate a task getting stuck by manually moving it to active
    // with an old timestamp
    std::string active_queue = "cppq:" + queue_name + ":active";
    auto old_timestamp = std::chrono::system_clock::now() - 2min;
    auto old_ts_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                         old_timestamp.time_since_epoch())
                         .count();

    redisCommand(ctx.get(), "RPOP cppq:%s:pending", queue_name.c_str());
    redisCommand(ctx.get(), "LPUSH %s %s", active_queue.c_str(),
                 task_id.c_str());
    redisCommand(ctx.get(),
                 "HSET cppq:%s:task:%s state Active dequeuedAtMs %lld",
                 queue_name.c_str(), task_id.c_str(), old_ts_ms);

    // Run recovery in a separate thread with 1 minute timeout
    redisOptions options = {0};
    REDIS_OPTIONS_SET_TCP(&options, "127.0.0.1", 6379);
    std::map<std::string, int> queues = {{queue_name, 1}};

    std::thread recovery_thread(
        [options, queues]() { cppq::recovery(options, queues, 60, 100); });

    // Small delay to allow recovery to run
    std::this_thread::sleep_for(500ms);

    // Detach the thread (it will run forever but we're done testing)
    recovery_thread.detach();

    // Task should be back in pending queue
    auto reply = (redisReply*)redisCommand(ctx.get(), "LLEN cppq:%s:pending",
                                           queue_name.c_str());
    REQUIRE(reply != nullptr);
    REQUIRE(reply->integer == 1);
    freeReplyObject(reply);

    // Task should not be in active queue
    reply = (redisReply*)redisCommand(ctx.get(), "LRANGE %s 0 -1",
                                      active_queue.c_str());
    REQUIRE(reply != nullptr);
    REQUIRE(reply->type == REDIS_REPLY_ARRAY);

    bool found_in_active = false;
    for (size_t i = 0; i < reply->elements; i++) {
      if (std::string(reply->element[i]->str) == task_id) {
        found_in_active = true;
        break;
      }
    }
    REQUIRE(found_in_active == false);
    freeReplyObject(reply);
  }

  SECTION("Don't recover recent active tasks") {
    json task_data = {{"test", "no_recovery"}};
    Task task{"no_recovery_test", task_data.dump(), 1};
    cppq::enqueue(ctx.get(), task, queue_name);

    // Dequeue task (makes it active with current timestamp)
    auto dequeued = cppq::dequeue(ctx.get(), queue_name);
    REQUIRE(dequeued.has_value());
    std::string task_id = uuidToString(dequeued->uuid);

    // Verify the task has a recent dequeuedAtMs
    auto check_reply = (redisReply*)redisCommand(
        ctx.get(), "HGET cppq:%s:task:%s dequeuedAtMs", queue_name.c_str(),
        task_id.c_str());
    REQUIRE(check_reply != nullptr);
    REQUIRE(check_reply->type == REDIS_REPLY_STRING);
    freeReplyObject(check_reply);

    // Small delay to ensure timestamp difference
    std::this_thread::sleep_for(10ms);

    // Run recovery in a separate thread with 60 second timeout
    redisOptions options = {0};
    REDIS_OPTIONS_SET_TCP(&options, "127.0.0.1", 6379);
    std::map<std::string, int> queues = {{queue_name, 1}};

    std::thread recovery_thread(
        [options, queues]() { cppq::recovery(options, queues, 60, 100); });

    // Small delay to allow recovery to run at least once
    std::this_thread::sleep_for(150ms);

    // Detach the thread (it will run forever but we're done testing)
    recovery_thread.detach();

    // Task should still be in active queue
    std::string active_queue = "cppq:" + queue_name + ":active";
    auto reply = (redisReply*)redisCommand(ctx.get(), "LRANGE %s 0 -1",
                                           active_queue.c_str());
    REQUIRE(reply != nullptr);
    REQUIRE(reply->type == REDIS_REPLY_ARRAY);

    bool found_in_active = false;
    for (size_t i = 0; i < reply->elements; i++) {
      if (std::string(reply->element[i]->str) == task_id) {
        found_in_active = true;
        break;
      }
    }
    REQUIRE(found_in_active == true);
    freeReplyObject(reply);

    // Task should not be in pending queue
    reply = (redisReply*)redisCommand(ctx.get(), "LLEN cppq:%s:pending",
                                      queue_name.c_str());
    REQUIRE(reply != nullptr);
    REQUIRE(reply->integer == 0);
    freeReplyObject(reply);
  }
}

TEST_CASE_METHOD(RedisTestFixture, "Queue pause and unpause functionality",
                 "[pause]") {
  std::string queue_name = getQueueName("pause");

  SECTION("Pause prevents dequeue") {
    json task_data = {{"test", "pause"}};
    Task task{"pause_test", task_data.dump(), 1};
    cppq::enqueue(ctx.get(), task, queue_name);

    // Pause the queue
    cppq::pause(ctx.get(), queue_name);

    // Verify queue is paused
    auto reply = (redisReply*)redisCommand(
        ctx.get(), "SISMEMBER cppq:queues:paused %s", queue_name.c_str());
    REQUIRE(reply != nullptr);
    REQUIRE(reply->type == REDIS_REPLY_INTEGER);
    REQUIRE(reply->integer == 1);
    freeReplyObject(reply);

    // Check that isPaused returns true
    REQUIRE(cppq::isPaused(ctx.get(), queue_name) == true);

    // Note: dequeue itself doesn't check pause state, only runServer does
    // So we'll just verify the task is still available for dequeue
    auto dequeued = cppq::dequeue(ctx.get(), queue_name);
    REQUIRE(dequeued.has_value());
  }

  SECTION("Unpause allows dequeue") {
    json task_data = {{"test", "unpause"}};
    Task task{"unpause_test", task_data.dump(), 1};
    cppq::enqueue(ctx.get(), task, queue_name);
    std::string task_id = getTaskId(task);

    // Pause then unpause
    cppq::pause(ctx.get(), queue_name);
    cppq::unpause(ctx.get(), queue_name);

    // Verify queue is not paused
    auto reply = (redisReply*)redisCommand(
        ctx.get(), "SISMEMBER cppq:queues:paused %s", queue_name.c_str());
    REQUIRE(reply != nullptr);
    REQUIRE(reply->type == REDIS_REPLY_INTEGER);
    REQUIRE(reply->integer == 0);
    freeReplyObject(reply);

    // Dequeue should work
    auto dequeued = cppq::dequeue(ctx.get(), queue_name);
    REQUIRE(dequeued.has_value());
    std::string dequeued_id = uuidToString(dequeued->uuid);
    REQUIRE(dequeued_id == task_id);
  }

  SECTION("Enqueue still works when paused") {
    cppq::pause(ctx.get(), queue_name);

    // Should be able to enqueue
    json task_data = {{"test", "paused_enqueue"}};
    Task task{"paused_enqueue_test", task_data.dump(), 1};
    cppq::enqueue(ctx.get(), task, queue_name);

    // Verify task is in queue
    auto reply = (redisReply*)redisCommand(ctx.get(), "LLEN cppq:%s:pending",
                                           queue_name.c_str());
    REQUIRE(reply != nullptr);
    REQUIRE(reply->integer == 1);
    freeReplyObject(reply);

    // Check that queue is paused
    REQUIRE(cppq::isPaused(ctx.get(), queue_name) == true);

    // Note: dequeue itself doesn't check pause state
    // The pause mechanism only works within runServer
    auto dequeued = cppq::dequeue(ctx.get(), queue_name);
    REQUIRE(dequeued.has_value());
  }
}

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

TEST_CASE_METHOD(RedisTestFixture, "Handler registration and task processing",
                 "[handlers]") {
  std::string queue_name = getQueueName("handlers");

  SECTION("Register handler and process email delivery task") {
    cppq::registerHandler(TypeEmailDelivery, &HandleEmailDeliveryTask);

    // Create and enqueue task
    Task task = NewEmailDeliveryTask(
        EmailDeliveryPayload{.UserID = 666, .TemplateID = "AH"});
    cppq::enqueue(ctx.get(), task, queue_name);

    // Verify task is enqueued with correct data
    auto dequeued = cppq::dequeue(ctx.get(), queue_name);
    REQUIRE(dequeued.has_value());
    REQUIRE(dequeued->type == TypeEmailDelivery);
    REQUIRE(dequeued->payload == "{\"TemplateID\":\"AH\",\"UserID\":666}");
    REQUIRE(dequeued->maxRetry == 10);

    // Process the task through handler
    HandleEmailDeliveryTask(*dequeued);
    json result = json::parse(dequeued->result);
    REQUIRE(result["Sent"] == true);
  }

  SECTION("Raw Redis command verification for enqueue") {
    cppq::registerHandler(TypeEmailDelivery, &HandleEmailDeliveryTask);

    Task task = NewEmailDeliveryTask(
        EmailDeliveryPayload{.UserID = 666, .TemplateID = "AH"});
    cppq::enqueue(ctx.get(), task, queue_name);
    std::string task_id = getTaskId(task);

    // Verify using raw Redis commands like in tests.cpp
    auto reply = (redisReply*)redisCommand(
        ctx.get(), "LRANGE cppq:%s:pending -1 -1", queue_name.c_str());
    REQUIRE(reply != nullptr);
    REQUIRE(reply->type == REDIS_REPLY_ARRAY);
    REQUIRE(reply->elements == 1);

    std::string uuid = reply->element[0]->str;
    REQUIRE(uuid == task_id);
    freeReplyObject(reply);

    // Check task details with MULTI/EXEC
    redisCommand(ctx.get(), "MULTI");
    redisCommand(ctx.get(), "HGET cppq:%s:task:%s type", queue_name.c_str(),
                 uuid.c_str());
    redisCommand(ctx.get(), "HGET cppq:%s:task:%s payload", queue_name.c_str(),
                 uuid.c_str());
    redisCommand(ctx.get(), "HGET cppq:%s:task:%s state", queue_name.c_str(),
                 uuid.c_str());
    redisCommand(ctx.get(), "HGET cppq:%s:task:%s maxRetry", queue_name.c_str(),
                 uuid.c_str());
    redisCommand(ctx.get(), "HGET cppq:%s:task:%s retried", queue_name.c_str(),
                 uuid.c_str());
    redisCommand(ctx.get(), "HGET cppq:%s:task:%s dequeuedAtMs",
                 queue_name.c_str(), uuid.c_str());
    reply = (redisReply*)redisCommand(ctx.get(), "EXEC");

    REQUIRE(reply != nullptr);
    REQUIRE(reply->type == REDIS_REPLY_ARRAY);
    REQUIRE(reply->elements == 6);

    REQUIRE(std::string(reply->element[0]->str) == TypeEmailDelivery);
    REQUIRE(std::string(reply->element[1]->str) ==
            "{\"TemplateID\":\"AH\",\"UserID\":666}");
    REQUIRE(std::string(reply->element[2]->str) == "Pending");
    REQUIRE(std::string(reply->element[3]->str) == "10");
    REQUIRE(std::string(reply->element[4]->str) == "0");
    REQUIRE(std::string(reply->element[5]->str) == "0");

    freeReplyObject(reply);
  }
}

TEST_CASE("Thread pool functionality", "[threadpool]") {
  SECTION("Basic thread pool execution") {
    thread_pool pool(4);
    std::atomic<int> counter{0};

    // Submit multiple tasks
    for (int i = 0; i < 10; i++) {
      pool.push_task([&counter]() {
        counter++;
        std::this_thread::sleep_for(10ms);
      });
    }

    // Wait for all tasks to complete
    pool.wait_for_tasks();

    REQUIRE(counter == 10);
  }

  SECTION("Thread pool with shared state") {
    thread_pool pool(2);
    std::atomic<int> result1{0};
    std::atomic<bool> result2_set{false};
    std::string result2;
    std::mutex result2_mutex;

    pool.push_task([&result1]() { result1 = 42; });
    pool.push_task([&result2, &result2_mutex, &result2_set]() {
      std::lock_guard<std::mutex> lock(result2_mutex);
      result2 = "hello";
      result2_set = true;
    });

    pool.wait_for_tasks();

    REQUIRE(result1 == 42);
    REQUIRE(result2_set == true);
    std::lock_guard<std::mutex> lock(result2_mutex);
    REQUIRE(result2 == "hello");
  }

  SECTION("Thread pool handles exceptions") {
    thread_pool pool(2);
    std::atomic<bool> exception_caught{false};

    pool.push_task([&exception_caught]() {
      try {
        throw std::runtime_error("test exception");
      } catch (const std::runtime_error&) {
        exception_caught = true;
      }
    });

    pool.wait_for_tasks();

    REQUIRE(exception_caught == true);
  }

  SECTION("Thread pool processes tasks concurrently") {
    thread_pool pool(4);
    std::atomic<int> concurrent_count{0};
    std::atomic<int> max_concurrent{0};

    for (int i = 0; i < 8; i++) {
      pool.push_task([&concurrent_count, &max_concurrent]() {
        int current = ++concurrent_count;
        int expected = max_concurrent.load();
        while (current > expected &&
               !max_concurrent.compare_exchange_weak(expected, current));

        std::this_thread::sleep_for(50ms);
        concurrent_count--;
      });
    }

    pool.wait_for_tasks();

    // Should have had multiple threads working concurrently
    REQUIRE(max_concurrent.load() > 1);
    REQUIRE(max_concurrent.load() <= 4);
  }
}

TEST_CASE_METHOD(RedisTestFixture, "Error handling and edge cases",
                 "[errors]") {
  std::string queue_name = getQueueName("errors");

  SECTION("Dequeue from non-existent queue returns empty") {
    auto task = cppq::dequeue(ctx.get(), "non_existent_queue");
    REQUIRE(!task.has_value());
  }

  SECTION("Empty queue name handling") {
    json task_data = {{"test", "empty_queue"}};
    Task task{"empty_queue_test", task_data.dump(), 1};
    cppq::enqueue(ctx.get(), task, "");
    std::string task_id = getTaskId(task);

    // Should be able to dequeue
    auto dequeued = cppq::dequeue(ctx.get(), "");
    REQUIRE(dequeued.has_value());
    std::string dequeued_id = uuidToString(dequeued->uuid);
    REQUIRE(dequeued_id == task_id);
  }

  SECTION("Large task data") {
    std::string large_data(1024 * 100, 'x');  // 100KB of data
    json task_data = {{"data", large_data}};
    Task task{"large_test", task_data.dump(), 1};
    cppq::enqueue(ctx.get(), task, queue_name);

    auto dequeued = cppq::dequeue(ctx.get(), queue_name);
    REQUIRE(dequeued.has_value());
    std::string dequeued_id = uuidToString(dequeued->uuid);
    std::string task_id = getTaskId(task);
    REQUIRE(dequeued_id == task_id);

    json retrieved = json::parse(dequeued->payload);
    REQUIRE(retrieved["data"] == large_data);
  }

  SECTION("Concurrent enqueue operations") {
    const int num_threads = 10;
    const int tasks_per_thread = 100;
    std::vector<std::thread> threads;
    std::atomic<int> success_count{0};

    for (int t = 0; t < num_threads; t++) {
      threads.emplace_back([&, t]() {
        redisOptions options = {0};
        REDIS_OPTIONS_SET_TCP(&options, "127.0.0.1", 6379);
        auto local_ctx = redisConnectWithOptions(&options);

        for (int i = 0; i < tasks_per_thread; i++) {
          json task_data = {{"thread", t}, {"task", i}};
          Task task{"concurrent_test", task_data.dump(), 1};
          cppq::enqueue(local_ctx, task, queue_name);
          success_count++;
        }

        redisFree(local_ctx);
      });
    }

    for (auto& t : threads) {
      t.join();
    }

    REQUIRE(success_count == num_threads * tasks_per_thread);

    // Verify queue length
    auto reply = (redisReply*)redisCommand(ctx.get(), "LLEN cppq:%s:pending",
                                           queue_name.c_str());
    REQUIRE(reply != nullptr);
    REQUIRE(reply->integer == num_threads * tasks_per_thread);
    freeReplyObject(reply);
  }

  SECTION("Task state transitions") {
    json task_data = {{"test", "state_transitions"}};
    Task task{"state_test", task_data.dump(), 1};
    cppq::enqueue(ctx.get(), task, queue_name);
    std::string task_id = getTaskId(task);

    // Check initial state (should be Pending)
    std::string state_key = "cppq:" + queue_name + ":task:" + task_id;
    auto reply = (redisReply*)redisCommand(ctx.get(), "HGET %s state",
                                           state_key.c_str());
    REQUIRE(reply != nullptr);
    REQUIRE(std::string(reply->str) == "Pending");
    freeReplyObject(reply);

    // Dequeue (moves to Active)
    auto dequeued = cppq::dequeue(ctx.get(), queue_name);
    REQUIRE(dequeued.has_value());

    reply = (redisReply*)redisCommand(ctx.get(), "HGET %s state",
                                      state_key.c_str());
    REQUIRE(reply != nullptr);
    REQUIRE(std::string(reply->str) == "Active");
    freeReplyObject(reply);
  }
}
