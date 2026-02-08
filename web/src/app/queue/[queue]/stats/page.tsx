'use client';

import { useState, useEffect, use } from 'react';
import { useRouter } from 'next/navigation';
import Link from 'next/link';
import Header from '@/components/Header';
import QueueStatsChart from '@/components/QueueStats';
import { QueueStats } from '@/lib/redis';
import { getBaseQueueName } from '@/lib/queue-utils';

export default function QueueStatsPage({ 
  params 
}: { 
  params: Promise<{ queue: string }> 
}) {
  const resolvedParams = use(params);
  const [refreshInterval, setRefreshInterval] = useState(10000);
  const [stats, setStats] = useState<QueueStats | null>(null);
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState('');
  const router = useRouter();
  const queueName = decodeURIComponent(resolvedParams.queue);

  useEffect(() => {
    // Check if Redis is connected
    const checkConnection = async () => {
      try {
        const response = await fetch('/api/redis/connect');
        const data = await response.json();
        
        if (!data.connected) {
          router.push('/');
        }
      } catch (err) {
        router.push('/');
      }
    };
    
    checkConnection();
  }, [router]);

  const fetchStats = async () => {
    try {
      const response = await fetch(
        `/api/queue/${encodeURIComponent(queueName)}/stats`,
      );
      if (!response.ok) throw new Error('Failed to fetch stats');
      
      const data = await response.json();
      setStats(data);
      setError('');
    } catch (err) {
      setError(err instanceof Error ? err.message : 'Failed to fetch stats');
    } finally {
      setLoading(false);
    }
  };

  useEffect(() => {
    fetchStats();
    
    if (refreshInterval > 0) {
      const interval = setInterval(fetchStats, refreshInterval);
      return () => clearInterval(interval);
    }
  }, [refreshInterval, queueName]);

  return (
    <div className="min-h-screen bg-gray-50 dark:bg-gray-900">
      <Header 
        refreshInterval={refreshInterval}
        onRefreshIntervalChange={setRefreshInterval}
      />
      
      <div className="p-6">
        <div className="flex items-center justify-between mb-6">
          <h2 className="text-2xl font-bold text-gray-900 dark:text-white">
            Queue Statistics: {getBaseQueueName(queueName)}
          </h2>
          <Link
            href={`/queue/${encodeURIComponent(queueName)}`}
            className="px-4 py-2 bg-blue-600 hover:bg-blue-700 text-white rounded-md transition-colors"
          >
            View Tasks
          </Link>
        </div>
        
        {loading ? (
          <div className="text-center py-8 text-gray-500 dark:text-gray-400">
            Loading statistics...
          </div>
        ) : error ? (
          <div className="text-center py-8 text-red-600 dark:text-red-400">
            {error}
          </div>
        ) : stats ? (
          <div className="grid grid-cols-1 lg:grid-cols-2 gap-6">
            <QueueStatsChart stats={stats} />
            
            <div className="bg-white dark:bg-gray-800 rounded-lg shadow p-6">
              <h3 className="text-lg font-semibold text-gray-900 dark:text-white mb-4">
                Performance Metrics
              </h3>
              
              <div className="space-y-4">
                <div>
                  <p className="text-sm text-gray-600 dark:text-gray-400">Success Rate</p>
                  <p className="text-2xl font-bold text-gray-900 dark:text-white">
                    {stats.completed + stats.failed > 0 
                      ? ((stats.completed / (stats.completed + stats.failed)) * 100).toFixed(1)
                      : '0'
                    }%
                  </p>
                </div>
                
                <div>
                  <p className="text-sm text-gray-600 dark:text-gray-400">Throughput</p>
                  <p className="text-2xl font-bold text-gray-900 dark:text-white">
                    {stats.active} active tasks
                  </p>
                </div>
                
                <div>
                  <p className="text-sm text-gray-600 dark:text-gray-400">Queue Depth</p>
                  <p className="text-2xl font-bold text-gray-900 dark:text-white">
                    {stats.pending + stats.scheduled} waiting
                  </p>
                </div>
              </div>
            </div>
          </div>
        ) : null}
      </div>
    </div>
  );
}
