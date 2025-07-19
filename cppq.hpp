#pragma once

#include <string>
#include <cstdint>
#include <chrono>
#include <thread>
#include <iostream>
#include <atomic>
#include <condition_variable>
#include <exception>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <queue>
#include <type_traits>
#include <utility>
#include <optional>
#include <map>

#include <hiredis/hiredis.h>
#include <uuid/uuid.h>

namespace cppq {
  using concurrency_t = std::invoke_result_t<decltype(std::thread::hardware_concurrency)>;

  // Retrofitted from https://github.com/bshoshany/thread-pool
  class [[nodiscard]] thread_pool
  {
    public:
      thread_pool(const concurrency_t thread_count_ = 0) :
        thread_count(determine_thread_count(thread_count_)),
        threads(std::make_unique<std::thread[]>(determine_thread_count(thread_count_))) {
          create_threads();
        }

      ~thread_pool() {
        wait_for_tasks();
        destroy_threads();
      }

      [[nodiscard]] concurrency_t get_thread_count() const {
        return thread_count;
      }

      template <typename F, typename... A>
        void push_task(F&& task, A&&... args) {
          std::function<void()> task_function =
            std::bind(std::forward<F>(task), std::forward<A>(args)...);
          {
            const std::scoped_lock tasks_lock(tasks_mutex);
            tasks.push(task_function);
          }
          ++tasks_total;
          task_available_cv.notify_one();
        }

      void wait_for_tasks() {
        waiting = true;
        std::unique_lock<std::mutex> tasks_lock(tasks_mutex);
        task_done_cv.wait(tasks_lock, [this] { return (tasks_total == 0); });
        waiting = false;
      }

    private:
      void create_threads() {
        running = true;
        for (concurrency_t i = 0; i < thread_count; ++i) {
          threads[i] = std::thread(&thread_pool::worker, this);
        }
      }

      void destroy_threads() {
        running = false;
        task_available_cv.notify_all();
        for (concurrency_t i = 0; i < thread_count; ++i) {
          threads[i].join();
        }
      }

      [[nodiscard]] concurrency_t determine_thread_count(const concurrency_t thread_count_) {
        if (thread_count_ > 0)
          return thread_count_;
        else {
          if (std::thread::hardware_concurrency() > 0)
            return std::thread::hardware_concurrency();
          else
            return 1;
        }
      }

      void worker() {
        while (running) {
          std::function<void()> task;
          std::unique_lock<std::mutex> tasks_lock(tasks_mutex);
          task_available_cv.wait(tasks_lock, [this] { return !tasks.empty() || !running; });
          if (running) {
            task = std::move(tasks.front());
            tasks.pop();
            tasks_lock.unlock();
            task();
            tasks_lock.lock();
            --tasks_total;
            if (waiting)
              task_done_cv.notify_one();
          }
        }
      }

      std::atomic<bool> running = false;
      std::condition_variable task_available_cv = {};
      std::condition_variable task_done_cv = {};
      std::queue<std::function<void()>> tasks = {};
      std::atomic<size_t> tasks_total = 0;
      mutable std::mutex tasks_mutex = {};
      concurrency_t thread_count = 0;
      std::unique_ptr<std::thread[]> threads = nullptr;
      std::atomic<bool> waiting = false;
  };

  enum class TaskState {
    Unknown,
    Pending,
    Scheduled,
    Active,
    Failed,
    Completed
  };

  std::string stateToString(TaskState state) {
    switch (state) {
      case TaskState::Unknown: return "Unknown";
      case TaskState::Pending: return "Pending";
      case TaskState::Scheduled: return "Scheduled";
      case TaskState::Active: return "Active";
      case TaskState::Failed: return "Failed";
      case TaskState::Completed: return "Completed";
    }
    return "Unknown";
  }

  TaskState stringToState(std::string state) {
    if (state.compare("Unknown") == 0) return TaskState::Unknown;
    if (state.compare("Pending") == 0) return TaskState::Pending;
    if (state.compare("Scheduled") == 0) return TaskState::Scheduled;
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
      Task(std::string type, std::string payload, uint64_t maxRetry) {
        uuid_generate(this->uuid);
        this->type = type;
        this->payload = payload;
        this->state = TaskState::Unknown;
        this->maxRetry = maxRetry;
        this->retried = 0;
        this->dequeuedAtMs = 0;
      }

      Task(
          std::string uuid,
          std::string type,
          std::string payload,
          std::string state,
          uint64_t maxRetry,
          uint64_t retried,
          uint64_t dequeuedAtMs,
          uint64_t schedule = 0,
          std::string cron = ""
          ) {
        uuid_t uuid_parsed;
        uuid_parse(uuid.c_str(), uuid_parsed);
        uuid_copy(this->uuid, uuid_parsed);
        this->type = type;
        this->payload = payload;
        this->maxRetry = maxRetry;
        this->retried = retried;
        this->dequeuedAtMs = dequeuedAtMs;
        this->state = stringToState(state);
        this->schedule = schedule;
        this->cron = cron;
      }

      uuid_t uuid;
      std::string type;
      std::string payload;
      TaskState state;
      uint64_t maxRetry;
      uint64_t retried;
      uint64_t dequeuedAtMs;
      uint64_t schedule;
      std::string cron;
      std::string result;
  };

  using Handler = void (*)(Task&);
  auto handlers = std::unordered_map<std::string, Handler>();

  void registerHandler(std::string type, Handler handler) {
    handlers[type] = handler;
  }

  typedef enum { Cron, TimePoint, None } ScheduleType;

  typedef struct ScheduleOptions {
    union {
      const char *cron;
      std::chrono::system_clock::time_point time;
    };
    ScheduleType type;
  } ScheduleOptions;

  ScheduleOptions scheduleOptions(std::chrono::system_clock::time_point t) {
    return ScheduleOptions{ .time = t, .type = ScheduleType::TimePoint };
  }

  ScheduleOptions scheduleOptions(std::string c) {
    return ScheduleOptions{ .cron = c.c_str(), .type = ScheduleType::Cron };
  }

  void enqueue(redisContext *c, Task task, std::string queue, ScheduleOptions s) {
    if (s.type == ScheduleType::None)
      task.state = TaskState::Pending;
    else
      task.state = TaskState::Scheduled;

    redisCommand(c, "MULTI");
    if (s.type == ScheduleType::None) {
      redisCommand(c, "LPUSH cppq:%s:pending %s", queue.c_str(), uuidToString(task.uuid).c_str());
      redisCommand(
          c,
          "HSET cppq:%s:task:%s type %s payload %s state %s maxRetry %d retried %d dequeuedAtMs %d",
          queue.c_str(),
          uuidToString(task.uuid).c_str(),
          task.type.c_str(),
          task.payload.c_str(),
          stateToString(task.state).c_str(),
          task.maxRetry,
          task.retried,
          task.dequeuedAtMs
          );
    } else if (s.type == ScheduleType::TimePoint) {
      redisCommand(c, "LPUSH cppq:%s:scheduled %s", queue.c_str(), uuidToString(task.uuid).c_str());
      redisCommand(
          c,
          "HSET cppq:%s:task:%s type %s payload %s state %s maxRetry %d retried %d dequeuedAtMs %d schedule %lu",
          queue.c_str(),
          uuidToString(task.uuid).c_str(),
          task.type.c_str(),
          task.payload.c_str(),
          stateToString(task.state).c_str(),
          task.maxRetry,
          task.retried,
          task.dequeuedAtMs,
          std::chrono::duration_cast<std::chrono::milliseconds>(s.time.time_since_epoch()).count()
          );
    } else if (s.type == ScheduleType::Cron) {
      redisCommand(c, "LPUSH cppq:%s:scheduled %s", queue.c_str(), uuidToString(task.uuid).c_str());
      redisCommand(
          c,
          "HSET cppq:%s:task:%s type %s payload %s state %s maxRetry %d retried %d dequeuedAtMs %d cron %s",
          queue.c_str(),
          uuidToString(task.uuid).c_str(),
          task.type.c_str(),
          task.payload.c_str(),
          stateToString(task.state).c_str(),
          task.maxRetry,
          task.retried,
          task.dequeuedAtMs,
          s.cron
          );
    }
    redisReply *reply = (redisReply *)redisCommand(c, "EXEC");

    if (reply->type == REDIS_REPLY_ERROR)
      throw std::runtime_error("Failed to enqueue task");
  }

  void enqueue(redisContext *c, Task task, std::string queue) {
    return enqueue(c, task, queue, ScheduleOptions{ .cron = "", .type = ScheduleType::None });
  }

  std::optional<Task> dequeue(redisContext *c, std::string queue) {
    redisReply *reply = (redisReply *)redisCommand(c, "LRANGE cppq:%s:pending -1 -1", queue.c_str());
    if (reply->type != REDIS_REPLY_ARRAY)
      return {};
    if (reply->elements == 0)
      return {};
    reply = reply->element[0];
    std::string uuid = reply->str;

    uint64_t dequeuedAtMs =
      std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();

    redisCommand(c, "MULTI");
    redisCommand(c, "LREM cppq:%s:pending 1 %s", queue.c_str(), uuid.c_str());
    redisCommand(c, "HGET cppq:%s:task:%s type", queue.c_str(), uuid.c_str());
    redisCommand(c, "HGET cppq:%s:task:%s payload", queue.c_str(), uuid.c_str());
    redisCommand(c, "HGET cppq:%s:task:%s state", queue.c_str(), uuid.c_str());
    redisCommand(c, "HGET cppq:%s:task:%s maxRetry", queue.c_str(), uuid.c_str());
    redisCommand(c, "HGET cppq:%s:task:%s retried", queue.c_str(), uuid.c_str());
    redisCommand(c, "HGET cppq:%s:task:%s dequeuedAtMs", queue.c_str(), uuid.c_str());
    redisCommand(c, "HSET cppq:%s:task:%s dequeuedAtMs %lu", queue.c_str(), uuid.c_str(), dequeuedAtMs);
    redisCommand(c, "HSET cppq:%s:task:%s state %s", queue.c_str(), uuid.c_str(), stateToString(TaskState::Active).c_str());
    redisCommand(c, "LPUSH cppq:%s:active %s", queue.c_str(), uuid.c_str());
    reply = (redisReply *)redisCommand(c, "EXEC");

    if (reply->type != REDIS_REPLY_ARRAY || reply->elements != 10)
      return {};

    Task task = Task(
        uuid,
        reply->element[1]->str,
        reply->element[2]->str,
        stateToString(TaskState::Active),
        strtoull(reply->element[4]->str, NULL, 0),
        strtoull(reply->element[5]->str, NULL, 0),
        dequeuedAtMs
        );

    return std::make_optional<Task>(task);
  }

  std::optional<Task> dequeueScheduled(redisContext *c, std::string queue, char *getScheduledScriptSHA) {
    redisReply *reply = (redisReply *)redisCommand(c, "EVALSHA %s 0 %s", getScheduledScriptSHA, queue.c_str());
    if (reply->type != REDIS_REPLY_STRING)
      return {};
    std::string uuid = reply->str;

    uint64_t dequeuedAtMs =
      std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();

    redisCommand(c, "MULTI");
    redisCommand(c, "LREM cppq:%s:scheduled 1 %s", queue.c_str(), uuid.c_str());
    redisCommand(c, "HGET cppq:%s:task:%s type", queue.c_str(), uuid.c_str());
    redisCommand(c, "HGET cppq:%s:task:%s payload", queue.c_str(), uuid.c_str());
    redisCommand(c, "HGET cppq:%s:task:%s state", queue.c_str(), uuid.c_str());
    redisCommand(c, "HGET cppq:%s:task:%s maxRetry", queue.c_str(), uuid.c_str());
    redisCommand(c, "HGET cppq:%s:task:%s retried", queue.c_str(), uuid.c_str());
    redisCommand(c, "HGET cppq:%s:task:%s dequeuedAtMs", queue.c_str(), uuid.c_str());
    redisCommand(c, "HGET cppq:%s:task:%s schedule", queue.c_str(), uuid.c_str());
    redisCommand(c, "HSET cppq:%s:task:%s dequeuedAtMs %lu", queue.c_str(), uuid.c_str(), dequeuedAtMs);
    redisCommand(c, "HSET cppq:%s:task:%s state %s", queue.c_str(), uuid.c_str(), stateToString(TaskState::Active).c_str());
    redisCommand(c, "LPUSH cppq:%s:active %s", queue.c_str(), uuid.c_str());
    reply = (redisReply *)redisCommand(c, "EXEC");

    if (reply->type != REDIS_REPLY_ARRAY || reply->elements != 11)
      return {};

    Task task = Task(
        uuid,
        reply->element[1]->str,
        reply->element[2]->str,
        stateToString(TaskState::Active),
        strtoull(reply->element[4]->str, NULL, 0),
        strtoull(reply->element[5]->str, NULL, 0),
        dequeuedAtMs,
        strtoull(reply->element[6]->str, NULL, 0)
        );

    return std::make_optional<Task>(task);
  }

  void taskRunner(redisOptions redisOpts, Task task, std::string queue) {
    redisContext *c = redisConnectWithOptions(&redisOpts);
    if (c == NULL || c->err) {
      std::cerr << "Failed to connect to Redis" << std::endl;
      return;
    }

    Handler handler = handlers[task.type];

    try {
      handler(task);
    } catch(const std::exception &e) {
      task.retried++;
      redisCommand(c, "MULTI");
      redisCommand(c, "LREM cppq:%s:active 1 %s", queue.c_str(), uuidToString(task.uuid).c_str());
      redisCommand(c, "HSET cppq:%s:task:%s retried %d", queue.c_str(), uuidToString(task.uuid).c_str(), task.retried);
      if (task.retried >= task.maxRetry) {
        task.state = TaskState::Failed;
        redisCommand(
            c,
            "HSET cppq:%s:task:%s state %s",
            queue.c_str(),
            uuidToString(task.uuid).c_str(),
            stateToString(task.state).c_str()
            );
        redisCommand(c, "LPUSH cppq:%s:failed %s", queue.c_str(), uuidToString(task.uuid).c_str());
      } else {
        task.state = TaskState::Pending;
        redisCommand(
            c,
            "HSET cppq:%s:task:%s state %s",
            queue.c_str(),
            uuidToString(task.uuid).c_str(),
            stateToString(task.state).c_str()
            );
        redisCommand(c, "LPUSH cppq:%s:pending %s", queue.c_str(), uuidToString(task.uuid).c_str());
      }
      redisCommand(c, "EXEC");
      redisFree(c);
      return;
    }

    task.state = TaskState::Completed;
    redisCommand(c, "MULTI");
    redisCommand(c, "LREM cppq:%s:active 1 %s", queue.c_str(), uuidToString(task.uuid).c_str());
    redisCommand(
        c,
        "HSET cppq:%s:task:%s state %s",
        queue.c_str(),
        uuidToString(task.uuid).c_str(),
        stateToString(task.state).c_str()
        );
    redisCommand(
        c,
        "HSET cppq:%s:task:%s result %s",
        queue.c_str(),
        uuidToString(task.uuid).c_str(),
        task.result.c_str()
        );
    redisCommand(c, "LPUSH cppq:%s:completed %s", queue.c_str(), uuidToString(task.uuid).c_str());
    redisCommand(c, "EXEC");
    redisFree(c);
  }

  void recovery(redisOptions redisOpts, std::map<std::string, int> queues, uint64_t timeoutMs, uint64_t checkEveryMs) {
    redisContext *c = redisConnectWithOptions(&redisOpts);
    if (c == NULL || c->err) {
      std::cerr << "Failed to connect to Redis" << std::endl;
      return;
    }

    // TODO: Consider incrementing `retried` on recovery
    while (true) {
      std::this_thread::sleep_for(std::chrono::milliseconds(checkEveryMs));
      for (std::map<std::string, int>::iterator it = queues.begin(); it != queues.end(); it++) {
        redisReply *reply = (redisReply *)redisCommand(c, "LRANGE cppq:%s:active 0 -1", it->first.c_str());
        for (int i = 0; i < reply->elements; i++) {
          std::string uuid = reply->element[i]->str;
          redisReply *dequeuedAtMsReply = (redisReply *)redisCommand(
              c,
              "HGET cppq:%s:task:%s dequeuedAtMs",
              it->first.c_str(),
              uuid.c_str()
              );
          redisReply *scheduleReply = (redisReply *)redisCommand(
              c,
              "HGET cppq:%s:task:%s schedule",
              it->first.c_str(),
              uuid.c_str()
              );
          uint64_t dequeuedAtMs = strtoull(dequeuedAtMsReply->str, NULL, 0);
          if (
              dequeuedAtMs + timeoutMs <
              std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()
                ).count()
             ) {
            redisCommand(c, "MULTI");
            redisCommand(c, "LREM cppq:%s:active 1 %s", it->first.c_str(), uuid.c_str());
            redisCommand(
                c,
                "HSET cppq:%s:task:%s state %s",
                it->first.c_str(),
                uuid.c_str(),
                stateToString(TaskState::Pending).c_str()
                );
            if (scheduleReply->type == REDIS_REPLY_NIL)
              redisCommand(c, "LPUSH cppq:%s:pending %s", it->first.c_str(), uuid.c_str());
            else
              redisCommand(c, "LPUSH cppq:%s:scheduled %s", it->first.c_str(), uuid.c_str());
            redisCommand(c, "EXEC");
          }
        }
      }
    }
  }

  void pause(redisContext *c, std::string queue) {
    redisCommand(c, "SADD cppq:queues:paused %s", queue.c_str());
  }

  void unpause(redisContext *c, std::string queue) {
    redisCommand(c, "SREM cppq:queues:paused %s", queue.c_str());
  }

  bool isPaused(redisContext *c, std::string queue) {
    redisReply *reply = (redisReply *)redisCommand(c, "SMEMBERS cppq:queues:paused");
    for (int i = 0; i < reply->elements; i++)
      if (queue == reply->element[i]->str)
        return true;
    return false;
  }

  const char *getScheduledScript = R"DOC(
    local timeCall = redis.call('time')
    local time = timeCall[1] .. timeCall[2]
    local scheduled = redis.call('LRANGE',  'cppq:' .. ARGV[1] .. ':scheduled', 0, -1)
    for _, key in ipairs(scheduled) do
      if (time > redis.call('HGET', 'cppq:' .. ARGV[1] .. ':task:' .. key, 'schedule')) then
        return key
      end
    end)DOC";

  void runServer(redisOptions redisOpts, std::map<std::string, int> queues, uint64_t recoveryTimeoutSecond) {
    redisContext *c = redisConnectWithOptions(&redisOpts);
    if (c == NULL || c->err) {
      std::cerr << "Failed to connect to Redis" << std::endl;
      return;
    }

    redisReply *reply = (redisReply *)redisCommand(c, "SCRIPT LOAD %s", getScheduledScript);
    char *getScheduledScriptSHA = reply->str;

    std::vector<std::pair<std::string, int>> queuesVector;
    for (auto& it : queues) queuesVector.push_back(it);
    sort(
        queuesVector.begin(),
        queuesVector.end(),
        [](std::pair<std::string, int> const& a, std::pair<std::string, int> const& b) { return a.second > b.second; }
        );

    for (auto it = queuesVector.begin(); it != queuesVector.end(); it++)
      redisCommand(c, "SADD cppq:queues %s:%d", it->first.c_str(), it->second);

    thread_pool pool;
    pool.push_task(recovery, redisOpts, queues, recoveryTimeoutSecond * 1000, 10000);

    while (true) {
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
      for (std::vector<std::pair<std::string, int>>::iterator it = queuesVector.begin(); it != queuesVector.end(); it++) {
        if (isPaused(c, it->first))
            continue;
        std::optional<Task> task;
        task = dequeueScheduled(c, it->first, getScheduledScriptSHA);
        if (!task.has_value())
          task = dequeue(c, it->first);
        if (task.has_value()) {
          pool.push_task(taskRunner, redisOpts, task.value(), it->first);
          break;
        }
      }
    }
  }
}

