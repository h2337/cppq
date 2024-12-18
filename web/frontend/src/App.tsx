import { useState, useEffect } from 'react';
import { Input, Button } from 'antd';
import logo from './cppq.png';
import Dashboard from './Dashboard';
import 'antd/dist/antd.min.css';
import './App.css';

function App() {
  const [redisURI, setRedisURI] = useState('');
  const [error, setError] = useState(null);
  const [dashboardOpen, setDashboardOpen] = useState(false);

  useEffect(() => {
    async function fetchConnection() {
      await fetch('http://localhost:5000/redis/connect', { method: 'GET' })
        .then((response) => response.json())
        .then((body) => {
          if (body.connected)
            setDashboardOpen(true);
        });
    }
    fetchConnection();
  }, []);

  const onConnectClick = async () => {
    setError(null);
    await fetch('http://localhost:5000/redis/connect', { method: 'POST', body: new URLSearchParams({ 'uri': redisURI }) })
      .then((response) => response.json())
      .then((body) => {
        if (!body.success)
          setError(body.error);
        else {
          setError(null);
          setDashboardOpen(true);
        }
      });
  };

  return (
    <div> {dashboardOpen ? <Dashboard /> :
      <div style={{ display: 'flex', alignItems: 'center', justifyContent: 'center', height: '100vh', flexFlow: 'wrap' }}>
        {error && <p>{error}</p>}
        <img src={logo} alt='logo' />
        <Input value={redisURI} onChange={(e) => setRedisURI(e.target.value)} onPressEnter={onConnectClick} placeholder="Redis connection URI" style={{ width: '500px' }} />
        <Button onClick={onConnectClick} type="primary" style={{ marginLeft: '10px' }}>Connect</Button>
    </div>}
  </div>
  );
}

export default App;
