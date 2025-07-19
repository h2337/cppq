import { useState, useEffect, useCallback } from 'react';
import styled from 'styled-components';
import cppq from './cppq.png';
import { Table, InputNumber } from 'antd';

const Header = styled.div`
body {
  font-family: Helvetica;
  margin: 0;
}

a {
  text-decoration: none;
  color: #000;
}

.site-header {
  border-bottom: 1px solid #ccc;
  padding: .5em 1em;
}

.site-header::after {
  content: "";
  display: table;
  clear: both;
}

.site-identity {
  float: left;
}

.site-identity h1 {
  font-size: 1.5em;
  margin: .7em 0 .3em 0;
  display: inline-block;
}

.site-identity img {
  max-width: 55px;
  float: left;
  margin: 0 10px 0 0;
}
`;

function Dashboard() {
  const [queues, setQueues] = useState<{ name: string, priority: string }[]>([]);
  const [refetch, setRefetch] = useState(new Date());
  const [refetchInterval, setRefetchInterval] = useState<ReturnType<typeof setInterval> | undefined>(undefined);
  let refetchInit = false;

  const onUpdateIntervalChange = useCallback((value: any) => {
    setRefetchInterval((interval) => {
      clearInterval(interval);
      return setInterval(() => { setRefetch(new Date()) }, value * 1000)
    });
  }, []);

  useEffect(() => {
    async function fetchQueues() {
      await fetch('http://localhost:5000/queue', { method: 'GET' })
        .then((response) => response.json())
        .then(async (body) => {
          let processedQueues = [];
          let key = 1;
          for (const queue of body.queues) {
            const queueParts = queue.split(':');
            const stats = await (await fetch('http://localhost:5000/queue/' + queueParts[0] + '/stats')).json();
            const memory = await (await fetch('http://localhost:5000/queue/' + queueParts[0] + '/memory')).json();
            processedQueues.push({ memory: String(memory.result / 1024) + ' MB', ...stats, failureRate: stats.completed ? String((stats.failed / stats.completed) * 100) + '%' : stats.failed ? '100%' : '0%', name: queueParts[0], priority: queueParts[1], key: key++ });
          }
          setQueues(processedQueues);
        });
    }
    fetchQueues();
    if (!refetchInit && refetchInterval === undefined) {
      refetchInit = true;
      onUpdateIntervalChange(5);
    }
  }, [refetch, refetchInterval, onUpdateIntervalChange]);

  const columns = [
    {
      title: 'Name',
      dataIndex: 'name',
      key: 'name',
    },
    {
      title: 'Priority',
      dataIndex: 'priority',
      key: 'priority',
    },
    {
      title: 'Memory usage',
      dataIndex: 'memory',
      key: 'memory',
    },
    {
      title: 'Pending',
      dataIndex: 'pending',
      key: 'pending',
    },
    {
      title: 'Scheduled',
      dataIndex: 'scheduled',
      key: 'scheduled',
    },
    {
      title: 'Active',
      dataIndex: 'active',
      key: 'active',
    },
    {
      title: 'Completed',
      dataIndex: 'completed',
      key: 'completed',
    },
    {
      title: 'Failed',
      dataIndex: 'failed',
      key: 'failed',
    },
    {
      title: 'Failure rate',
      dataIndex: 'failureRate',
      key: 'failureRate',
    },
  ];

  return (<>
  <Header>
    <header className="site-header">
      <div className="site-identity">
        <a href="/"><img src={cppq} alt="cppq logo" /></a>
      </div>
      <div style={{ float: 'right' }}>
        <span style={{ marginRight: '10px' }}>Update interval (seconds):</span>
        <InputNumber min={1} max={10000} defaultValue={5} onChange={onUpdateIntervalChange} />
      </div>
    </header>
  </Header>
  <Table dataSource={queues} columns={columns} />
</>);
}

export default Dashboard;
