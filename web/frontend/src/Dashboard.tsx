import { useState, useEffect } from 'react';
import { useNavigate } from 'react-router-dom';
import { Button, Table } from 'antd';
import { PauseOutlined, CaretRightOutlined } from '@ant-design/icons';

function Dashboard(props: { refetch: Date, setRefetch: (date: Date) => void }) {
  const [queues, setQueues] = useState<{ name: string, priority: string, memory: string, failureRate: string, paused: boolean }[]>([]);
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
            processedQueues.push({ memory: String(memory.result / 1024) + ' MB', ...stats, failureRate: stats.completed ? String((stats.failed / stats.completed) * 100) + '%' : stats.failed ? '100%' : '0%', name: queueParts[0], priority: queueParts[1], paused: stats.paused, key: key++ });
          }
          setQueues(processedQueues);
        });
    }
    fetchQueues();
  }, [props.refetch, navigate]);

  const onQueueClick = (queue: string) => {
    navigate('/queue/' + queue);
  };

  const onPauseClick = async (queue: string) => {
    await fetch('http://localhost:5000/queue/' + queue + '/pause', { method: 'POST' });
    props.setRefetch(new Date());
  };

  const onUnpauseClick = async (queue: string) => {
    await fetch('http://localhost:5000/queue/' + queue + '/unpause', { method: 'POST' });
    props.setRefetch(new Date());
  };

  const columns = [
    {
      title: 'Name',
      dataIndex: 'name',
      key: 'name',
      render: (name: string) => <a onClick={() => onQueueClick(name)}>{name}</a>
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
    {
      title: 'Actions',
      dataIndex: 'name',
      key: 'pause',
      render: (name: string) => queues.find((q) => q.name === name)?.paused ?
        <Button icon={<CaretRightOutlined />} onClick={() => onUnpauseClick(name)}>Unpause</Button> :
        <Button icon={<PauseOutlined />} onClick={() => onPauseClick(name)}>Pause</Button>
    },
  ];

  return (<>
  <Table dataSource={queues} columns={columns} />
</>);
}

export default Dashboard;
