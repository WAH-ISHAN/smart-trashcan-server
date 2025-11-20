import React, { useState } from 'react';
import { useNavigate } from 'react-router-dom';

const BACKEND_URL = 'http://localhost:3001'; // <-- add backend URL here

const Login = () => {
  const [username, setUsername] = useState('admin');
  const [password, setPassword] = useState('1234'); // matches backend dummy user
  const [message, setMessage] = useState('');
  const navigate = useNavigate();

  const handleSubmit = async (e) => {
    e.preventDefault();
    setMessage('');

    try {
      const res = await fetch(`${BACKEND_URL}/login`, {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({
          username: username.trim(),
          password
        })
      });

      if (res.ok) {
        const data = await res.json();
        localStorage.setItem('trashcan_token', data.token);
        navigate('/dashboard', { replace: true });
      } else {
        const errData = await res.json().catch(() => null);
        setMessage(errData?.message || 'Error: Invalid username or password.');
      }
    } catch {
      setMessage('Error: Could not connect to server.');
    }
  };

  return (
    <div className="login-container">
      <h2>Smart Trashcan Login</h2>
      <form id="loginForm" onSubmit={handleSubmit}>
        <input
          type="text"
          placeholder="Username"
          value={username}
          onChange={(e) => setUsername(e.target.value)}
          required
        />
        <input
          type="password"
          placeholder="Password"
          value={password}
          onChange={(e) => setPassword(e.target.value)}
          required
        />
        <button type="submit">Login</button>
      </form>
      <p>{message}</p>
    </div>
  );
};

export default Login;
