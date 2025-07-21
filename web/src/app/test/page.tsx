'use client';

import { useState, useEffect } from 'react';

export default function TestPage() {
  const [queueName, setQueueName] = useState('');
  const [debugData, setDebugData] = useState<{
    debug?: unknown;
    stats?: unknown;
    statsUrl?: string;
    error?: string;
  } | null>(null);
  const [loading, setLoading] = useState(false);
  const [availableQueues, setAvailableQueues] = useState<string[]>([]);

  useEffect(() => {
    // Fetch available queues
    fetch('/api/queue')
      .then(res => res.json())
      .then(queues => setAvailableQueues(queues))
      .catch(() => setAvailableQueues([]));
  }, []);

  const checkQueue = async () => {
    if (!queueName) return;
    
    setLoading(true);
    try {
      // Fetch both debug data and stats
      const [debugResponse, statsResponse] = await Promise.all([
        fetch(`/api/debug/queue/${encodeURIComponent(queueName)}`),
        fetch(`/api/queue/${encodeURIComponent(queueName)}/stats`)
      ]);
      
      const debugData = await debugResponse.json();
      const statsData = await statsResponse.json();
      
      setDebugData({
        debug: debugData,
        stats: statsData,
        statsUrl: `/api/queue/${encodeURIComponent(queueName)}/stats`
      });
    } catch (error) {
      setDebugData({ error: 'Failed to fetch debug data' });
    } finally {
      setLoading(false);
    }
  };

  return (
    <div className="min-h-screen bg-gray-50 dark:bg-gray-900 p-8">
      <div className="max-w-4xl mx-auto">
        <h1 className="text-2xl font-bold mb-6">Queue Debug Tool</h1>
        
        <div className="mb-4">
          <a 
            href="/api/test-redis" 
            target="_blank" 
            className="text-blue-600 hover:text-blue-800 underline"
          >
            View all Redis data →
          </a>
        </div>
        
        {availableQueues.length > 0 && (
          <div className="mb-4 p-4 bg-blue-50 dark:bg-blue-900/20 rounded-lg">
            <p className="text-sm font-medium text-blue-900 dark:text-blue-100 mb-2">
              Available queues:
            </p>
            <div className="flex flex-wrap gap-2">
              {availableQueues.map(q => (
                <button
                  key={q}
                  onClick={() => setQueueName(q)}
                  className="px-3 py-1 bg-blue-100 dark:bg-blue-800 text-blue-700 dark:text-blue-200 rounded hover:bg-blue-200 dark:hover:bg-blue-700 text-sm"
                >
                  {q}
                </button>
              ))}
            </div>
          </div>
        )}
        
        <div className="bg-white dark:bg-gray-800 rounded-lg shadow p-6">
          <div className="flex gap-4 mb-6">
            <input
              type="text"
              value={queueName}
              onChange={(e) => setQueueName(e.target.value)}
              placeholder="Enter queue name (e.g., high:20)"
              className="flex-1 px-4 py-2 border border-gray-300 dark:border-gray-600 rounded-md dark:bg-gray-700 dark:text-white"
            />
            <button
              onClick={checkQueue}
              disabled={loading}
              className="px-6 py-2 bg-blue-600 hover:bg-blue-700 text-white rounded-md disabled:bg-blue-400"
            >
              {loading ? 'Checking...' : 'Check Queue'}
            </button>
          </div>
          
          {debugData && (
            <pre className="bg-gray-100 dark:bg-gray-900 p-4 rounded overflow-auto text-sm">
              {JSON.stringify(debugData, null, 2)}
            </pre>
          )}
        </div>
      </div>
    </div>
  );
}