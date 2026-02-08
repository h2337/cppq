import {
  getRedisClient as getSessionRedisClient,
  connectRedis,
  disconnectRedis,
  type RedisClient,
} from './redis-singleton';
import { getBaseQueueName } from './queue-utils';

export type { RedisClient };
export { connectRedis, disconnectRedis };

export interface QueueStats {
  pending: number;
  scheduled: number;
  active: number;
  completed: number;
  failed: number;
  paused: boolean;
}

export interface Task {
  uuid: string;
  type: string;
  payload: string;
  max_retry: string;
  retry_count: string;
  schedule_time?: string;
  cron?: string;
  dequeue_time?: string;
  result?: string;
}

export async function getQueues(sessionId: string): Promise<string[]> {
  const client = await getSessionRedisClient(sessionId);
  if (!client) throw new Error('Redis not connected');
  return await client.sMembers('cppq:queues');
}

export async function getQueueStats(
  queue: string,
  sessionId: string,
): Promise<QueueStats> {
  const client = await getSessionRedisClient(sessionId);
  if (!client) throw new Error('Redis not connected');

  const baseQueue = getBaseQueueName(queue);
  const [pending, scheduled, active, completed, failed, isPaused] =
    await Promise.all([
      client.lLen(`cppq:${baseQueue}:pending`),
      client.lLen(`cppq:${baseQueue}:scheduled`),
      client.lLen(`cppq:${baseQueue}:active`),
      client.lLen(`cppq:${baseQueue}:completed`),
      client.lLen(`cppq:${baseQueue}:failed`),
      client.sIsMember('cppq:queues:paused', baseQueue),
    ]);

  return {
    pending: Number(pending) || 0,
    scheduled: Number(scheduled) || 0,
    active: Number(active) || 0,
    completed: Number(completed) || 0,
    failed: Number(failed) || 0,
    paused: Boolean(isPaused),
  };
}

export async function getQueueMemory(
  queue: string,
  sessionId: string,
): Promise<number> {
  const client = await getSessionRedisClient(sessionId);
  if (!client) throw new Error('Redis not connected');

  const baseQueue = getBaseQueueName(queue);
  const keyPattern = `cppq:${baseQueue}:*`;

  const keys: string[] = [];
  for await (const key of client.scanIterator({ MATCH: keyPattern, COUNT: 100 })) {
    keys.push(key as string);
  }

  if (keys.length === 0) {
    return 0;
  }

  const usages = await Promise.all(
    keys.map(async (key) => {
      try {
        return (await client.memoryUsage(key)) ?? 0;
      } catch {
        return 0;
      }
    }),
  );

  const totalBytes = usages.reduce((sum, value) => sum + Number(value || 0), 0);
  return Math.round(totalBytes / (1024 * 1024));
}

export async function pauseQueue(queue: string, sessionId: string): Promise<void> {
  const client = await getSessionRedisClient(sessionId);
  if (!client) throw new Error('Redis not connected');
  await client.sAdd('cppq:queues:paused', getBaseQueueName(queue));
}

export async function unpauseQueue(
  queue: string,
  sessionId: string,
): Promise<void> {
  const client = await getSessionRedisClient(sessionId);
  if (!client) throw new Error('Redis not connected');
  await client.sRem('cppq:queues:paused', getBaseQueueName(queue));
}

export async function getTasks(
  queue: string,
  state: string,
  sessionId: string,
): Promise<Task[]> {
  const client = await getSessionRedisClient(sessionId);
  if (!client) throw new Error('Redis not connected');

  const baseQueue = getBaseQueueName(queue);
  const taskIds = await client.lRange(`cppq:${baseQueue}:${state}`, 0, -1);
  const taskDataList = await Promise.all(
    taskIds.map((taskId) => client.hGetAll(`cppq:${baseQueue}:task:${taskId}`)),
  );

  const tasks: Task[] = [];
  for (let i = 0; i < taskIds.length; i++) {
    const taskId = taskIds[i];
    const taskData = taskDataList[i];
    if (!taskData || Object.keys(taskData).length === 0) {
      continue;
    }

    tasks.push({
      uuid: taskId,
      type: taskData.type || '',
      payload: taskData.payload || '',
      max_retry: taskData.maxRetry || '0',
      retry_count: taskData.retried || '0',
      schedule_time: taskData.schedule,
      cron: taskData.cron,
      dequeue_time: taskData.dequeuedAtMs,
      result: taskData.result,
    });
  }

  return tasks;
}
