import React from 'react';
import { Routes, Route, Navigate } from 'react-router-dom';
import Login from './pages/login.jsx';
import Dashboard from './pages/dashboard.jsx';

const App = () => {
  return (
    <Routes>
      <Route path="/login" element={<Login />} />
      <Route path="/dashboard" element={<Dashboard />} />
      {/* default redirect */}
      <Route path="*" element={<Navigate to="/login" replace />} />
    </Routes>
  );
};

export default App;