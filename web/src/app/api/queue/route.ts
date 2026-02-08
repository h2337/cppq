import { NextRequest, NextResponse } from 'next/server';
import { getQueues } from '@/lib/redis';

export async function GET(request: NextRequest) {
  try {
    const sessionId = request.cookies.get('cppq_session')?.value;
    if (!sessionId) {
      return NextResponse.json({ error: 'Redis not connected' }, { status: 500 });
    }

    const queues = await getQueues(sessionId);
    return NextResponse.json(queues);
  } catch (error) {
    return NextResponse.json({ 
      error: error instanceof Error ? error.message : 'Failed to fetch queues' 
    }, { status: 500 });
  }
}
