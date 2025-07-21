import { NextResponse } from 'next/server';
import { getQueues } from '@/lib/redis';

export async function GET() {
  try {
    const queues = await getQueues();
    return NextResponse.json(queues);
  } catch (error) {
    return NextResponse.json({ 
      error: error instanceof Error ? error.message : 'Failed to fetch queues' 
    }, { status: 500 });
  }
}