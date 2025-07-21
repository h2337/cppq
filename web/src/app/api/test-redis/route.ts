import { NextResponse } from 'next/server';
import { getRedisClient } from '@/lib/redis-singleton';
import { getBaseQueueName } from '@/lib/queue-utils';

export async function GET() {
  try {
    const client = await getRedisClient();
    if (!client) {
      return NextResponse.json({ error: 'Redis not connected' }, { status: 500 });
    }
    
    // Get all queues
    const queues = await client.sMembers('cppq:queues');
    
    const results: Record<string, {
      fullQueueName: string;
      baseQueueName: string;
      pendingKey: string;
      llen: number;
      lrangeFirstItem: string | null;
      exists: number;
      type: string;
      stats: {
        pending: number;
        scheduled: number;
        active: number;
        completed: number;
        failed: number;
      };
    }> = {};
    
    for (const queue of queues) {
      // Extract base queue name for Redis keys
      const baseQueue = getBaseQueueName(queue);
      const pendingKey = `cppq:${baseQueue}:pending`;
      
      const [
        llenResult,
        lrangeResult,
        existsResult,
        typeResult
      ] = await Promise.all([
        client.lLen(pendingKey),
        client.lRange(pendingKey, 0, 0),
        client.exists(pendingKey),
        client.type(pendingKey)
      ]);
      
      results[queue] = {
        fullQueueName: queue,
        baseQueueName: baseQueue,
        pendingKey,
        llen: llenResult,
        lrangeFirstItem: lrangeResult[0] || null,
        exists: existsResult,
        type: typeResult,
        // Also get stats using our function
        stats: {
          pending: await client.lLen(`cppq:${baseQueue}:pending`),
          scheduled: await client.lLen(`cppq:${baseQueue}:scheduled`),
          active: await client.lLen(`cppq:${baseQueue}:active`),
          completed: await client.lLen(`cppq:${baseQueue}:completed`),
          failed: await client.lLen(`cppq:${baseQueue}:failed`)
        }
      };
    }
    
    return NextResponse.json({
      queues,
      results,
      timestamp: new Date().toISOString()
    });
  } catch (error) {
    return NextResponse.json({ 
      error: error instanceof Error ? error.message : 'Failed to test Redis' 
    }, { status: 500 });
  }
}