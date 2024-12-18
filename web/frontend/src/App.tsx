import { useState, useEffect, useCallback, useRef } from 'react';
import { Input, Button, InputNumber } from 'antd';
import { Routes, Route, useNavigate } from 'react-router-dom';
import styled from 'styled-components';
import logo from './cppq.png';
import Dashboard from './Dashboard';
import Queue from './Queue';
import cppq from './cppq.png';
import 'antd/dist/antd.min.css';
import './App.css';

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

function App() {
  const [redisURI, setRedisURI] = useState('');
  const [error, setError] = useState(null);
  const [refetch, setRefetch] = useState(new Date());
  //const [refetchInterval, setRefetchInterval] = useState<ReturnType<typeof setInterval> | undefined>(undefined);
  const refetchInterval = useRef<ReturnType<typeof setInterval> | undefined>(undefined);
  let refetchInit = useRef(false);
  const navigate = useNavigate();

  useEffect(() => {
    async function fetchConnection() {
      await fetch('http://localhost:5000/redis/connect', { method: 'GET' })
        .then((response) => response.json())
        .then((body) => {
          if (body.connected)
            navigate('/dashboard');
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
          navigate('/dashboard');
        }
      });
  };

  const onUpdateIntervalChange = useCallback((value: any) => {
    clearInterval(refetchInterval.current);
    refetchInterval.current = setInterval(() => { setRefetch(new Date()) }, value * 1000)
  }, [refetchInterval]);

  useEffect(() => {
    if (!refetchInit.current && refetchInterval.current === undefined) {
      refetchInit.current = true;
      onUpdateIntervalChange(5);
    }
  }, [refetchInterval, onUpdateIntervalChange]);

  const onLogoClick = () => {
    navigate('/dashboard');
  };

  function RedisLogin() {
    return (
      <div style={{ display: 'flex', alignItems: 'center', justifyContent: 'center', height: '100vh', flexFlow: 'wrap' }}>
        {error && <p>{error}</p>}
        <img src={logo} alt='logo' />
        <Input value={redisURI} onChange={(e) => setRedisURI(e.target.value)} onPressEnter={onConnectClick} placeholder="Redis connection URI" style={{ width: '500px' }} />
        <Button onClick={onConnectClick} type="primary" style={{ marginLeft: '10px' }}>Connect</Button>
      </div>
    );
  }

  return (
    <div>
      <Header>
        <header className="site-header">
          <div className="site-identity">
            <a onClick={onLogoClick}><img src={cppq} alt="cppq logo" /></a>
          </div>
          <div style={{ float: 'right' }}>
            <span style={{ marginRight: '10px' }}>Update interval (seconds):</span>
            <InputNumber min={1} max={10000} defaultValue={5} onChange={onUpdateIntervalChange} />
          </div>
        </header>
      </Header>
      <Routes>
        <Route path="/" element={RedisLogin()} />
        <Route path="/dashboard" element={Dashboard({ refetch })} />
        <Route path="/queue/:name" element={<Queue />} />
      </Routes>
    </div>
  );
}

export default App;
