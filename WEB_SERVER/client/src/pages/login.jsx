import React, { useState } from 'react';
import { useNavigate } from 'react-router-dom';

const Login = () => {
  const [username, setUsername] = useState('admin');
  const [password, setPassword] = useState('password123');
  const [message, setMessage] = useState('');
  const navigate = useNavigate();

  const handleSubmit = async (e) => {
    e.preventDefault();
    setMessage('');

    try {
      const res = await fetch('/login', {
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
        navigate('/dashboard');
      } else {
        setMessage('Error: Invalid username or password.');
      }
    } catch (err) {
      setMessage('Error: Could not connect to server.');
    }
  };

  return (
    <div className="login-container">
      <h2>Smart Trashcan Login</h2>
      <form id="loginForm" onSubmit={handleSubmit}>
        <input
          type="text"
          id="username"
          placeholder="Username"
          value={username}
          onChange={(e) => setUsername(e.target.value)}
          required
        />
        <input
          type="password"
          id="password"
          placeholder="Password"
          value={password}
          onChange={(e) => setPassword(e.target.value)}
          required
        />
        <button type="submit">Login</button>
      </form>
      <p id="message">{message}</p>
    </div>
  );
};

export default Login;