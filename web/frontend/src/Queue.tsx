import { useEffect } from 'react';
import { useParams } from 'react-router-dom';
import { Tabs, Tag } from 'antd';

function Queue(props: { refetch: Date }) {
  const { name } = useParams();

  useEffect(() => {
  }, [props.refetch]);

  const items = [
    { label: 'Pending', key: 'pending', children: 'Content 1' },
    { label: 'Scheduled', key: 'scheduled', children: 'Content 2' },
    { label: 'Active', key: 'active', children: 'Content 1' },
    { label: 'Completed', key: 'completed', children: 'Content 2' },
    { label: 'Failed', key: 'failed', children: 'Content 2' },
  ];

  return (<>
  <Tag color="green" style={{ marginLeft: '10px', marginTop: '10px' }}>Queue: {name}</Tag>
  <Tabs items={items} style={{ marginLeft: '10px', marginRight: '10px' }} />
</>);
}

export default Queue;
