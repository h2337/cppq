import { NextRequest, NextResponse } from 'next/server';
import { pauseQueue } from '@/lib/redis';

export async function POST(
  request: NextRequest,
  { params }: { params: Promise<{ queue: string }> }
) {
  try {
    const sessionId = request.cookies.get('cppq_session')?.value;
    if (!sessionId) {
      return NextResponse.json({ error: 'Redis not connected' }, { status: 500 });
    }

    const { queue } = await params;
    await pauseQueue(queue, sessionId);
    return NextResponse.json({ success: true });
  } catch (error) {
    return NextResponse.json({ 
      error: error instanceof Error ? error.message : 'Failed to pause queue' 
    }, { status: 500 });
  }
}
