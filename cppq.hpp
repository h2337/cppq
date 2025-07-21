#pragma once

#include <hiredis/hiredis.h>
#include <uuid/uuid.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <exception>
#include <functional>
#include <future>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <queue>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace cppq {
using concurrency_t =
    std::invoke_result_t<decltype(std::thread::hardware_concurrency)>;

enum class ErrorCode {
  Success = 0,
  ConnectionFailed,
  EnqueueFailed,
  DequeueFailed,
  InvalidTask,
  RedisError,
  TaskNotFound,
  QueueEmpty
};

class CppqException : public std::exception {
 public:
  CppqException(ErrorCode code, std::string_view message)
      : code_(code), message_(message) {}

  const char *what() const noexcept override { return message_.c_str(); }
  ErrorCode code() const noexcept { return code_; }

 private:
  ErrorCode code_;
  std::string message_;
};

class UUID {
 public:
  UUID() { uuid_generate(data_.data()); }

  explicit UUID(const uuid_t uuid) {
    std::copy(uuid, uuid + 16, data_.begin());
  }

  explicit UUID(std::string_view uuid_str) {
    uuid_t temp;
    if (uuid_parse(uuid_str.data(), temp) != 0) {
      throw CppqException(ErrorCode::InvalidTask, "Invalid UUID string");
    }
    std::copy(std::begin(temp), std::end(temp), data_.begin());
  }

  std::string toString() const {
    char uuid_str[37];
    uuid_unparse_lower(data_.data(), uuid_str);
    return uuid_str;
  }

  const std::array<unsigned char, 16> &data() const noexcept { return data_; }

 private:
  std::array<unsigned char, 16> data_;
};

class RedisConnection {
 public:
  explicit RedisConnection(const redisOptions &options)
      : ctx_(redisConnectWithOptions(&options), redisFree) {
    if (!ctx_ || ctx_->err) {
      throw CppqException(
          ErrorCode::ConnectionFailed,
          ctx_ ? ctx_->errstr : "Failed to allocate Redis context");
    }
  }

  redisContext *get() noexcept { return ctx_.get(); }
  const redisContext *get() const noexcept { return ctx_.get(); }

  redisContext *operator->() noexcept { return ctx_.get(); }
  const redisContext *operator->() const noexcept { return ctx_.get(); }

  bool isConnected() const noexcept { return ctx_ && ctx_->err == 0; }

 private:
  std::unique_ptr<redisContext, decltype(&redisFree)> ctx_;
};

class RedisConnectionPool {
 public:
  explicit RedisConnectionPool(const redisOptions &options,
                               size_t pool_size = 10)
      : options_(options), pool_size_(pool_size) {
    for (size_t i = 0; i < pool_size_; ++i) {
      connections_.emplace_back(std::make_unique<RedisConnection>(options_));
    }
  }

  std::unique_ptr<RedisConnection> acquire() {
    std::unique_lock lock(mutex_);
    cv_.wait(lock, [this] { return !connections_.empty(); });

    auto conn = std::move(connections_.back());
    connections_.pop_back();

    if (!conn->isConnected()) {
      conn = std::make_unique<RedisConnection>(options_);
    }

    return conn;
  }

  void release(std::unique_ptr<RedisConnection> conn) {
    if (conn && conn->isConnected()) {
      std::lock_guard lock(mutex_);
      connections_.push_back(std::move(conn));
      cv_.notify_one();
    }
  }

 private:
  redisOptions options_;
  size_t pool_size_;
  std::vector<std::unique_ptr<RedisConnection>> connections_;
  mutable std::mutex mutex_;
  std::condition_variable cv_;
};

class [[nodiscard]] thread_pool {
 public:
  thread_pool(const concurrency_t thread_count_ = 0)
      : thread_count(determine_thread_count(thread_count_)),
        threads(std::make_unique<std::thread[]>(
            determine_thread_count(thread_count_))) {
    create_threads();
  }

  ~thread_pool() {
    wait_for_tasks();
    destroy_threads();
  }

  [[nodiscard]] concurrency_t get_thread_count() const { return thread_count; }

  template <typename F, typename... A>
  void push_task(F &&task, A &&...args) {
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

  [[nodiscard]] concurrency_t determine_thread_count(
      const concurrency_t thread_count_) {
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
      task_available_cv.wait(tasks_lock,
                             [this] { return !tasks.empty() || !running; });
      if (running) {
        task = std::move(tasks.front());
        tasks.pop();
        tasks_lock.unlock();
        task();
        tasks_lock.lock();
        --tasks_total;
        if (waiting) task_done_cv.notify_one();
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

enum class TaskState { Unknown, Pending, Scheduled, Active, Failed, Completed };

inline const char *stateToString(TaskState state) noexcept {
  switch (state) {
    case TaskState::Unknown:
      return "Unknown";
    case TaskState::Pending:
      return "Pending";
    case TaskState::Scheduled:
      return "Scheduled";
    case TaskState::Active:
      return "Active";
    case TaskState::Failed:
      return "Failed";
    case TaskState::Completed:
      return "Completed";
  }
  return "Unknown";
}

inline TaskState stringToState(std::string_view state) noexcept {
  if (state == "Unknown") return TaskState::Unknown;
  if (state == "Pending") return TaskState::Pending;
  if (state == "Scheduled") return TaskState::Scheduled;
  if (state == "Active") return TaskState::Active;
  if (state == "Failed") return TaskState::Failed;
  if (state == "Completed") return TaskState::Completed;
  return TaskState::Unknown;
}

// Deprecated: Use UUID class instead
[[deprecated("Use UUID class instead")]]
std::string uuidToString(uuid_t uuid) {
  char uuid_str[37];
  uuid_unparse_lower(uuid, uuid_str);
  return uuid_str;
}

class Task {
 public:
  Task(std::string type, std::string payload, uint64_t maxRetry)
      : uuid_(),
        type(std::move(type)),
        payload(std::move(payload)),
        state(TaskState::Unknown),
        maxRetry(maxRetry),
        retried(0),
        dequeuedAtMs(0),
        schedule(0) {
    // Initialize legacy uuid field for backward compatibility
    getUuidLegacy(uuid);
  }

  Task(std::string_view uuid_str, std::string type, std::string payload,
       std::string_view state_str, uint64_t maxRetry, uint64_t retried,
       uint64_t dequeuedAtMs, uint64_t schedule = 0, std::string cron = "")
      : uuid_(uuid_str),
        type(std::move(type)),
        payload(std::move(payload)),
        state(stringToState(state_str)),
        maxRetry(maxRetry),
        retried(retried),
        dequeuedAtMs(dequeuedAtMs),
        schedule(schedule),
        cron(std::move(cron)) {
    // Initialize legacy uuid field for backward compatibility
    getUuidLegacy(uuid);
  }

  Task(const Task &) = default;
  Task(Task &&) = default;
  Task &operator=(const Task &) = default;
  Task &operator=(Task &&) = default;

  const UUID &getUuid() const noexcept { return uuid_; }
  std::string getUuidString() const { return uuid_.toString(); }

  // For backward compatibility
  void getUuidLegacy(uuid_t out) const {
    const auto &data = uuid_.data();
    std::copy(data.begin(), data.end(), out);
  }

  UUID uuid_;
  std::string type;
  std::string payload;
  TaskState state;
  uint64_t maxRetry;
  uint64_t retried;
  uint64_t dequeuedAtMs;
  uint64_t schedule;
  std::string cron;
  std::string result;

  // Provide backward compatibility
  uuid_t uuid;  // Deprecated, kept for compatibility
};

using Handler = void (*)(Task &);
auto handlers = std::unordered_map<std::string, Handler>();

void registerHandler(std::string_view type, Handler handler) {
  handlers[std::string(type)] = handler;
}

typedef enum { Cron, TimePoint, None } ScheduleType;

typedef struct ScheduleOptions {
  union {
    const char *cron;
    std::chrono::system_clock::time_point time;
  };
  ScheduleType type;
} ScheduleOptions;

ScheduleOptions scheduleOptions(
    std::chrono::system_clock::time_point t) noexcept {
  return ScheduleOptions{.time = t, .type = ScheduleType::TimePoint};
}

ScheduleOptions scheduleOptions(std::string_view c) noexcept {
  return ScheduleOptions{.cron = c.data(), .type = ScheduleType::Cron};
}

void enqueue(redisContext *c, Task &task, std::string_view queue,
             ScheduleOptions s) {
  if (s.type == ScheduleType::None)
    task.state = TaskState::Pending;
  else
    task.state = TaskState::Scheduled;

  std::string uuid_str = task.getUuidString();
  const char *state_str = stateToString(task.state);

  redisCommand(c, "MULTI");
  if (s.type == ScheduleType::None) {
    redisCommand(c, "LPUSH cppq:%s:pending %s", queue.data(), uuid_str.c_str());
    redisCommand(c,
                 "HSET cppq:%s:task:%s type %s payload %s state %s maxRetry %d "
                 "retried %d dequeuedAtMs %d",
                 queue.data(), uuid_str.c_str(), task.type.c_str(),
                 task.payload.c_str(), state_str, task.maxRetry, task.retried,
                 task.dequeuedAtMs);
  } else if (s.type == ScheduleType::TimePoint) {
    redisCommand(c, "LPUSH cppq:%s:scheduled %s", queue.data(),
                 uuid_str.c_str());
    redisCommand(c,
                 "HSET cppq:%s:task:%s type %s payload %s state %s maxRetry %d "
                 "retried %d dequeuedAtMs %d schedule %lu",
                 queue.data(), uuid_str.c_str(), task.type.c_str(),
                 task.payload.c_str(), state_str, task.maxRetry, task.retried,
                 task.dequeuedAtMs,
                 std::chrono::duration_cast<std::chrono::milliseconds>(
                     s.time.time_since_epoch())
                     .count());
  } else if (s.type == ScheduleType::Cron) {
    redisCommand(c, "LPUSH cppq:%s:scheduled %s", queue.data(),
                 uuid_str.c_str());
    redisCommand(c,
                 "HSET cppq:%s:task:%s type %s payload %s state %s maxRetry %d "
                 "retried %d dequeuedAtMs %d cron %s",
                 queue.data(), uuid_str.c_str(), task.type.c_str(),
                 task.payload.c_str(), state_str, task.maxRetry, task.retried,
                 task.dequeuedAtMs, s.cron);
  }
  redisReply *reply = (redisReply *)redisCommand(c, "EXEC");

  if (!reply || reply->type == REDIS_REPLY_ERROR) {
    std::string error_msg =
        reply ? reply->str : "Failed to execute Redis command";
    if (reply) freeReplyObject(reply);
    throw CppqException(ErrorCode::EnqueueFailed, error_msg);
  }
  freeReplyObject(reply);
}

void enqueue(redisContext *c, Task &task, std::string_view queue) {
  return enqueue(c, task, queue,
                 ScheduleOptions{.cron = "", .type = ScheduleType::None});
}

void enqueueBatch(redisContext *c,
                  std::vector<std::reference_wrapper<Task>> &tasks,
                  std::string_view queue,
                  ScheduleOptions s = ScheduleOptions{
                      .cron = "", .type = ScheduleType::None}) {
  if (tasks.empty()) return;

  redisCommand(c, "MULTI");

  for (auto &task_ref : tasks) {
    Task &task = task_ref.get();

    if (s.type == ScheduleType::None)
      task.state = TaskState::Pending;
    else
      task.state = TaskState::Scheduled;

    std::string uuid_str = task.getUuidString();
    const char *state_str = stateToString(task.state);

    if (s.type == ScheduleType::None) {
      redisCommand(c, "LPUSH cppq:%s:pending %s", queue.data(),
                   uuid_str.c_str());
      redisCommand(
          c,
          "HSET cppq:%s:task:%s type %s payload %s state %s maxRetry %d "
          "retried %d dequeuedAtMs %d",
          queue.data(), uuid_str.c_str(), task.type.c_str(),
          task.payload.c_str(), state_str, task.maxRetry, task.retried,
          task.dequeuedAtMs);
    } else if (s.type == ScheduleType::TimePoint) {
      redisCommand(c, "LPUSH cppq:%s:scheduled %s", queue.data(),
                   uuid_str.c_str());
      redisCommand(
          c,
          "HSET cppq:%s:task:%s type %s payload %s state %s maxRetry %d "
          "retried %d dequeuedAtMs %d schedule %lu",
          queue.data(), uuid_str.c_str(), task.type.c_str(),
          task.payload.c_str(), state_str, task.maxRetry, task.retried,
          task.dequeuedAtMs,
          std::chrono::duration_cast<std::chrono::milliseconds>(
              s.time.time_since_epoch())
              .count());
    } else if (s.type == ScheduleType::Cron) {
      redisCommand(c, "LPUSH cppq:%s:scheduled %s", queue.data(),
                   uuid_str.c_str());
      redisCommand(
          c,
          "HSET cppq:%s:task:%s type %s payload %s state %s maxRetry %d "
          "retried %d dequeuedAtMs %d cron %s",
          queue.data(), uuid_str.c_str(), task.type.c_str(),
          task.payload.c_str(), state_str, task.maxRetry, task.retried,
          task.dequeuedAtMs, s.cron);
    }
  }

  redisReply *reply = (redisReply *)redisCommand(c, "EXEC");

  if (!reply || reply->type == REDIS_REPLY_ERROR) {
    std::string error_msg =
        reply ? reply->str : "Failed to execute Redis command";
    if (reply) freeReplyObject(reply);
    throw CppqException(ErrorCode::EnqueueFailed, error_msg);
  }
  freeReplyObject(reply);
}

std::optional<Task> dequeue(redisContext *c, std::string_view queue) {
  redisReply *reply = (redisReply *)redisCommand(
      c, "LRANGE cppq:%s:pending -1 -1", queue.data());
  if (!reply || reply->type != REDIS_REPLY_ARRAY) {
    if (reply) freeReplyObject(reply);
    return {};
  }
  if (reply->elements == 0) {
    freeReplyObject(reply);
    return {};
  }

  std::string uuid_str = reply->element[0]->str;
  freeReplyObject(reply);

  uint64_t dequeuedAtMs =
      std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::system_clock::now().time_since_epoch())
          .count();

  const char *active_state = stateToString(TaskState::Active);

  redisCommand(c, "MULTI");
  redisCommand(c, "LREM cppq:%s:pending 1 %s", queue.data(), uuid_str.c_str());
  redisCommand(c, "HGET cppq:%s:task:%s type", queue.data(), uuid_str.c_str());
  redisCommand(c, "HGET cppq:%s:task:%s payload", queue.data(),
               uuid_str.c_str());
  redisCommand(c, "HGET cppq:%s:task:%s state", queue.data(), uuid_str.c_str());
  redisCommand(c, "HGET cppq:%s:task:%s maxRetry", queue.data(),
               uuid_str.c_str());
  redisCommand(c, "HGET cppq:%s:task:%s retried", queue.data(),
               uuid_str.c_str());
  redisCommand(c, "HGET cppq:%s:task:%s dequeuedAtMs", queue.data(),
               uuid_str.c_str());
  redisCommand(c, "HSET cppq:%s:task:%s dequeuedAtMs %lu", queue.data(),
               uuid_str.c_str(), dequeuedAtMs);
  redisCommand(c, "HSET cppq:%s:task:%s state %s", queue.data(),
               uuid_str.c_str(), active_state);
  redisCommand(c, "LPUSH cppq:%s:active %s", queue.data(), uuid_str.c_str());
  reply = (redisReply *)redisCommand(c, "EXEC");

  if (!reply || reply->type != REDIS_REPLY_ARRAY || reply->elements != 10) {
    if (reply) freeReplyObject(reply);
    return {};
  }

  Task task(uuid_str, reply->element[1]->str, reply->element[2]->str,
            stateToString(TaskState::Active),
            strtoull(reply->element[4]->str, NULL, 0),
            strtoull(reply->element[5]->str, NULL, 0), dequeuedAtMs);

  freeReplyObject(reply);
  return task;
}

std::optional<Task> dequeueScheduled(redisContext *c, std::string_view queue,
                                     const char *getScheduledScriptSHA) {
  redisReply *reply = (redisReply *)redisCommand(
      c, "EVALSHA %s 0 %s", getScheduledScriptSHA, queue.data());
  if (!reply || reply->type != REDIS_REPLY_STRING) {
    if (reply) freeReplyObject(reply);
    return {};
  }

  std::string uuid_str = reply->str;
  freeReplyObject(reply);

  uint64_t dequeuedAtMs =
      std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::system_clock::now().time_since_epoch())
          .count();

  const char *active_state = stateToString(TaskState::Active);

  redisCommand(c, "MULTI");
  redisCommand(c, "LREM cppq:%s:scheduled 1 %s", queue.data(),
               uuid_str.c_str());
  redisCommand(c, "HGET cppq:%s:task:%s type", queue.data(), uuid_str.c_str());
  redisCommand(c, "HGET cppq:%s:task:%s payload", queue.data(),
               uuid_str.c_str());
  redisCommand(c, "HGET cppq:%s:task:%s state", queue.data(), uuid_str.c_str());
  redisCommand(c, "HGET cppq:%s:task:%s maxRetry", queue.data(),
               uuid_str.c_str());
  redisCommand(c, "HGET cppq:%s:task:%s retried", queue.data(),
               uuid_str.c_str());
  redisCommand(c, "HGET cppq:%s:task:%s dequeuedAtMs", queue.data(),
               uuid_str.c_str());
  redisCommand(c, "HGET cppq:%s:task:%s schedule", queue.data(),
               uuid_str.c_str());
  redisCommand(c, "HSET cppq:%s:task:%s dequeuedAtMs %lu", queue.data(),
               uuid_str.c_str(), dequeuedAtMs);
  redisCommand(c, "HSET cppq:%s:task:%s state %s", queue.data(),
               uuid_str.c_str(), active_state);
  redisCommand(c, "LPUSH cppq:%s:active %s", queue.data(), uuid_str.c_str());
  reply = (redisReply *)redisCommand(c, "EXEC");

  if (!reply || reply->type != REDIS_REPLY_ARRAY || reply->elements != 11) {
    if (reply) freeReplyObject(reply);
    return {};
  }

  Task task(uuid_str, reply->element[1]->str, reply->element[2]->str,
            stateToString(TaskState::Active),
            strtoull(reply->element[4]->str, NULL, 0),
            strtoull(reply->element[5]->str, NULL, 0), dequeuedAtMs,
            strtoull(reply->element[6]->str, NULL, 0));

  freeReplyObject(reply);
  return task;
}

void taskRunner(redisOptions redisOpts, Task task, std::string queue) {
  try {
    RedisConnection conn(redisOpts);

    Handler handler = handlers[task.type];
    if (!handler) {
      throw CppqException(ErrorCode::TaskNotFound,
                          "No handler registered for task type");
    }

    std::string uuid_str = task.getUuidString();

    try {
      handler(task);
    } catch (const std::exception &e) {
      task.retried++;
      const char *state_str = stateToString(task.state);

      redisCommand(conn.get(), "MULTI");
      redisCommand(conn.get(), "LREM cppq:%s:active 1 %s", queue.data(),
                   uuid_str.c_str());
      redisCommand(conn.get(), "HSET cppq:%s:task:%s retried %d", queue.data(),
                   uuid_str.c_str(), task.retried);
      if (task.retried >= task.maxRetry) {
        task.state = TaskState::Failed;
        state_str = stateToString(task.state);
        redisCommand(conn.get(), "HSET cppq:%s:task:%s state %s", queue.data(),
                     uuid_str.c_str(), state_str);
        redisCommand(conn.get(), "LPUSH cppq:%s:failed %s", queue.data(),
                     uuid_str.c_str());
      } else {
        task.state = TaskState::Pending;
        state_str = stateToString(task.state);
        redisCommand(conn.get(), "HSET cppq:%s:task:%s state %s", queue.data(),
                     uuid_str.c_str(), state_str);
        redisCommand(conn.get(), "LPUSH cppq:%s:pending %s", queue.data(),
                     uuid_str.c_str());
      }
      redisCommand(conn.get(), "EXEC");
      return;
    }

    task.state = TaskState::Completed;
    const char *state_str = stateToString(task.state);

    redisCommand(conn.get(), "MULTI");
    redisCommand(conn.get(), "LREM cppq:%s:active 1 %s", queue.data(),
                 uuid_str.c_str());
    redisCommand(conn.get(), "HSET cppq:%s:task:%s state %s", queue.data(),
                 uuid_str.c_str(), state_str);
    redisCommand(conn.get(), "HSET cppq:%s:task:%s result %s", queue.data(),
                 uuid_str.c_str(), task.result.c_str());
    redisCommand(conn.get(), "LPUSH cppq:%s:completed %s", queue.data(),
                 uuid_str.c_str());
    redisCommand(conn.get(), "EXEC");
  } catch (const CppqException &e) {
    std::cerr << "Task runner error: " << e.what() << std::endl;
  }
}

void recovery(redisOptions redisOpts, std::map<std::string, int> queues,
              uint64_t timeoutMs, uint64_t checkEveryMs) {
  redisContext *c = redisConnectWithOptions(&redisOpts);
  if (c == NULL || c->err) {
    std::cerr << "Failed to connect to Redis" << std::endl;
    return;
  }

  // TODO: Consider incrementing `retried` on recovery
  while (true) {
    std::this_thread::sleep_for(std::chrono::milliseconds(checkEveryMs));
    for (std::map<std::string, int>::iterator it = queues.begin();
         it != queues.end(); it++) {
      redisReply *reply = (redisReply *)redisCommand(
          c, "LRANGE cppq:%s:active 0 -1", it->first.c_str());
      for (size_t i = 0; i < reply->elements; i++) {
        std::string uuid = reply->element[i]->str;
        redisReply *dequeuedAtMsReply =
            (redisReply *)redisCommand(c, "HGET cppq:%s:task:%s dequeuedAtMs",
                                       it->first.c_str(), uuid.c_str());
        redisReply *scheduleReply =
            (redisReply *)redisCommand(c, "HGET cppq:%s:task:%s schedule",
                                       it->first.c_str(), uuid.c_str());
        uint64_t dequeuedAtMs = strtoull(dequeuedAtMsReply->str, NULL, 0);
        if (dequeuedAtMs + timeoutMs <
            static_cast<uint64_t>(
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::system_clock::now().time_since_epoch())
                    .count())) {
          redisCommand(c, "MULTI");
          redisCommand(c, "LREM cppq:%s:active 1 %s", it->first.c_str(),
                       uuid.c_str());
          redisCommand(c, "HSET cppq:%s:task:%s state %s", it->first.c_str(),
                       uuid.c_str(), stateToString(TaskState::Pending));
          if (scheduleReply->type == REDIS_REPLY_NIL)
            redisCommand(c, "LPUSH cppq:%s:pending %s", it->first.c_str(),
                         uuid.c_str());
          else
            redisCommand(c, "LPUSH cppq:%s:scheduled %s", it->first.c_str(),
                         uuid.c_str());
          redisCommand(c, "EXEC");
        }
      }
    }
  }
}

void pause(redisContext *c, std::string_view queue) {
  redisCommand(c, "SADD cppq:queues:paused %s", queue.data());
}

void unpause(redisContext *c, std::string_view queue) {
  redisCommand(c, "SREM cppq:queues:paused %s", queue.data());
}

bool isPaused(redisContext *c, std::string_view queue) noexcept {
  redisReply *reply =
      (redisReply *)redisCommand(c, "SMEMBERS cppq:queues:paused");
  if (!reply) return false;

  bool paused = false;
  for (size_t i = 0; i < reply->elements; i++) {
    if (queue == reply->element[i]->str) {
      paused = true;
      break;
    }
  }
  freeReplyObject(reply);
  return paused;
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

void runServer(redisOptions redisOpts, std::map<std::string, int> queues,
               uint64_t recoveryTimeoutSecond) {
  redisContext *c = redisConnectWithOptions(&redisOpts);
  if (c == NULL || c->err) {
    std::cerr << "Failed to connect to Redis" << std::endl;
    return;
  }

  redisReply *reply =
      (redisReply *)redisCommand(c, "SCRIPT LOAD %s", getScheduledScript);
  char *getScheduledScriptSHA = reply->str;

  std::vector<std::pair<std::string, int>> queuesVector;
  for (auto &it : queues) queuesVector.push_back(it);
  sort(
      queuesVector.begin(), queuesVector.end(),
      [](std::pair<std::string, int> const &a,
         std::pair<std::string, int> const &b) { return a.second > b.second; });

  for (auto it = queuesVector.begin(); it != queuesVector.end(); it++)
    redisCommand(c, "SADD cppq:queues %s:%d", it->first.c_str(), it->second);

  thread_pool pool;
  pool.push_task(recovery, redisOpts, queues, recoveryTimeoutSecond * 1000,
                 10000);

  while (true) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    for (std::vector<std::pair<std::string, int>>::iterator it =
             queuesVector.begin();
         it != queuesVector.end(); it++) {
      if (isPaused(c, it->first)) continue;
      std::optional<Task> task;
      task = dequeueScheduled(c, it->first, getScheduledScriptSHA);
      if (!task.has_value()) task = dequeue(c, it->first);
      if (task.has_value()) {
        pool.push_task(taskRunner, redisOpts, task.value(), it->first);
        break;
      }
    }
  }
}
}  // namespace cppq
