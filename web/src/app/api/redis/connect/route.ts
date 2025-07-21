import { NextRequest, NextResponse } from 'next/server';
import { connectRedis } from '@/lib/redis';
import { getRedisClient } from '@/lib/redis-singleton';

export async function POST(request: NextRequest) {
  try {
    const { uri } = await request.json();
    
    if (!uri) {
      return NextResponse.json({ error: 'Redis URI is required' }, { status: 400 });
    }
    
    await connectRedis(uri);
    
    return NextResponse.json({ connected: true });
  } catch (error) {
    return NextResponse.json({ 
      connected: false, 
      error: error instanceof Error ? error.message : 'Failed to connect to Redis' 
    }, { status: 500 });
  }
}

export async function GET() {
  const client = await getRedisClient();
  const connected = client !== null && client.isReady;
  
  return NextResponse.json({ connected });
}