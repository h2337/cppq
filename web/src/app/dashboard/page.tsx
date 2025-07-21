'use client';

import { useState, useEffect } from 'react';
import { useRouter } from 'next/navigation';
import Header from '@/components/Header';
import Dashboard from '@/components/Dashboard';

export default function DashboardPage() {
  const [refreshInterval, setRefreshInterval] = useState(10000);
  const router = useRouter();

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

  return (
    <div className="min-h-screen bg-gray-50 dark:bg-gray-900">
      <Header 
        refreshInterval={refreshInterval}
        onRefreshIntervalChange={setRefreshInterval}
      />
      <Dashboard refreshInterval={refreshInterval} />
    </div>
  );
}