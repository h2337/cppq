import { NextRequest, NextResponse } from 'next/server';
import { getRedisClient } from '@/lib/redis-singleton';
import { getBaseQueueName } from '@/lib/queue-utils';

export async function GET(
  request: NextRequest,
  { params }: { params: Promise<{ queue: string }> }
) {
  try {
    const sessionId = request.cookies.get('cppq_session')?.value;
    if (!sessionId) {
      return NextResponse.json({ error: 'Redis not connected' }, { status: 500 });
    }

    const { queue } = await params;
    const baseQueue = getBaseQueueName(queue);
    const client = await getRedisClient(sessionId);
    
    if (!client) {
      return NextResponse.json({ error: 'Redis not connected' }, { status: 500 });
    }
    
    // Get raw data from Redis
    const [
      pendingKey,
      pendingCount,
      pendingItems,
      allQueues,
      queueExists
    ] = await Promise.all([
      Promise.resolve(`cppq:${baseQueue}:pending`),
      client.lLen(`cppq:${baseQueue}:pending`),
      client.lRange(`cppq:${baseQueue}:pending`, 0, 10),
      client.sMembers('cppq:queues'),
      client.sIsMember('cppq:queues', queue) // Check with full queue name
    ]);
    
    return NextResponse.json({
      queue,
      baseQueue,
      pendingKey,
      pendingCount,
      pendingItems: pendingItems.slice(0, 5), // First 5 items
      allQueues,
      queueExists,
      debug: {
        clientConnected: client.isReady,
        keyPattern: `cppq:${baseQueue}:*`,
        fullQueueName: queue,
        baseQueueName: baseQueue
      }
    });
  } catch (error) {
    return NextResponse.json({ 
      error: error instanceof Error ? error.message : 'Failed to debug queue' 
    }, { status: 500 });
  }
}
