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
#include <shared_mutex>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace cppq {
inline constexpr char enqueueScript[] = R"DOC(
    local queue = ARGV[1]
    local uuid = ARGV[2]
    local type = ARGV[3]
    local payload = ARGV[4]
    local state = ARGV[5]
    local maxRetry = ARGV[6]
    local retried = ARGV[7]
    local dequeuedAtMs = ARGV[8]
    local scheduleType = ARGV[9]
    local scheduleValue = ARGV[10]

    if scheduleType == 'none' then
        redis.call('LPUSH', 'cppq:' .. queue .. ':pending', uuid)
        redis.call('HSET', 'cppq:' .. queue .. ':task:' .. uuid,
            'type', type, 'payload', payload, 'state', state,
            'maxRetry', maxRetry, 'retried', retried, 'dequeuedAtMs', dequeuedAtMs)
    elseif scheduleType == 'time' then
        redis.call('LPUSH', 'cppq:' .. queue .. ':scheduled', uuid)
        redis.call('HSET', 'cppq:' .. queue .. ':task:' .. uuid,
            'type', type, 'payload', payload, 'state', state,
            'maxRetry', maxRetry, 'retried', retried, 'dequeuedAtMs', dequeuedAtMs,
            'schedule', scheduleValue)
    elseif scheduleType == 'cron' then
        redis.call('LPUSH', 'cppq:' .. queue .. ':scheduled', uuid)
        redis.call('HSET', 'cppq:' .. queue .. ':task:' .. uuid,
            'type', type, 'payload', payload, 'state', state,
            'maxRetry', maxRetry, 'retried', retried, 'dequeuedAtMs', dequeuedAtMs,
            'cron', scheduleValue)
    end
    return 'OK'
)DOC";

inline constexpr char dequeueScript[] = R"DOC(
    local queue = ARGV[1]
    local dequeuedAtMs = ARGV[2]

    local pending = redis.call('LRANGE', 'cppq:' .. queue .. ':pending', -1, -1)
    if #pending == 0 then
        return nil
    end

    local uuid = pending[1]
    redis.call('LREM', 'cppq:' .. queue .. ':pending', 1, uuid)

    local task = redis.call('HGETALL', 'cppq:' .. queue .. ':task:' .. uuid)
    redis.call('HSET', 'cppq:' .. queue .. ':task:' .. uuid, 'dequeuedAtMs', dequeuedAtMs, 'state', 'Active')
    redis.call('LPUSH', 'cppq:' .. queue .. ':active', uuid)

    return {uuid, task}
)DOC";

inline constexpr char dequeueScheduledScript[] = R"DOC(
    local queue = ARGV[1]
    local dequeuedAtMs = ARGV[2]
    local getScheduledSHA = ARGV[3]

    local uuid = redis.call('EVALSHA', getScheduledSHA, 0, queue)
    if not uuid then
        return nil
    end

    redis.call('LREM', 'cppq:' .. queue .. ':scheduled', 1, uuid)

    local task = redis.call('HGETALL', 'cppq:' .. queue .. ':task:' .. uuid)
    redis.call('HSET', 'cppq:' .. queue .. ':task:' .. uuid, 'dequeuedAtMs', dequeuedAtMs, 'state', 'Active')
    redis.call('LPUSH', 'cppq:' .. queue .. ':active', uuid)

    return {uuid, task}
)DOC";

inline constexpr char taskSuccessScript[] = R"DOC(
    local queue = ARGV[1]
    local uuid = ARGV[2]
    local result = ARGV[3]

    redis.call('LREM', 'cppq:' .. queue .. ':active', 1, uuid)
    redis.call('HSET', 'cppq:' .. queue .. ':task:' .. uuid, 'state', 'Completed', 'result', result)
    redis.call('LPUSH', 'cppq:' .. queue .. ':completed', uuid)
    return 'OK'
)DOC";

inline constexpr char taskFailureScript[] = R"DOC(
    local queue = ARGV[1]
    local uuid = ARGV[2]
    local retried = ARGV[3]
    local maxRetry = ARGV[4]

    redis.call('LREM', 'cppq:' .. queue .. ':active', 1, uuid)
    redis.call('HSET', 'cppq:' .. queue .. ':task:' .. uuid, 'retried', retried)

    if tonumber(retried) >= tonumber(maxRetry) then
        redis.call('HSET', 'cppq:' .. queue .. ':task:' .. uuid, 'state', 'Failed')
        redis.call('LPUSH', 'cppq:' .. queue .. ':failed', uuid)
    else
        redis.call('HSET', 'cppq:' .. queue .. ':task:' .. uuid, 'state', 'Pending')
        redis.call('LPUSH', 'cppq:' .. queue .. ':pending', uuid)
    end
    return 'OK'
)DOC";

inline constexpr char recoveryScript[] = R"DOC(
    local queue = ARGV[1]
    local uuid = ARGV[2]
    local hasSchedule = ARGV[3]

    redis.call('LREM', 'cppq:' .. queue .. ':active', 1, uuid)
    redis.call('HSET', 'cppq:' .. queue .. ':task:' .. uuid, 'state', 'Pending')

    if hasSchedule == '0' then
        redis.call('LPUSH', 'cppq:' .. queue .. ':pending', uuid)
    else
        redis.call('LPUSH', 'cppq:' .. queue .. ':scheduled', uuid)
    end
    return 'OK'
)DOC";

inline constexpr char getScheduledScript[] = R"DOC(
    local timeCall = redis.call('time')
    local time = timeCall[1] .. timeCall[2]
    local scheduled = redis.call('LRANGE',  'cppq:' .. ARGV[1] .. ':scheduled', 0, -1)
    for _, key in ipairs(scheduled) do
      if (time > redis.call('HGET', 'cppq:' .. ARGV[1] .. ':task:' .. key, 'schedule')) then
        return key
      end
    end)DOC";

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
        schedule(0) {}

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
        cron(std::move(cron)) {}

  Task(const Task &) = default;
  Task(Task &&) = default;
  Task &operator=(const Task &) = default;
  Task &operator=(Task &&) = default;

  const UUID &getUuid() const noexcept { return uuid_; }
  std::string getUuidString() const { return uuid_.toString(); }

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
};

using Handler = void (*)(Task &);
namespace detail {
struct RedisReplyDeleter {
  void operator()(redisReply *reply) const noexcept {
    if (reply) freeReplyObject(reply);
  }
};

using RedisReplyPtr = std::unique_ptr<redisReply, RedisReplyDeleter>;

struct ScriptCache {
  std::once_flag flag;
  std::string sha;
};

struct HandlerRegistry {
  std::unordered_map<std::string, Handler> handlers;
  std::shared_mutex mutex;
};

inline HandlerRegistry &handlerRegistry() {
  static HandlerRegistry instance;
  return instance;
}

inline std::optional<Handler> getHandler(std::string_view type) {
  auto &registry = handlerRegistry();
  std::shared_lock<std::shared_mutex> lock(registry.mutex);
  auto it = registry.handlers.find(std::string(type));
  if (it == registry.handlers.end()) return std::nullopt;
  return it->second;
}

inline const std::string &ensureScriptLoaded(redisContext *c,
                                             const char *script,
                                             ScriptCache &cache, ErrorCode code,
                                             std::string_view context) {
  std::call_once(cache.flag, [&]() {
    RedisReplyPtr reply(
        static_cast<redisReply *>(redisCommand(c, "SCRIPT LOAD %s", script)));
    if (!reply || reply->type != REDIS_REPLY_STRING) {
      std::string error_msg(context);
      if (reply && reply->str) {
        error_msg.append(": ").append(reply->str);
      }
      throw CppqException(code, error_msg);
    }
    cache.sha = reply->str;
  });

  if (cache.sha.empty()) {
    throw CppqException(code, std::string(context));
  }

  return cache.sha;
}

inline ScriptCache &enqueueScriptCache() {
  static ScriptCache cache;
  return cache;
}

inline ScriptCache &dequeueScriptCache() {
  static ScriptCache cache;
  return cache;
}

inline ScriptCache &dequeueScheduledScriptCache() {
  static ScriptCache cache;
  return cache;
}

inline ScriptCache &taskSuccessScriptCache() {
  static ScriptCache cache;
  return cache;
}

inline ScriptCache &taskFailureScriptCache() {
  static ScriptCache cache;
  return cache;
}

inline ScriptCache &recoveryScriptCache() {
  static ScriptCache cache;
  return cache;
}

inline ScriptCache &getScheduledScriptCache() {
  static ScriptCache cache;
  return cache;
}
}  // namespace detail

inline void registerHandler(std::string_view type, Handler handler) {
  auto &registry = detail::handlerRegistry();
  std::unique_lock<std::shared_mutex> lock(registry.mutex);
  registry.handlers[std::string(type)] = handler;
}

enum class ScheduleType { None, Cron, TimePoint };

struct ScheduleOptions {
  ScheduleType type{ScheduleType::None};
  std::optional<std::chrono::system_clock::time_point> timePoint;
  std::optional<std::string> cronExpression;
};

inline ScheduleOptions scheduleOptions(
    std::chrono::system_clock::time_point t) noexcept {
  ScheduleOptions options;
  options.type = ScheduleType::TimePoint;
  options.timePoint = t;
  return options;
}

inline ScheduleOptions scheduleOptions(std::string_view c) {
  ScheduleOptions options;
  options.type = ScheduleType::Cron;
  options.cronExpression = std::string(c);
  return options;
}

void enqueue(redisContext *c, Task &task, std::string_view queue,
             ScheduleOptions s) {
  task.state =
      s.type == ScheduleType::None ? TaskState::Pending : TaskState::Scheduled;

  std::string uuid_str = task.getUuidString();
  const char *state_str = stateToString(task.state);

  std::string scheduleType;
  std::string scheduleValue;

  switch (s.type) {
    case ScheduleType::None:
      scheduleType = "none";
      break;
    case ScheduleType::TimePoint: {
      if (!s.timePoint.has_value()) {
        throw CppqException(
            ErrorCode::InvalidTask,
            "ScheduleOptions missing time point for time-based schedule");
      }
      scheduleType = "time";
      scheduleValue =
          std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(
                             s.timePoint.value().time_since_epoch())
                             .count());
      break;
    }
    case ScheduleType::Cron: {
      if (!s.cronExpression.has_value()) {
        throw CppqException(ErrorCode::InvalidTask,
                            "ScheduleOptions missing cron expression");
      }
      scheduleType = "cron";
      scheduleValue = s.cronExpression.value();
      break;
    }
  }

  const auto &enqueueScriptSHA = detail::ensureScriptLoaded(
      c, enqueueScript, detail::enqueueScriptCache(), ErrorCode::EnqueueFailed,
      "Failed to load enqueue script");

  const auto maxRetryStr = std::to_string(task.maxRetry);
  const auto retriedStr = std::to_string(task.retried);
  const auto dequeuedAtMsStr = std::to_string(task.dequeuedAtMs);

  detail::RedisReplyPtr reply(static_cast<redisReply *>(redisCommand(
      c, "EVALSHA %s 0 %s %s %s %s %s %s %s %s %s %s", enqueueScriptSHA.c_str(),
      queue.data(), uuid_str.c_str(), task.type.c_str(), task.payload.c_str(),
      state_str, maxRetryStr.c_str(), retriedStr.c_str(),
      dequeuedAtMsStr.c_str(), scheduleType.c_str(), scheduleValue.c_str())));

  if (!reply || reply->type == REDIS_REPLY_ERROR) {
    std::string error_msg =
        reply && reply->str ? reply->str : "Failed to execute Redis command";
    throw CppqException(ErrorCode::EnqueueFailed, error_msg);
  }
}

void enqueue(redisContext *c, Task &task, std::string_view queue) {
  return enqueue(c, task, queue, ScheduleOptions{});
}

void enqueueBatch(redisContext *c,
                  std::vector<std::reference_wrapper<Task>> &tasks,
                  std::string_view queue,
                  ScheduleOptions s = ScheduleOptions{}) {
  if (tasks.empty()) return;

  const auto &enqueueScriptSHA = detail::ensureScriptLoaded(
      c, enqueueScript, detail::enqueueScriptCache(), ErrorCode::EnqueueFailed,
      "Failed to load enqueue script");

  for (auto &task_ref : tasks) {
    Task &task = task_ref.get();

    task.state = s.type == ScheduleType::None ? TaskState::Pending
                                              : TaskState::Scheduled;

    std::string uuid_str = task.getUuidString();
    const char *state_str = stateToString(task.state);

    std::string scheduleType;
    std::string scheduleValue;

    switch (s.type) {
      case ScheduleType::None:
        scheduleType = "none";
        break;
      case ScheduleType::TimePoint: {
        if (!s.timePoint.has_value()) {
          throw CppqException(
              ErrorCode::InvalidTask,
              "ScheduleOptions missing time point for time-based schedule");
        }
        scheduleType = "time";
        scheduleValue = std::to_string(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                s.timePoint.value().time_since_epoch())
                .count());
        break;
      }
      case ScheduleType::Cron: {
        if (!s.cronExpression.has_value()) {
          throw CppqException(ErrorCode::InvalidTask,
                              "ScheduleOptions missing cron expression");
        }
        scheduleType = "cron";
        scheduleValue = s.cronExpression.value();
        break;
      }
    }

    const auto maxRetryStr = std::to_string(task.maxRetry);
    const auto retriedStr = std::to_string(task.retried);
    const auto dequeuedAtMsStr = std::to_string(task.dequeuedAtMs);

    detail::RedisReplyPtr reply(static_cast<redisReply *>(redisCommand(
        c, "EVALSHA %s 0 %s %s %s %s %s %s %s %s %s %s",
        enqueueScriptSHA.c_str(), queue.data(), uuid_str.c_str(),
        task.type.c_str(), task.payload.c_str(), state_str, maxRetryStr.c_str(),
        retriedStr.c_str(), dequeuedAtMsStr.c_str(), scheduleType.c_str(),
        scheduleValue.c_str())));

    if (!reply || reply->type == REDIS_REPLY_ERROR) {
      std::string error_msg =
          reply && reply->str ? reply->str : "Failed to execute Redis command";
      throw CppqException(ErrorCode::EnqueueFailed, error_msg);
    }
  }
}

std::optional<Task> dequeue(redisContext *c, std::string_view queue) {
  uint64_t dequeuedAtMs =
      std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::system_clock::now().time_since_epoch())
          .count();

  const auto &dequeueScriptSHA = detail::ensureScriptLoaded(
      c, dequeueScript, detail::dequeueScriptCache(), ErrorCode::DequeueFailed,
      "Failed to load dequeue script");

  const auto dequeuedAtMsStr = std::to_string(dequeuedAtMs);

  detail::RedisReplyPtr reply(static_cast<redisReply *>(
      redisCommand(c, "EVALSHA %s 0 %s %s", dequeueScriptSHA.c_str(),
                   queue.data(), dequeuedAtMsStr.c_str())));

  if (!reply || reply->type == REDIS_REPLY_NIL) {
    return {};
  }

  if (reply->type != REDIS_REPLY_ARRAY || reply->elements != 2) {
    return {};
  }

  std::string uuid_str = reply->element[0]->str;
  redisReply *taskData = reply->element[1];

  if (taskData->type != REDIS_REPLY_ARRAY || taskData->elements < 12) {
    return {};
  }

  std::unordered_map<std::string, std::string> taskMap;
  for (size_t i = 0; i < taskData->elements; i += 2) {
    if (i + 1 < taskData->elements) {
      taskMap[taskData->element[i]->str] = taskData->element[i + 1]->str;
    }
  }

  Task task(uuid_str, taskMap["type"], taskMap["payload"], "Active",
            strtoull(taskMap["maxRetry"].c_str(), NULL, 0),
            strtoull(taskMap["retried"].c_str(), NULL, 0), dequeuedAtMs);

  return task;
}

std::optional<Task> dequeueScheduled(redisContext *c, std::string_view queue,
                                     std::string_view getScheduledScriptSHA) {
  uint64_t dequeuedAtMs =
      std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::system_clock::now().time_since_epoch())
          .count();

  const auto &dequeueScheduledScriptSHA = detail::ensureScriptLoaded(
      c, dequeueScheduledScript, detail::dequeueScheduledScriptCache(),
      ErrorCode::DequeueFailed, "Failed to load dequeueScheduled script");

  const auto dequeuedAtMsStr = std::to_string(dequeuedAtMs);

  detail::RedisReplyPtr reply(static_cast<redisReply *>(redisCommand(
      c, "EVALSHA %s 0 %s %s %s", dequeueScheduledScriptSHA.c_str(),
      queue.data(), dequeuedAtMsStr.c_str(), getScheduledScriptSHA.data())));

  if (!reply || reply->type == REDIS_REPLY_NIL) {
    return {};
  }

  if (reply->type != REDIS_REPLY_ARRAY || reply->elements != 2) {
    return {};
  }

  std::string uuid_str = reply->element[0]->str;
  redisReply *taskData = reply->element[1];

  if (taskData->type != REDIS_REPLY_ARRAY || taskData->elements < 12) {
    return {};
  }

  std::unordered_map<std::string, std::string> taskMap;
  for (size_t i = 0; i < taskData->elements; i += 2) {
    if (i + 1 < taskData->elements) {
      taskMap[taskData->element[i]->str] = taskData->element[i + 1]->str;
    }
  }

  uint64_t schedule = 0;
  if (taskMap.find("schedule") != taskMap.end()) {
    schedule = strtoull(taskMap["schedule"].c_str(), NULL, 0);
  }

  Task task(uuid_str, taskMap["type"], taskMap["payload"], "Active",
            strtoull(taskMap["maxRetry"].c_str(), NULL, 0),
            strtoull(taskMap["retried"].c_str(), NULL, 0), dequeuedAtMs,
            schedule);

  return task;
}

void taskRunner(redisOptions redisOpts, Task task, std::string queue) {
  try {
    RedisConnection conn(redisOpts);

    auto handlerOpt = detail::getHandler(task.type);
    if (!handlerOpt) {
      throw CppqException(ErrorCode::TaskNotFound,
                          "No handler registered for task type");
    }
    Handler handler = *handlerOpt;

    std::string uuid_str = task.getUuidString();

    try {
      handler(task);
    } catch (const std::exception &e) {
      task.retried++;

      const auto &taskFailureScriptSHA = detail::ensureScriptLoaded(
          conn.get(), taskFailureScript, detail::taskFailureScriptCache(),
          ErrorCode::RedisError, "Failed to load task failure script");

      const auto retriedStr = std::to_string(task.retried);
      const auto maxRetryStr = std::to_string(task.maxRetry);

      detail::RedisReplyPtr reply(static_cast<redisReply *>(redisCommand(
          conn.get(), "EVALSHA %s 0 %s %s %s %s", taskFailureScriptSHA.c_str(),
          queue.data(), uuid_str.c_str(), retriedStr.c_str(),
          maxRetryStr.c_str())));

      if (reply && reply->type == REDIS_REPLY_ERROR) {
        throw CppqException(
            ErrorCode::RedisError,
            reply->str ? reply->str : "Failed to execute task failure script");
      }
      return;
    }

    task.state = TaskState::Completed;

    const auto &taskSuccessScriptSHA = detail::ensureScriptLoaded(
        conn.get(), taskSuccessScript, detail::taskSuccessScriptCache(),
        ErrorCode::RedisError, "Failed to load task success script");

    detail::RedisReplyPtr reply(static_cast<redisReply *>(redisCommand(
        conn.get(), "EVALSHA %s 0 %s %s %s", taskSuccessScriptSHA.c_str(),
        queue.data(), uuid_str.c_str(), task.result.c_str())));

    if (reply && reply->type == REDIS_REPLY_ERROR) {
      throw CppqException(
          ErrorCode::RedisError,
          reply->str ? reply->str : "Failed to execute task success script");
    }
  } catch (const CppqException &e) {
    std::cerr << "Task runner error: " << e.what() << std::endl;
  }
}

void recovery(redisOptions redisOpts, std::map<std::string, int> queues,
              uint64_t timeoutMs, uint64_t checkEveryMs) {
  std::unique_ptr<redisContext, decltype(&redisFree)> ctx(
      redisConnectWithOptions(&redisOpts), redisFree);
  if (!ctx || ctx->err) {
    std::cerr << "Failed to connect to Redis" << std::endl;
    return;
  }
  redisContext *c = ctx.get();

  const std::string *recoveryScriptSHA = nullptr;
  try {
    recoveryScriptSHA = &detail::ensureScriptLoaded(
        c, recoveryScript, detail::recoveryScriptCache(), ErrorCode::RedisError,
        "Failed to load recovery script");
  } catch (const CppqException &e) {
    std::cerr << e.what() << std::endl;
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
          const auto hasScheduleStr =
              scheduleReply->type == REDIS_REPLY_NIL ? "0" : "1";
          detail::RedisReplyPtr execReply(
              static_cast<redisReply *>(redisCommand(
                  c, "EVALSHA %s 0 %s %s %s", recoveryScriptSHA->c_str(),
                  it->first.c_str(), uuid.c_str(), hasScheduleStr)));
          if (execReply && execReply->type == REDIS_REPLY_ERROR) {
            std::cerr << "Recovery script error: "
                      << (execReply->str ? execReply->str : "unknown error")
                      << std::endl;
          }
        }
        freeReplyObject(dequeuedAtMsReply);
        freeReplyObject(scheduleReply);
      }
      freeReplyObject(reply);
    }
  }
}

void pause(redisContext *c, std::string_view queue) {
  detail::RedisReplyPtr(static_cast<redisReply *>(
      redisCommand(c, "SADD cppq:queues:paused %s", queue.data())));
}

void unpause(redisContext *c, std::string_view queue) {
  detail::RedisReplyPtr(static_cast<redisReply *>(
      redisCommand(c, "SREM cppq:queues:paused %s", queue.data())));
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

void runServer(redisOptions redisOpts, std::map<std::string, int> queues,
               uint64_t recoveryTimeoutSecond) {
  std::unique_ptr<redisContext, decltype(&redisFree)> ctx(
      redisConnectWithOptions(&redisOpts), redisFree);
  if (!ctx || ctx->err) {
    std::cerr << "Failed to connect to Redis" << std::endl;
    return;
  }
  redisContext *c = ctx.get();

  const std::string *getScheduledScriptSHA = nullptr;
  try {
    getScheduledScriptSHA = &detail::ensureScriptLoaded(
        c, getScheduledScript, detail::getScheduledScriptCache(),
        ErrorCode::RedisError, "Failed to load getScheduled script");
  } catch (const CppqException &e) {
    std::cerr << "runServer error: " << e.what() << std::endl;
    return;
  }
  const std::string &getScheduledScriptSHARef = *getScheduledScriptSHA;

  std::vector<std::pair<std::string, int>> queuesVector;
  for (auto &it : queues) queuesVector.push_back(it);
  sort(
      queuesVector.begin(), queuesVector.end(),
      [](std::pair<std::string, int> const &a,
         std::pair<std::string, int> const &b) { return a.second > b.second; });

  for (auto it = queuesVector.begin(); it != queuesVector.end(); it++)
    detail::RedisReplyPtr(static_cast<redisReply *>(redisCommand(
        c, "SADD cppq:queues %s:%d", it->first.c_str(), it->second)));

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
      task = dequeueScheduled(c, it->first, getScheduledScriptSHARef);
      if (!task.has_value()) task = dequeue(c, it->first);
      if (task.has_value()) {
        pool.push_task(taskRunner, redisOpts, task.value(), it->first);
        break;
      }
    }
  }
}
}  // namespace cppq
