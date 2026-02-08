import { NextRequest, NextResponse } from 'next/server';
import { getQueueStats } from '@/lib/redis';

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
    const stats = await getQueueStats(queue, sessionId);
    return NextResponse.json(stats);
  } catch (error) {
    return NextResponse.json({ 
      error: error instanceof Error ? error.message : 'Failed to fetch queue stats' 
    }, { status: 500 });
  }
}
