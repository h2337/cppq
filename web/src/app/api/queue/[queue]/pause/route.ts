import { NextRequest, NextResponse } from 'next/server';
import { pauseQueue } from '@/lib/redis';

export async function POST(
  request: NextRequest,
  { params }: { params: Promise<{ queue: string }> }
) {
  try {
    const { queue } = await params;
    await pauseQueue(queue);
    return NextResponse.json({ success: true });
  } catch (error) {
    return NextResponse.json({ 
      error: error instanceof Error ? error.message : 'Failed to pause queue' 
    }, { status: 500 });
  }
}