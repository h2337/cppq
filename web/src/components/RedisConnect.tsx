'use client';

import { useState } from 'react';
import { useRouter } from 'next/navigation';
import Image from 'next/image';

export default function RedisConnect() {
  const [uri, setUri] = useState('redis://localhost:6379');
  const [error, setError] = useState('');
  const [loading, setLoading] = useState(false);
  const router = useRouter();

  const handleConnect = async (e: React.FormEvent) => {
    e.preventDefault();
    setError('');
    setLoading(true);

    try {
      const response = await fetch('/api/redis/connect', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ uri }),
      });

      const data = await response.json();

      if (data.connected) {
        router.push('/dashboard');
      } else {
        setError(data.error || 'Failed to connect to Redis');
      }
    } catch (err) {
      setError('Network error. Please try again.');
    } finally {
      setLoading(false);
    }
  };

  return (
    <div className="flex items-center justify-center min-h-screen p-4">
      <div className="w-full max-w-md bg-white dark:bg-gray-800 rounded-lg shadow-lg p-8">
        <div className="text-center mb-8">
          <Image
            src="/logo.svg"
            alt="cppq Logo"
            width={120}
            height={120}
            className="mx-auto mb-4"
          />
          <h1 className="text-2xl font-bold text-gray-900 dark:text-white">
            cppq Dashboard
          </h1>
          <p className="text-gray-600 dark:text-gray-400 mt-2">
            Connect to Redis to manage your queues
          </p>
        </div>

        <form onSubmit={handleConnect} className="space-y-4">
          <div>
            <label
              htmlFor="uri"
              className="block text-sm font-medium text-gray-700 dark:text-gray-300 mb-2"
            >
              Redis URI
            </label>
            <input
              id="uri"
              type="text"
              value={uri}
              onChange={(e) => setUri(e.target.value)}
              placeholder="redis://localhost:6379"
              className="w-full px-4 py-2 border border-gray-300 dark:border-gray-600 rounded-md focus:ring-2 focus:ring-blue-500 focus:border-transparent dark:bg-gray-700 dark:text-white"
              required
            />
          </div>

          {error && (
            <div className="text-red-600 dark:text-red-400 text-sm">
              {error}
            </div>
          )}

          <button
            type="submit"
            disabled={loading}
            className="w-full bg-blue-600 hover:bg-blue-700 disabled:bg-blue-400 text-white font-medium py-2 px-4 rounded-md transition-colors"
          >
            {loading ? 'Connecting...' : 'Connect'}
          </button>
        </form>
      </div>
    </div>
  );
}