import { useState, useEffect } from 'react';
import { useNavigate } from 'react-router-dom';
import { Table } from 'antd';

function Dashboard(props: { refetch: Date }) {
  const [queues, setQueues] = useState<{ name: string, priority: string }[]>([]);
  const navigate = useNavigate();

  useEffect(() => {
    async function fetchQueues() {
      await fetch('http://localhost:5000/queue', { method: 'GET' })
        .then((response) => response.json())
        .then(async (body) => {
          if (!body.connected) navigate('/');
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
  }, [props.refetch]);

  const onQueueClick = (queue: string) => {
    navigate('/queue/' + queue);
  };

  const columns = [
    {
      title: 'Name',
      dataIndex: 'name',
      key: 'name',
      render: (text: string) => <a onClick={() => onQueueClick(text)}>{text}</a>
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
  <Table dataSource={queues} columns={columns} />
</>);
}

export default Dashboard;
