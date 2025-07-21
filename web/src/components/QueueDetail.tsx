'use client';

import { useState, useEffect, useMemo } from 'react';
import Link from 'next/link';
import { Task } from '@/lib/redis';
import { getBaseQueueName } from '@/lib/queue-utils';

interface QueueDetailProps {
  queueName: string;
  refreshInterval: number;
}

type TabState = 'pending' | 'scheduled' | 'active' | 'completed' | 'failed';

export default function QueueDetail({ queueName, refreshInterval }: QueueDetailProps) {
  const [activeTab, setActiveTab] = useState<TabState>('pending');
  const [tasks, setTasks] = useState<Task[]>([]);
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState('');
  const [searchTerm, setSearchTerm] = useState('');
  const [sortField, setSortField] = useState<keyof Task>('uuid');
  const [sortDirection, setSortDirection] = useState<'asc' | 'desc'>('asc');

  const fetchTasks = async () => {
    try {
      const response = await fetch(`/api/queue/${queueName}/${activeTab}/tasks`);
      if (!response.ok) throw new Error('Failed to fetch tasks');
      
      const data = await response.json();
      setTasks(data);
      setError('');
    } catch (err) {
      setError(err instanceof Error ? err.message : 'Failed to fetch tasks');
    } finally {
      setLoading(false);
    }
  };

  useEffect(() => {
    setLoading(true);
    fetchTasks();
    
    if (refreshInterval > 0) {
      const interval = setInterval(fetchTasks, refreshInterval);
      return () => clearInterval(interval);
    }
  }, [queueName, activeTab, refreshInterval]);

  const formatDateTime = (timestamp?: string) => {
    if (!timestamp) return '-';
    return new Date(parseInt(timestamp) * 1000).toLocaleString();
  };

  const filteredAndSortedTasks = useMemo(() => {
    let filtered = tasks;
    
    // Filter by search term
    if (searchTerm) {
      filtered = tasks.filter(task => 
        task.uuid.toLowerCase().includes(searchTerm.toLowerCase()) ||
        task.type.toLowerCase().includes(searchTerm.toLowerCase()) ||
        task.payload.toLowerCase().includes(searchTerm.toLowerCase())
      );
    }
    
    // Sort tasks
    return filtered.sort((a, b) => {
      const aValue = a[sortField] || '';
      const bValue = b[sortField] || '';
      
      if (sortDirection === 'asc') {
        return aValue > bValue ? 1 : -1;
      } else {
        return aValue < bValue ? 1 : -1;
      }
    });
  }, [tasks, searchTerm, sortField, sortDirection]);

  const handleSort = (field: keyof Task) => {
    if (sortField === field) {
      setSortDirection(sortDirection === 'asc' ? 'desc' : 'asc');
    } else {
      setSortField(field);
      setSortDirection('asc');
    }
  };

  const exportToCSV = () => {
    const headers = ['UUID', 'Type', 'Payload', 'Max Retry', 'Retry Count'];
    
    if (activeTab === 'scheduled') {
      headers.push('Schedule Time', 'Cron');
    }
    if (activeTab === 'active' || activeTab === 'completed' || activeTab === 'failed') {
      headers.push('Dequeue Time');
    }
    if (activeTab === 'completed') {
      headers.push('Result');
    }
    
    const rows = filteredAndSortedTasks.map(task => {
      const row = [task.uuid, task.type, task.payload, task.max_retry, task.retry_count];
      
      if (activeTab === 'scheduled') {
        row.push(formatDateTime(task.schedule_time), task.cron || '-');
      }
      if (activeTab === 'active' || activeTab === 'completed' || activeTab === 'failed') {
        row.push(formatDateTime(task.dequeue_time));
      }
      if (activeTab === 'completed') {
        row.push(task.result || '-');
      }
      
      return row;
    });
    
    const csvContent = [
      headers.join(','),
      ...rows.map(row => row.map(cell => `"${cell}"`).join(','))
    ].join('\n');
    
    const blob = new Blob([csvContent], { type: 'text/csv;charset=utf-8;' });
    const link = document.createElement('a');
    const url = URL.createObjectURL(blob);
    
    link.setAttribute('href', url);
    link.setAttribute('download', `cppq-${queueName}-${activeTab}-${new Date().toISOString().split('T')[0]}.csv`);
    link.style.visibility = 'hidden';
    
    document.body.appendChild(link);
    link.click();
    document.body.removeChild(link);
  };

  const tabs: { key: TabState; label: string }[] = [
    { key: 'pending', label: 'Pending' },
    { key: 'scheduled', label: 'Scheduled' },
    { key: 'active', label: 'Active' },
    { key: 'completed', label: 'Completed' },
    { key: 'failed', label: 'Failed' },
  ];

  return (
    <div className="p-6">
      <div className="flex items-center justify-between mb-6">
        <h2 className="text-2xl font-bold text-gray-900 dark:text-white">
          Queue: {getBaseQueueName(queueName)}
        </h2>
        <div className="flex items-center space-x-2">
          <Link
            href={`/queue/${encodeURIComponent(queueName)}/stats`}
            className="px-4 py-2 bg-blue-600 hover:bg-blue-700 text-white rounded-md transition-colors"
          >
            View Stats
          </Link>
          <button
            onClick={exportToCSV}
            className="px-4 py-2 bg-green-600 hover:bg-green-700 text-white rounded-md transition-colors flex items-center space-x-2"
          >
            <svg className="w-4 h-4" fill="none" viewBox="0 0 24 24" stroke="currentColor">
              <path strokeLinecap="round" strokeLinejoin="round" strokeWidth={2} d="M12 10v6m0 0l-3-3m3 3l3-3m2 8H7a2 2 0 01-2-2V5a2 2 0 012-2h5.586a1 1 0 01.707.293l5.414 5.414a1 1 0 01.293.707V19a2 2 0 01-2 2z" />
            </svg>
            <span>Export</span>
          </button>
        </div>
      </div>

      <div className="border-b border-gray-200 dark:border-gray-700">
        <nav className="-mb-px flex space-x-8">
          {tabs.map((tab) => (
            <button
              key={tab.key}
              onClick={() => setActiveTab(tab.key)}
              className={`py-2 px-1 border-b-2 font-medium text-sm transition-colors ${
                activeTab === tab.key
                  ? 'border-blue-500 text-blue-600 dark:text-blue-400'
                  : 'border-transparent text-gray-500 hover:text-gray-700 hover:border-gray-300 dark:text-gray-400 dark:hover:text-gray-300'
              }`}
            >
              {tab.label}
            </button>
          ))}
        </nav>
      </div>

      <div className="mt-6">
        <div className="mb-4">
          <input
            type="text"
            placeholder="Search tasks by UUID, type, or payload..."
            value={searchTerm}
            onChange={(e) => setSearchTerm(e.target.value)}
            className="w-full px-4 py-2 border border-gray-300 dark:border-gray-600 rounded-md focus:ring-2 focus:ring-blue-500 focus:border-transparent dark:bg-gray-700 dark:text-white"
          />
        </div>
        {loading ? (
          <div className="text-center py-8 text-gray-500 dark:text-gray-400">
            Loading tasks...
          </div>
        ) : error ? (
          <div className="text-center py-8 text-red-600 dark:text-red-400">
            {error}
          </div>
        ) : tasks.length === 0 ? (
          <div className="text-center py-8 text-gray-500 dark:text-gray-400">
            No {activeTab} tasks
          </div>
        ) : (
          <div className="bg-white dark:bg-gray-800 shadow overflow-hidden rounded-lg">
            <table className="min-w-full divide-y divide-gray-200 dark:divide-gray-700">
              <thead className="bg-gray-50 dark:bg-gray-700">
                <tr>
                  <th 
                    className="px-6 py-3 text-left text-xs font-medium text-gray-500 dark:text-gray-300 uppercase tracking-wider cursor-pointer hover:text-gray-700 dark:hover:text-gray-100"
                    onClick={() => handleSort('uuid')}
                  >
                    UUID {sortField === 'uuid' && (sortDirection === 'asc' ? '↑' : '↓')}
                  </th>
                  <th 
                    className="px-6 py-3 text-left text-xs font-medium text-gray-500 dark:text-gray-300 uppercase tracking-wider cursor-pointer hover:text-gray-700 dark:hover:text-gray-100"
                    onClick={() => handleSort('type')}
                  >
                    Type {sortField === 'type' && (sortDirection === 'asc' ? '↑' : '↓')}
                  </th>
                  <th className="px-6 py-3 text-left text-xs font-medium text-gray-500 dark:text-gray-300 uppercase tracking-wider">
                    Payload
                  </th>
                  <th className="px-6 py-3 text-left text-xs font-medium text-gray-500 dark:text-gray-300 uppercase tracking-wider">
                    Retry
                  </th>
                  {activeTab === 'scheduled' && (
                    <>
                      <th className="px-6 py-3 text-left text-xs font-medium text-gray-500 dark:text-gray-300 uppercase tracking-wider">
                        Schedule Time
                      </th>
                      <th className="px-6 py-3 text-left text-xs font-medium text-gray-500 dark:text-gray-300 uppercase tracking-wider">
                        Cron
                      </th>
                    </>
                  )}
                  {(activeTab === 'active' || activeTab === 'completed' || activeTab === 'failed') && (
                    <th className="px-6 py-3 text-left text-xs font-medium text-gray-500 dark:text-gray-300 uppercase tracking-wider">
                      Dequeue Time
                    </th>
                  )}
                  {activeTab === 'completed' && (
                    <th className="px-6 py-3 text-left text-xs font-medium text-gray-500 dark:text-gray-300 uppercase tracking-wider">
                      Result
                    </th>
                  )}
                </tr>
              </thead>
              <tbody className="bg-white dark:bg-gray-800 divide-y divide-gray-200 dark:divide-gray-700">
                {filteredAndSortedTasks.map((task) => (
                  <tr key={task.uuid} className="hover:bg-gray-50 dark:hover:bg-gray-700">
                    <td className="px-6 py-4 whitespace-nowrap text-sm font-mono text-gray-900 dark:text-gray-100">
                      {task.uuid.substring(0, 8)}...
                    </td>
                    <td className="px-6 py-4 whitespace-nowrap text-sm text-gray-900 dark:text-gray-100">
                      {task.type}
                    </td>
                    <td className="px-6 py-4 text-sm text-gray-900 dark:text-gray-100">
                      <div className="max-w-xs truncate" title={task.payload}>
                        {task.payload}
                      </div>
                    </td>
                    <td className="px-6 py-4 whitespace-nowrap text-sm text-gray-900 dark:text-gray-100">
                      {task.retry_count}/{task.max_retry}
                    </td>
                    {activeTab === 'scheduled' && (
                      <>
                        <td className="px-6 py-4 whitespace-nowrap text-sm text-gray-900 dark:text-gray-100">
                          {formatDateTime(task.schedule_time)}
                        </td>
                        <td className="px-6 py-4 whitespace-nowrap text-sm font-mono text-gray-900 dark:text-gray-100">
                          {task.cron || '-'}
                        </td>
                      </>
                    )}
                    {(activeTab === 'active' || activeTab === 'completed' || activeTab === 'failed') && (
                      <td className="px-6 py-4 whitespace-nowrap text-sm text-gray-900 dark:text-gray-100">
                        {formatDateTime(task.dequeue_time)}
                      </td>
                    )}
                    {activeTab === 'completed' && (
                      <td className="px-6 py-4 text-sm text-gray-900 dark:text-gray-100">
                        <div className="max-w-xs truncate" title={task.result}>
                          {task.result || '-'}
                        </div>
                      </td>
                    )}
                  </tr>
                ))}
              </tbody>
            </table>
          </div>
        )}
      </div>
    </div>
  );
}