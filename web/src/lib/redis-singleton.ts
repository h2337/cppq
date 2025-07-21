import { createClient } from 'redis';

export type RedisClient = ReturnType<typeof createClient>;

declare global {
  var redis: RedisClient | undefined;
  var redisUrl: string | undefined;
}

export async function getRedisClient(): Promise<RedisClient | null> {
  if (!global.redisUrl) {
    return null;
  }

  if (!global.redis) {
    global.redis = createClient({
      url: global.redisUrl
    });
    
    global.redis.on('error', (err) => {
      console.error('Redis Client Error', err);
    });
    
    await global.redis.connect();
  }
  
  // Check if still connected
  if (!global.redis.isReady) {
    try {
      await global.redis.connect();
    } catch (err) {
      console.error('Failed to reconnect to Redis:', err);
      return null;
    }
  }
  
  return global.redis;
}

export async function connectRedis(uri: string): Promise<void> {
  // Store the URL globally
  global.redisUrl = uri;
  
  // Disconnect existing client if any
  if (global.redis) {
    await global.redis.disconnect();
    global.redis = undefined;
  }
  
  // Create new connection
  const client = await getRedisClient();
  if (!client) {
    throw new Error('Failed to connect to Redis');
  }
}

export async function disconnectRedis(): Promise<void> {
  if (global.redis) {
    await global.redis.disconnect();
    global.redis = undefined;
    global.redisUrl = undefined;
  }
}