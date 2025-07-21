import { 
  getRedisClient as getSingletonClient, 
  type RedisClient
} from './redis-singleton';
import { getBaseQueueName } from './queue-utils';

export type { RedisClient };
export { connectRedis, disconnectRedis } from './redis-singleton';

export function getRedisClient(): RedisClient | null {
  // This is a sync wrapper for compatibility
  // The actual client should be retrieved async in the API routes
  return global.redis || null;
}

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

export async function getQueues(): Promise<string[]> {
  const client = await getSingletonClient();
  if (!client) throw new Error('Redis not connected');
  return await client.sMembers('cppq:queues');
}

export async function getQueueStats(queue: string): Promise<QueueStats> {
  const client = await getSingletonClient();
  if (!client) throw new Error('Redis not connected');
  
  // Extract base queue name for Redis keys
  const baseQueue = getBaseQueueName(queue);
  
  const [pending, scheduled, active, completed, failed, isPaused] = await Promise.all([
    client.lLen(`cppq:${baseQueue}:pending`),
    client.lLen(`cppq:${baseQueue}:scheduled`),
    client.lLen(`cppq:${baseQueue}:active`),
    client.lLen(`cppq:${baseQueue}:completed`),
    client.lLen(`cppq:${baseQueue}:failed`),
    client.sIsMember('cppq:queues:paused', queue) // Use full name for paused check
  ]);
  
  // Ensure all counts are numbers
  return {
    pending: Number(pending) || 0,
    scheduled: Number(scheduled) || 0,
    active: Number(active) || 0,
    completed: Number(completed) || 0,
    failed: Number(failed) || 0,
    paused: Boolean(isPaused)
  };
}

export async function getQueueMemory(queue: string): Promise<number> {
  const client = await getSingletonClient();
  if (!client) throw new Error('Redis not connected');
  
  const info = await client.info('memory');
  const usedMemoryMatch = info.match(/used_memory:(\d+)/);
  const usedMemory = usedMemoryMatch ? parseInt(usedMemoryMatch[1]) : 0;
  
  // This is a simplified calculation - in production you might want to
  // calculate actual memory usage per queue
  const queues = await getQueues();
  const memoryPerQueue = usedMemory / queues.length;
  
  return Math.round(memoryPerQueue / (1024 * 1024)); // Convert to MB
}

export async function pauseQueue(queue: string): Promise<void> {
  const client = await getSingletonClient();
  if (!client) throw new Error('Redis not connected');
  await client.sAdd('cppq:queues:paused', queue);
}

export async function unpauseQueue(queue: string): Promise<void> {
  const client = await getSingletonClient();
  if (!client) throw new Error('Redis not connected');
  await client.sRem('cppq:queues:paused', queue);
}

export async function getTasks(queue: string, state: string): Promise<Task[]> {
  const client = await getSingletonClient();
  if (!client) throw new Error('Redis not connected');
  
  // Extract base queue name for Redis keys
  const baseQueue = getBaseQueueName(queue);
  
  const taskIds = await client.lRange(`cppq:${baseQueue}:${state}`, 0, -1);
  const tasks: Task[] = [];
  
  for (const taskId of taskIds) {
    const taskData = await client.hGetAll(`cppq:${baseQueue}:task:${taskId}`);
    if (taskData && Object.keys(taskData).length > 0) {
      tasks.push({
        uuid: taskId,
        type: taskData.type || '',
        payload: taskData.payload || '',
        max_retry: taskData.max_retry || '0',
        retry_count: taskData.retry_count || '0',
        schedule_time: taskData.schedule_time,
        cron: taskData.cron,
        dequeue_time: taskData.dequeue_time,
        result: taskData.result
      });
    }
  }
  
  return tasks;
}