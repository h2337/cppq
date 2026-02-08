import { NextRequest, NextResponse } from 'next/server';
import { getTasks } from '@/lib/redis';

export async function GET(
  request: NextRequest,
  { params }: { params: Promise<{ queue: string; state: string }> }
) {
  try {
    const sessionId = request.cookies.get('cppq_session')?.value;
    if (!sessionId) {
      return NextResponse.json({ error: 'Redis not connected' }, { status: 500 });
    }

    const { queue, state } = await params;
    const tasks = await getTasks(queue, state, sessionId);
    return NextResponse.json(tasks);
  } catch (error) {
    return NextResponse.json({ 
      error: error instanceof Error ? error.message : 'Failed to fetch tasks' 
    }, { status: 500 });
  }
}
