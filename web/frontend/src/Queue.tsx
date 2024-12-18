import { useState, useEffect } from 'react';
import { useParams } from 'react-router-dom';
import { Tabs, Tag, Table } from 'antd';

function Queue(props: { refetch: Date }) {
  const [currentTab, setCurrentTab] = useState('pending');
  const [tasks, setTasks] = useState([]);
  const { name } = useParams();

  useEffect(() => {
    async function fetchQueues() {
      await fetch('http://localhost:5000/queue/' + name + '/' + currentTab + '/tasks', { method: 'GET' })
        .then((response) => response.json())
        .then(async (body) => {
          setTasks(body.result.map((e: any) => {
            if (e.schedule)
              e.schedule = new Date(Number(e.schedule)).toISOString();
            if (e.dequeuedAtMs)
              e.dequeuedAt = new Date(Number(e.dequeuedAtMs)).toString();
            return e;
          }));
        });
    }
    fetchQueues();
  }, [props.refetch, currentTab, name]);

  useEffect(() => {
    setTasks([]);
  }, [currentTab]);

  const commonColumns = [
    {
      title: 'Type',
      dataIndex: 'type',
      key: 'type',
    },
    {
      title: 'Payload',
      dataIndex: 'payload',
      key: 'payload',
    },
    {
      title: 'Max retry',
      dataIndex: 'maxRetry',
      key: 'maxRetry',
    },
    {
      title: 'Retried',
      dataIndex: 'retried',
      key: 'retried',
    },
  ];
  const pendingColumns = [...commonColumns];
  const scheduledColumns = [
    ...commonColumns,
    {
      title: 'Schedule',
      dataIndex: 'schedule',
      key: 'schedule',
    },
    {
      title: 'Cron expression',
      dataIndex: 'cron',
      key: 'cron',
    },
  ];
  const activeColumns = [
    ...commonColumns,
    {
      title: 'Dequeue time',
      dataIndex: 'dequeuedAt',
      key: 'dequeuedAt',
    },
  ];
  const completedColumns = [
    ...commonColumns,
    {
      title: 'Dequeue time',
      dataIndex: 'dequeuedAt',
      key: 'dequeuedAt',
    },
    {
      title: 'Result',
      dataIndex: 'result',
      key: 'result',
    },
  ];
  const failedColumns = [
    ...commonColumns,
    {
      title: 'Dequeue time',
      dataIndex: 'dequeuedAt',
      key: 'dequeuedAt',
    },
  ];

  const items = [
    { label: 'Pending', key: 'pending', children: <Table dataSource={tasks} columns={pendingColumns} /> },
    { label: 'Scheduled', key: 'scheduled', children: <Table dataSource={tasks} columns={scheduledColumns} /> },
    { label: 'Active', key: 'active', children: <Table dataSource={tasks} columns={activeColumns} /> },
    { label: 'Completed', key: 'completed', children: <Table dataSource={tasks} columns={completedColumns} /> },
    { label: 'Failed', key: 'failed', children: <Table dataSource={tasks} columns={failedColumns} /> },
  ];

  return (<>
  <Tag color="green" style={{ marginLeft: '10px', marginTop: '10px' }}>Queue: {name}</Tag>
  <Tabs items={items} onChange={setCurrentTab} style={{ marginLeft: '10px', marginRight: '10px' }} />
</>);
}

export default Queue;
