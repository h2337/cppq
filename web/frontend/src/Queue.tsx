import { useParams } from 'react-router-dom';

function Queue() {
  const { name } = useParams();
  return <div>Hello {name}</div>;
}

export default Queue;
