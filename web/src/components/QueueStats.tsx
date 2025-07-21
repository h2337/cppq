'use client';

import { QueueStats } from '@/lib/redis';

interface QueueStatsProps {
  stats: QueueStats;
}

export default function QueueStatsChart({ stats }: QueueStatsProps) {
  const total = stats.pending + stats.scheduled + stats.active + stats.completed + stats.failed;
  
  const getPercentage = (value: number) => {
    if (total === 0) return 0;
    return ((value / total) * 100).toFixed(1);
  };

  const statItems = [
    { label: 'Pending', value: stats.pending, color: 'bg-yellow-500' },
    { label: 'Scheduled', value: stats.scheduled, color: 'bg-purple-500' },
    { label: 'Active', value: stats.active, color: 'bg-blue-500' },
    { label: 'Completed', value: stats.completed, color: 'bg-green-500' },
    { label: 'Failed', value: stats.failed, color: 'bg-red-500' },
  ];

  return (
    <div className="bg-white dark:bg-gray-800 rounded-lg shadow p-6">
      <h3 className="text-lg font-semibold text-gray-900 dark:text-white mb-4">
        Queue Statistics
      </h3>
      
      <div className="space-y-4">
        {statItems.map((item) => (
          <div key={item.label}>
            <div className="flex justify-between items-center mb-1">
              <span className="text-sm font-medium text-gray-700 dark:text-gray-300">
                {item.label}
              </span>
              <span className="text-sm text-gray-600 dark:text-gray-400">
                {item.value} ({getPercentage(item.value)}%)
              </span>
            </div>
            <div className="w-full bg-gray-200 dark:bg-gray-700 rounded-full h-2">
              <div
                className={`${item.color} h-2 rounded-full transition-all duration-300`}
                style={{ width: `${getPercentage(item.value)}%` }}
              />
            </div>
          </div>
        ))}
      </div>
      
      <div className="mt-6 pt-6 border-t border-gray-200 dark:border-gray-700">
        <div className="flex justify-between items-center">
          <span className="text-sm font-medium text-gray-700 dark:text-gray-300">
            Total Tasks
          </span>
          <span className="text-lg font-semibold text-gray-900 dark:text-white">
            {total.toLocaleString()}
          </span>
        </div>
        <div className="flex justify-between items-center mt-2">
          <span className="text-sm font-medium text-gray-700 dark:text-gray-300">
            Queue Status
          </span>
          <span className={`text-sm font-medium ${stats.paused ? 'text-yellow-600 dark:text-yellow-400' : 'text-green-600 dark:text-green-400'}`}>
            {stats.paused ? 'Paused' : 'Active'}
          </span>
        </div>
      </div>
    </div>
  );
}