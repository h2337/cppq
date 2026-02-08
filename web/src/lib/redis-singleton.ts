import { createClient } from 'redis';

export type RedisClient = ReturnType<typeof createClient>;

interface RedisSession {
  uri: string;
  client: RedisClient;
}

declare global {
  var redisSessions: Map<string, RedisSession> | undefined;
}

function getSessionStore(): Map<string, RedisSession> {
  if (!global.redisSessions) {
    global.redisSessions = new Map<string, RedisSession>();
  }
  return global.redisSessions;
}

async function closeClient(client: RedisClient): Promise<void> {
  if (!client.isOpen) {
    return;
  }

  try {
    await client.quit();
  } catch {
    try {
      client.disconnect();
    } catch {
      // Best-effort cleanup; ignore disconnect errors.
    }
  }
}

export async function getRedisClient(sessionId: string): Promise<RedisClient | null> {
  if (!sessionId) {
    return null;
  }

  const session = getSessionStore().get(sessionId);
  if (!session) {
    return null;
  }

  if (!session.client.isReady) {
    try {
      if (!session.client.isOpen) {
        await session.client.connect();
      } else {
        await session.client.ping();
      }
    } catch (err) {
      console.error(`Failed to reconnect Redis for session ${sessionId}:`, err);
      return null;
    }
  }

  return session.client;
}

export async function connectRedis(sessionId: string, uri: string): Promise<void> {
  if (!sessionId) {
    throw new Error('Missing session identifier');
  }

  const sessions = getSessionStore();
  const existing = sessions.get(sessionId);
  if (existing && existing.uri === uri) {
    if (!existing.client.isReady && !existing.client.isOpen) {
      await existing.client.connect();
    }
    return;
  }

  if (existing) {
    await closeClient(existing.client);
    sessions.delete(sessionId);
  }

  const client = createClient({ url: uri });
  client.on('error', (err) => {
    console.error(`Redis Client Error [session=${sessionId}]`, err);
  });

  await client.connect();
  sessions.set(sessionId, { uri, client });
}

export async function disconnectRedis(sessionId: string): Promise<void> {
  if (!sessionId) {
    return;
  }

  const sessions = getSessionStore();
  const existing = sessions.get(sessionId);
  if (!existing) {
    return;
  }

  await closeClient(existing.client);
  sessions.delete(sessionId);
}
