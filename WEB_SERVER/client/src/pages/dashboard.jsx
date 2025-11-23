// Dashboard.jsx
import React, { useEffect, useRef, useState } from 'react';
import { useNavigate } from 'react-router-dom';
import { io } from 'socket.io-client';
import Chart from 'chart.js/auto';

// ====== IMPORTANT: no process.env here ======
const SOCKET_URL = 'http://localhost:3001';   // Node backend / Socket.IO URL
const ESP32_BASE_URL = 'http://192.168.4.1'; // ESP32-CAM IP (AP mode)

const Dashboard = () => {
  const navigate = useNavigate();

  const [health, setHealth] = useState({
    motors: '--',
    arm: '--',
    ml_service: '--',
  });
  const [accuracy, setAccuracy] = useState(0);
  const [detections, setDetections] = useState([]);
  const [activityCounts, setActivityCounts] = useState([0, 0, 0]);
  const [frameTs, setFrameTs] = useState(Date.now());

  const socketRef = useRef(null);
  const chartCanvasRef = useRef(null);
  const chartInstanceRef = useRef(null);
  const tokenRef = useRef(localStorage.getItem('trashcan_token'));

  // Token check + Socket.IO connection
  useEffect(() => {
    if (!tokenRef.current) {
      alert('Please login first');
      navigate('/login', { replace: true });
      return;
    }

    const socket = io(SOCKET_URL, {
      transports: ['websocket', 'polling'],
    });
    socketRef.current = socket;

    socket.on('connect', () => {
      console.log('Socket connected:', socket.id);
    });

    socket.on('disconnect', () => {
      console.log('Socket disconnected');
    });

    socket.on('error_message', (msg) => {
      console.warn('Server error:', msg?.message || msg);
    });

    socket.on('health_update', (data) => {
      setHealth({
        motors: data.motors || '--',
        arm: data.arm || '--',
        ml_service: data.ml_service || '--',
      });
    });

    socket.on('accuracy_update', (data) => {
      setAccuracy(data?.accuracy || 0);
    });

    socket.on('detection_update', (data) => {
      setDetections((prev) => {
        const real = prev.filter((d) => !d.__placeholder);
        return [data, ...real].slice(0, 20); // recent 20 only
      });
    });

    socket.on('chart_update', (data) => {
      setActivityCounts((prev) => {
        const next = [...prev];
        if (data.tool === 'arm') next[0] += 1;
        else if (data.tool === 'motor') next[1] += 1;
        else if (data.tool === 'detection') next[2] += 1;
        return next;
      });
    });

    return () => {
      socket.disconnect();
      socketRef.current = null;
    };
  }, [navigate]);

  // Init Chart.js once
  useEffect(() => {
    if (!chartCanvasRef.current || chartInstanceRef.current) return;

    const ctx = chartCanvasRef.current.getContext('2d');
    const chart = new Chart(ctx, {
      type: 'bar',
      data: {
        labels: ['Arm Moves', 'Motor Runs', 'Detections'],
        datasets: [
          {
            label: '# of Activities',
            data: activityCounts,
            backgroundColor: [
              'rgba(255, 99, 132, 0.2)',
              'rgba(54, 162, 235, 0.2)',
              'rgba(255, 206, 86, 0.2)',
            ],
            borderColor: [
              'rgba(255, 99, 132, 1)',
              'rgba(54, 162, 235, 1)',
              'rgba(255, 206, 86, 1)',
            ],
            borderWidth: 1,
          },
        ],
      },
      options: { scales: { y: { beginAtZero: true } } },
    });

    chartInstanceRef.current = chart;

    return () => {
      chart.destroy();
      chartInstanceRef.current = null;
    };
  }, []);

  // Update chart when counts change
  useEffect(() => {
    const chart = chartInstanceRef.current;
    if (!chart) return;
    chart.data.datasets[0].data = activityCounts;
    chart.update();
  }, [activityCounts]);

  // Auto-refresh ESP32 snapshot (change URL param)
  useEffect(() => {
    const id = setInterval(() => {
      setFrameTs(Date.now());
    }, 300); // ~3 fps
    return () => clearInterval(id);
  }, []);

  const sendCommand = (cmd) => {
    const socket = socketRef.current;
    const token = tokenRef.current;
    if (!socket || !token) return;
    socket.emit('manual_control', { command: cmd, token });
  };

  const logout = () => {
    localStorage.removeItem('trashcan_token');
    tokenRef.current = null;
    navigate('/login', { replace: true });
  };

  const sendFeedback = (id, feedback) => {
    const socket = socketRef.current;
    const token = tokenRef.current;
    if (!socket || !token) return;

    socket.emit('ml_feedback', { id, feedback, token });

    setDetections((prev) =>
      prev.map((det) => (det.id === id ? { ...det, status: feedback } : det))
    );
  };

  const detectionItems =
    detections.length === 0
      ? [{ id: -1, __placeholder: true, text: 'Waiting for detections...' }]
      : detections;

  return (
    <div className="dashboard-container">
      {/* Manual Control */}
      <div className="dash-box">
        <h3>Manual Control</h3>
        <button onClick={() => sendCommand('F')}>Forward</button>
        <button onClick={() => sendCommand('B')}>Backward</button>
        <button onClick={() => sendCommand('L')}>Left</button>
        <button onClick={() => sendCommand('R')}>Right</button>
        <button
          onClick={() => sendCommand('S')}
          style={{ backgroundColor: '#dc3545' }}
        >
          STOP
        </button>
        <hr />
        <button onClick={() => sendCommand('P')}>Pickup</button>
        <button onClick={() => sendCommand('D')}>Drop</button>
        <button onClick={() => sendCommand('X')}>Reset Arm</button>
        <br />
        <br />
        <button onClick={logout} style={{ backgroundColor: '#6c757d' }}>
          Logout
        </button>
      </div>

      {/* Health & Accuracy */}
      <div className="dash-box">
        <h3>Health &amp; Accuracy</h3>
        <div className="health-item">
          <span>Motors:</span> <strong>{health.motors}</strong>
        </div>
        <div className="health-item">
          <span>Arm:</span> <strong>{health.arm}</strong>
        </div>
        <div className="health-item">
          <span>Detection Service:</span> <strong>{health.ml_service}</strong>
        </div>
        <hr />
        <h4>ML Accuracy</h4>
        <span>{accuracy}</span>%
      </div>

      {/* Activity Chart */}
      <div className="dash-box">
        <h3>Tool Activity Chart</h3>
        <canvas ref={chartCanvasRef} />
      </div>

      {/* ESP32 Stream */}
      <div className="dash-box">
        <h3>ESP32 Live Stream</h3>
        <img
          src={`${ESP32_BASE_URL}/jpg?t=${frameTs}`}
          width="640"
          height="480"
          alt="ESP32 live stream"
        />
      </div>

      {/* Hardware Template */}
      <div className="dash-box">
        <h3>Hardware Template</h3>
        <div className="template-diagram">
          <p>[ESP32 (Cam)] --- WiFi --- [Router] --- [Backend / MQTT]</p>
          <p>[ESP32 (Motors)] --- [Motor Driver] --- [Motors]</p>
          <p>[ESP32 (Arm)] --- [Servo Driver] --- [Arm Servos]</p>
        </div>
      </div>

      {/* ML Detections */}
      <div className="dash-box">
        <h3>ML Detections</h3>
        <ul>
          {detectionItems.map((det) =>
            det.__placeholder ? (
              <li key="placeholder">{det.text}</li>
            ) : (
              <li key={det.id}>
                <span>
                  {det.item} ({det.confidence}%)
                </span>
                <span className="feedback-buttons">
                  {det.status && det.status !== 'pending' ? (
                    <em>{det.status}</em>
                  ) : (
                    <>
                      <button
                        className="btn-correct"
                        onClick={() => sendFeedback(det.id, 'correct')}
                      >
                        ✔
                      </button>
                      <button
                        className="btn-incorrect"
                        onClick={() => sendFeedback(det.id, 'incorrect')}
                      >
                        ❌
                      </button>
                    </>
                  )}
                </span>
              </li>
            )
          )}
        </ul>
      </div>
    </div>
  );
};

export default Dashboard;