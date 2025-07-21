'use client';

import { useState, useEffect } from 'react';
import Link from 'next/link';
import { QueueStats } from '@/lib/redis';
import { getBaseQueueName, getQueuePriority } from '@/lib/queue-utils';

interface QueueData {
  name: string;
  priority: number;
  memory: number;
  stats: QueueStats;
}

interface DashboardProps {
  refreshInterval: number;
}

export default function Dashboard({ refreshInterval }: DashboardProps) {
  const [queues, setQueues] = useState<QueueData[]>([]);
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState('');

  const fetchQueues = async () => {
    try {
      const response = await fetch('/api/queue');
      if (!response.ok) throw new Error('Failed to fetch queues');
      
      const queueNames: string[] = await response.json();
      
      const queueData = await Promise.all(
        queueNames.map(async (name) => {
          const [statsRes, memoryRes] = await Promise.all([
            fetch(`/api/queue/${encodeURIComponent(name)}/stats`),
            fetch(`/api/queue/${encodeURIComponent(name)}/memory`)
          ]);
          
          const stats = await statsRes.json();
          const { memory } = await memoryRes.json();
          
          return {
            name,
            priority: getQueuePriority(name),
            memory,
            stats
          };
        })
      );
      
      setQueues(queueData.sort((a, b) => b.priority - a.priority));
      setError('');
    } catch (err) {
      setError(err instanceof Error ? err.message : 'Failed to fetch queue data');
    } finally {
      setLoading(false);
    }
  };

  useEffect(() => {
    fetchQueues();
    
    if (refreshInterval > 0) {
      const interval = setInterval(fetchQueues, refreshInterval);
      return () => clearInterval(interval);
    }
  }, [refreshInterval]);

  const handlePauseToggle = async (queue: string, isPaused: boolean) => {
    try {
      const endpoint = `/api/queue/${encodeURIComponent(queue)}/${isPaused ? 'unpause' : 'pause'}`;
      const response = await fetch(endpoint, { method: 'POST' });
      
      if (!response.ok) throw new Error(`Failed to ${isPaused ? 'unpause' : 'pause'} queue`);
      
      // Refresh data
      fetchQueues();
    } catch (err) {
      alert(err instanceof Error ? err.message : 'Operation failed');
    }
  };

  const calculateFailureRate = (stats: QueueStats) => {
    const total = stats.completed + stats.failed;
    if (total === 0) return 0;
    return ((stats.failed / total) * 100).toFixed(1);
  };

  const exportToCSV = () => {
    const headers = ['Queue', 'Priority', 'Memory (MB)', 'Pending', 'Scheduled', 'Active', 'Completed', 'Failed', 'Failure Rate (%)', 'Status'];
    const rows = queues.map(q => [
      q.name,
      q.priority,
      q.memory,
      q.stats.pending,
      q.stats.scheduled,
      q.stats.active,
      q.stats.completed,
      q.stats.failed,
      calculateFailureRate(q.stats),
      q.stats.paused ? 'Paused' : 'Active'
    ]);
    
    const csvContent = [
      headers.join(','),
      ...rows.map(row => row.join(','))
    ].join('\n');
    
    const blob = new Blob([csvContent], { type: 'text/csv;charset=utf-8;' });
    const link = document.createElement('a');
    const url = URL.createObjectURL(blob);
    
    link.setAttribute('href', url);
    link.setAttribute('download', `cppq-queues-${new Date().toISOString().split('T')[0]}.csv`);
    link.style.visibility = 'hidden';
    
    document.body.appendChild(link);
    link.click();
    document.body.removeChild(link);
  };

  if (loading) {
    return (
      <div className="flex items-center justify-center h-64">
        <div className="text-gray-500 dark:text-gray-400">Loading queues...</div>
      </div>
    );
  }

  if (error) {
    return (
      <div className="flex items-center justify-center h-64">
        <div className="text-red-600 dark:text-red-400">{error}</div>
      </div>
    );
  }

  return (
    <div className="p-6">
      <div className="flex items-center justify-between mb-6">
        <h2 className="text-2xl font-bold text-gray-900 dark:text-white">
          Queue Overview
        </h2>
        <button
          onClick={exportToCSV}
          className="px-4 py-2 bg-green-600 hover:bg-green-700 text-white rounded-md transition-colors flex items-center space-x-2"
        >
          <svg className="w-4 h-4" fill="none" viewBox="0 0 24 24" stroke="currentColor">
            <path strokeLinecap="round" strokeLinejoin="round" strokeWidth={2} d="M12 10v6m0 0l-3-3m3 3l3-3m2 8H7a2 2 0 01-2-2V5a2 2 0 012-2h5.586a1 1 0 01.707.293l5.414 5.414a1 1 0 01.293.707V19a2 2 0 01-2 2z" />
          </svg>
          <span>Export CSV</span>
        </button>
      </div>
      
      <div className="bg-white dark:bg-gray-800 shadow overflow-hidden rounded-lg">
        <table className="min-w-full divide-y divide-gray-200 dark:divide-gray-700">
          <thead className="bg-gray-50 dark:bg-gray-700">
            <tr>
              <th className="px-6 py-3 text-left text-xs font-medium text-gray-500 dark:text-gray-300 uppercase tracking-wider">
                Queue
              </th>
              <th className="px-6 py-3 text-left text-xs font-medium text-gray-500 dark:text-gray-300 uppercase tracking-wider">
                Priority
              </th>
              <th className="px-6 py-3 text-left text-xs font-medium text-gray-500 dark:text-gray-300 uppercase tracking-wider">
                Memory
              </th>
              <th className="px-6 py-3 text-left text-xs font-medium text-gray-500 dark:text-gray-300 uppercase tracking-wider">
                Pending
              </th>
              <th className="px-6 py-3 text-left text-xs font-medium text-gray-500 dark:text-gray-300 uppercase tracking-wider">
                Scheduled
              </th>
              <th className="px-6 py-3 text-left text-xs font-medium text-gray-500 dark:text-gray-300 uppercase tracking-wider">
                Active
              </th>
              <th className="px-6 py-3 text-left text-xs font-medium text-gray-500 dark:text-gray-300 uppercase tracking-wider">
                Completed
              </th>
              <th className="px-6 py-3 text-left text-xs font-medium text-gray-500 dark:text-gray-300 uppercase tracking-wider">
                Failed
              </th>
              <th className="px-6 py-3 text-left text-xs font-medium text-gray-500 dark:text-gray-300 uppercase tracking-wider">
                Failure Rate
              </th>
              <th className="px-6 py-3 text-left text-xs font-medium text-gray-500 dark:text-gray-300 uppercase tracking-wider">
                Actions
              </th>
            </tr>
          </thead>
          <tbody className="bg-white dark:bg-gray-800 divide-y divide-gray-200 dark:divide-gray-700">
            {queues.map((queue) => (
              <tr key={queue.name} className="hover:bg-gray-50 dark:hover:bg-gray-700">
                <td className="px-6 py-4 whitespace-nowrap">
                  <div className="flex items-center space-x-2">
                    <Link
                      href={`/queue/${encodeURIComponent(queue.name)}`}
                      className="text-blue-600 dark:text-blue-400 hover:text-blue-800 dark:hover:text-blue-300 font-medium"
                    >
                      {getBaseQueueName(queue.name)}
                    </Link>
                    <Link
                      href={`/queue/${encodeURIComponent(queue.name)}/stats`}
                      className="text-gray-500 hover:text-gray-700 dark:text-gray-400 dark:hover:text-gray-200"
                      title="View statistics"
                    >
                      <svg className="w-4 h-4" fill="none" viewBox="0 0 24 24" stroke="currentColor">
                        <path strokeLinecap="round" strokeLinejoin="round" strokeWidth={2} d="M9 19v-6a2 2 0 00-2-2H5a2 2 0 00-2 2v6a2 2 0 002 2h2a2 2 0 002-2zm0 0V9a2 2 0 012-2h2a2 2 0 012 2v10m-6 0a2 2 0 002 2h2a2 2 0 002-2m0 0V5a2 2 0 012-2h2a2 2 0 012 2v14a2 2 0 01-2 2h-2a2 2 0 01-2-2z" />
                      </svg>
                    </Link>
                  </div>
                </td>
                <td className="px-6 py-4 whitespace-nowrap text-sm text-gray-900 dark:text-gray-100">
                  {queue.priority}
                </td>
                <td className="px-6 py-4 whitespace-nowrap text-sm text-gray-900 dark:text-gray-100">
                  {queue.memory} MB
                </td>
                <td className="px-6 py-4 whitespace-nowrap text-sm text-gray-900 dark:text-gray-100">
                  {queue.stats.pending}
                </td>
                <td className="px-6 py-4 whitespace-nowrap text-sm text-gray-900 dark:text-gray-100">
                  {queue.stats.scheduled}
                </td>
                <td className="px-6 py-4 whitespace-nowrap text-sm text-gray-900 dark:text-gray-100">
                  {queue.stats.active}
                </td>
                <td className="px-6 py-4 whitespace-nowrap text-sm text-gray-900 dark:text-gray-100">
                  {queue.stats.completed}
                </td>
                <td className="px-6 py-4 whitespace-nowrap text-sm text-gray-900 dark:text-gray-100">
                  {queue.stats.failed}
                </td>
                <td className="px-6 py-4 whitespace-nowrap text-sm text-gray-900 dark:text-gray-100">
                  {calculateFailureRate(queue.stats)}%
                </td>
                <td className="px-6 py-4 whitespace-nowrap text-sm">
                  <button
                    onClick={() => handlePauseToggle(queue.name, queue.stats.paused)}
                    className={`px-3 py-1 rounded text-white font-medium transition-colors ${
                      queue.stats.paused
                        ? 'bg-green-600 hover:bg-green-700'
                        : 'bg-red-600 hover:bg-red-700'
                    }`}
                  >
                    {queue.stats.paused ? 'Unpause' : 'Pause'}
                  </button>
                </td>
              </tr>
            ))}
          </tbody>
        </table>
        
        {queues.length === 0 && (
          <div className="text-center py-8 text-gray-500 dark:text-gray-400">
            No queues found
          </div>
        )}
      </div>
    </div>
  );
}