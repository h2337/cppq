# cppq Web UI

A modern web dashboard for monitoring and managing cppq (C++ task queue) built with Next.js, React, and TypeScript.

## Overview

The cppq Web UI provides a real-time dashboard to monitor your task queues, inspect individual tasks, and control queue operations. It connects directly to your Redis instance to provide live updates on queue statistics and task states.

## Features

### Queue Management
- **Real-time Monitoring**: Auto-refresh functionality with configurable intervals (5s, 10s, 30s, 1m, or manual)
- **Queue Overview**: View all queues with their priorities, memory usage, and task counts
- **Queue Statistics**: Visualize queue performance with charts showing task distribution and success rates
- **Pause/Unpause**: Control queue processing with one-click pause/unpause functionality
- **Failure Rate Tracking**: Automatic calculation and display of queue failure rates

### Task Inspection
- **State-based Views**: Browse tasks by state (pending, scheduled, active, completed, failed)
- **Task Details**: View complete task information including:
  - Task UUID
  - Task type
  - Payload data
  - Retry count and max retries
  - Schedule time and cron expressions (for scheduled tasks)
  - Dequeue time (for active/completed/failed tasks)
  - Results (for completed tasks)
- **Search & Filter**: Search tasks by UUID, type, or payload content
- **Sort Functionality**: Sort tasks by any field with click-to-sort headers

### Data Export
- **CSV Export**: Export queue statistics and task lists to CSV files
- **Bulk Export**: Export all tasks from a specific queue state

### User Experience
- **Dark Mode**: Toggle between light and dark themes with system preference detection
- **Responsive Design**: Works seamlessly on desktop and mobile devices
- **Clean Navigation**: Intuitive navigation between dashboard, queue details, and statistics

## Getting Started

### Prerequisites
- Node.js 18+ and npm
- Redis server running (default: localhost:6379)
- cppq tasks already in Redis

### Installation

1. Clone the repository and navigate to the web directory:
   ```bash
   cd web
   ```

2. Install dependencies:
   ```bash
   npm install
   ```

3. Start the development server:
   ```bash
   npm run dev
   ```

4. Open [http://localhost:3000](http://localhost:3000) in your browser

### Configuration

#### Redis Connection
The default Redis connection is `redis://localhost:6379`. You can connect to a different Redis instance by entering the URI in the connection screen.

Supported Redis URI formats:
- `redis://localhost:6379`
- `redis://username:password@hostname:port`
- `redis://hostname:port/db_number`

#### Environment Variables
You can configure the application using environment variables:

```bash
# .env.local
REDIS_URI=redis://localhost:6379
```

## Architecture

### Technology Stack
- **Frontend Framework**: Next.js 15 (App Router)
- **UI Library**: React 19
- **Language**: TypeScript
- **Styling**: Tailwind CSS v4
- **State Management**: React hooks
- **Data Fetching**: Next.js API routes
- **Redis Client**: node-redis

### Project Structure
```
web/
├── src/
│   ├── app/                    # Next.js app directory
│   │   ├── api/               # API routes
│   │   │   ├── queue/         # Queue-related endpoints
│   │   │   └── redis/         # Redis connection endpoints
│   │   ├── dashboard/         # Dashboard page
│   │   ├── queue/[queue]/     # Queue detail pages
│   │   └── test/              # Debug/test page
│   ├── components/            # React components
│   │   ├── Dashboard.tsx      # Main dashboard view
│   │   ├── Header.tsx         # App header with controls
│   │   ├── QueueDetail.tsx    # Queue task list view
│   │   ├── QueueStats.tsx     # Statistics visualization
│   │   ├── RedisConnect.tsx   # Redis connection form
│   │   └── ThemeToggle.tsx    # Dark mode toggle
│   └── lib/                   # Utility functions
│       ├── redis.ts           # Redis operations
│       ├── redis-singleton.ts # Redis connection management
│       └── queue-utils.ts     # Queue name parsing utilities
├── public/                    # Static assets
└── package.json              # Dependencies and scripts
```

### API Endpoints

#### Connection Management
- `GET /api/redis/connect` - Check connection status
- `POST /api/redis/connect` - Connect to Redis instance

#### Queue Operations
- `GET /api/queue` - List all queues
- `GET /api/queue/[queue]/stats` - Get queue statistics
- `GET /api/queue/[queue]/memory` - Get queue memory usage
- `POST /api/queue/[queue]/pause` - Pause queue
- `POST /api/queue/[queue]/unpause` - Unpause queue

#### Task Operations
- `GET /api/queue/[queue]/[state]/tasks` - Get tasks by state

#### Debug Endpoints
- `GET /api/debug/queue/[queue]` - Debug queue information
- `GET /api/test-redis` - Test Redis connectivity

## Queue Naming Convention

cppq uses a priority-based naming convention for queues:
- Queue names in Redis: `queuename:priority` (e.g., `high:20`, `default:10`, `low:5`)
- Redis keys use base names: `cppq:queuename:state` (e.g., `cppq:high:pending`)
- The UI automatically handles this conversion and displays user-friendly names

## Development

### Running in Development
```bash
npm run dev
```
The application will be available at [http://localhost:3000](http://localhost:3000) with hot-reload enabled.

### Building for Production
```bash
npm run build
npm start
```

### Linting
```bash
npm run lint
```

### Type Checking
TypeScript type checking is performed automatically during build. For manual checking:
```bash
npx tsc --noEmit
```

## Troubleshooting

### Redis Connection Issues
- Ensure Redis is running and accessible
- Check if the Redis URI is correct
- Verify network connectivity to Redis host
- Check Redis authentication if using password

### Queue Not Showing Data
- Verify queue names match the cppq naming convention
- Check if tasks exist in Redis using the debug page (`/test`)
- Ensure Redis keys follow the pattern `cppq:queuename:state`

### Dark Mode Not Working
- Clear browser cache and localStorage
- Check if JavaScript is enabled
- Try toggling theme manually using the theme button

## Contributing

When contributing to the web UI:

1. Follow the existing code style and conventions
2. Use TypeScript for all new code
3. Add appropriate error handling
4. Update this README if adding new features
5. Test on both light and dark themes
6. Ensure responsive design works on mobile

## License

This project is part of cppq and follows the same MIT license.