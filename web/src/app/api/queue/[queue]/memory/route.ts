import { NextRequest, NextResponse } from 'next/server';
import { getQueueMemory } from '@/lib/redis';

export async function GET(
  request: NextRequest,
  { params }: { params: Promise<{ queue: string }> }
) {
  try {
    const { queue } = await params;
    const memory = await getQueueMemory(queue);
    return NextResponse.json({ memory });
  } catch (error) {
    return NextResponse.json({ 
      error: error instanceof Error ? error.message : 'Failed to fetch queue memory' 
    }, { status: 500 });
  }
}