import { NextRequest, NextResponse } from 'next/server';
import { connectRedis } from '@/lib/redis';
import { getRedisClient } from '@/lib/redis-singleton';
import { randomUUID } from 'crypto';

export async function POST(request: NextRequest) {
  try {
    const { uri } = await request.json();
    
    if (!uri) {
      return NextResponse.json({ error: 'Redis URI is required' }, { status: 400 });
    }

    const sessionId = request.cookies.get('cppq_session')?.value ?? randomUUID();
    await connectRedis(sessionId, uri);

    const response = NextResponse.json({ connected: true });
    response.cookies.set('cppq_session', sessionId, {
      httpOnly: true,
      sameSite: 'lax',
      path: '/',
      maxAge: 60 * 60 * 24 * 7,
    });
    return response;
  } catch (error) {
    return NextResponse.json({ 
      connected: false, 
      error: error instanceof Error ? error.message : 'Failed to connect to Redis' 
    }, { status: 500 });
  }
}

export async function GET(request: NextRequest) {
  const sessionId = request.cookies.get('cppq_session')?.value;
  const client = sessionId ? await getRedisClient(sessionId) : null;
  const connected = client !== null && client.isReady;
  
  return NextResponse.json({ connected });
}
