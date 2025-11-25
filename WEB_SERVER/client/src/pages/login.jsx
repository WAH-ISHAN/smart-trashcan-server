// src/components/Login.js 
import React, { useState } from 'react';
import { useNavigate } from 'react-router-dom';
import { signInWithEmailAndPassword } from 'firebase/auth';
import { auth } from '../firebase'; 
const Login = () => {
  // Firebase email/password 
  const [email, setEmail] = useState('yeshminathasha@gmail.com'); 
  const [password, setPassword] = useState('123456');        
  const [message, setMessage] = useState('');
  const navigate = useNavigate();

  const handleSubmit = async (e) => {
    e.preventDefault();
    setMessage('');

    try {
      // Firebase Auth login
      await signInWithEmailAndPassword(auth, email.trim(), password);

      // login success toward dashboard 
      navigate('/dashboard', { replace: true });
    } catch (err) {
      console.error(err);
      // error code msg
      switch (err.code) {
        case 'auth/user-not-found':
        case 'auth/wrong-password':
          setMessage('Invalid email or password.');
          break;
        case 'auth/too-many-requests':
          setMessage('Too many attempts. Please try again later.');
          break;
        default:
          setMessage('Login failed: ' + err.code);
      }
    }
  };

  return (
    <div className="login-page">
      <div className="login-container">
        <h2>Smart Trashcan</h2>
        <p className="login-subtitle">Admin Dashboard Login</p>

        <form id="loginForm" className="login-form" onSubmit={handleSubmit}>
          <label>
            Email
            <input
              type="email"
              placeholder="Enter email"
              value={email}
              autoComplete="email"
              onChange={(e) => setEmail(e.target.value)}
              required
            />
          </label>

          <label>
            Password
            <input
              type="password"
              placeholder="Enter password"
              value={password}
              autoComplete="current-password"
              onChange={(e) => setPassword(e.target.value)}
              required
            />
          </label>

          {message && <div className="error-msg">{message}</div>}

          <button type="submit">Login</button>
        </form>
      </div>
    </div>
  );
};

export default Login;