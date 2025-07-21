import { NextRequest, NextResponse } from 'next/server';
import { getQueueStats } from '@/lib/redis';

export async function GET(
  request: NextRequest,
  { params }: { params: Promise<{ queue: string }> }
) {
  try {
    const { queue } = await params;
    const stats = await getQueueStats(queue);
    return NextResponse.json(stats);
  } catch (error) {
    return NextResponse.json({ 
      error: error instanceof Error ? error.message : 'Failed to fetch queue stats' 
    }, { status: 500 });
  }
}