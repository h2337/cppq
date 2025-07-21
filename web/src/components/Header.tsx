'use client';

import Image from 'next/image';
import Link from 'next/link';
import ThemeToggle from './ThemeToggle';

interface HeaderProps {
  refreshInterval: number;
  onRefreshIntervalChange: (interval: number) => void;
}

export default function Header({ refreshInterval, onRefreshIntervalChange }: HeaderProps) {
  return (
    <header className="bg-white dark:bg-gray-800 shadow-sm border-b border-gray-200 dark:border-gray-700">
      <div className="max-w-7xl mx-auto px-4 sm:px-6 lg:px-8">
        <div className="flex items-center justify-between h-16">
          <Link href="/dashboard" className="flex items-center space-x-3">
            <Image
              src="/logo.svg"
              alt="cppq Logo"
              width={40}
              height={40}
              className="rounded"
            />
            <h1 className="text-xl font-semibold text-gray-900 dark:text-white">
              cppq Dashboard
            </h1>
          </Link>

          <div className="flex items-center space-x-4">
            <label className="flex items-center space-x-2">
              <span className="text-sm text-gray-700 dark:text-gray-300">
                Refresh:
              </span>
              <select
                value={refreshInterval}
                onChange={(e) => onRefreshIntervalChange(parseInt(e.target.value))}
                className="text-sm border border-gray-300 dark:border-gray-600 rounded px-2 py-1 dark:bg-gray-700 dark:text-white"
              >
                <option value={5000}>5s</option>
                <option value={10000}>10s</option>
                <option value={30000}>30s</option>
                <option value={60000}>1m</option>
                <option value={0}>Off</option>
              </select>
            </label>
            <ThemeToggle />
          </div>
        </div>
      </div>
    </header>
  );
}