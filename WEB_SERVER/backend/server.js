// server.js

require('dotenv').config();
const express = require('express');
const http = require('http');
const cors = require('cors');
const { Server } = require('socket.io');
const mqtt = require('mqtt');
const jwt = require('jsonwebtoken');

const app = express();
const server = http.createServer(app);
const io = new Server(server, {
  cors: { origin: '*' }
});

app.use(cors());
app.use(express.json());

// ====== Config ======
const PORT = process.env.PORT || 3001;
const MQTT_BROKER = process.env.MQTT_BROKER || 'mqtt://test.mosquitto.org';
const JWT_SECRET = process.env.JWT_SECRET || 'supersecret';

// ====== Dummy Users ======
const users = [
  { id: 1, username: 'admin', password: '1234' },
  { id: 2, username: 'user',  password: '1234' }
];

// ====== MQTT Setup ======
const mqttClient = mqtt.connect(MQTT_BROKER);

mqttClient.on('connect', () => {
  console.log('Connected to MQTT broker:', MQTT_BROKER);
  mqttClient.subscribe('smarttrashcan/#', (err) => {
    if (err) {
      console.error('MQTT subscribe error:', err.message);
    } else {
      console.log('Subscribed to smarttrashcan/# topics');
    }
  });
});

mqttClient.on('error', (err) => {
  console.error('MQTT error:', err.message);
});

mqttClient.on('message', (topic, message) => {
  // NodeMCU publishes JSON
  let data = null;
  try {
    data = JSON.parse(message.toString());
  } catch (e) {
    console.error('MQTT parse error:', e.message, 'topic:', topic);
    return;
  }

  if (topic === 'smarttrashcan/health') {
    io.emit('health_update', data);
  }
  else if (topic === 'smarttrashcan/accuracy') {
    io.emit('accuracy_update', data);
  }
  else if (topic === 'smarttrashcan/detection') {
    io.emit('detection_update', data);
  }
  else if (topic === 'smarttrashcan/activity') {
    io.emit('chart_update', data);
  }
  // optional - ack feedback
  else if (topic === 'smarttrashcan/feedback_ack') {
    // you can forward to frontend if needed
    // io.emit('feedback_ack', data);
  }
});

// ====== Socket.IO (browser <-> server) ======
io.on('connection', (socket) => {
  console.log('Client connected:', socket.id);

  // Manual control from dashboard
  socket.on('manual_control', ({ command, token }) => {
    console.log('Manual command:', command);

    // (Optional) JWT check
    if (!token) {
      socket.emit('error_message', { message: 'No token' });
      return;
    }
    try {
      jwt.verify(token, JWT_SECRET);
    } catch (e) {
      socket.emit('error_message', { message: 'Invalid token' });
      return;
    }

    // Publish to NodeMCU via MQTT
    mqttClient.publish(
      'smarttrashcan/manual',
      JSON.stringify({ command }),
      { qos: 0, retain: false }
    );
  });

  // ML feedback from dashboard
  socket.on('ml_feedback', ({ id, feedback, token }) => {
    console.log('ML feedback:', id, feedback);

    if (!token) {
      socket.emit('error_message', { message: 'No token' });
      return;
    }
    try {
      jwt.verify(token, JWT_SECRET);
    } catch (e) {
      socket.emit('error_message', { message: 'Invalid token' });
      return;
    }

    mqttClient.publish(
      'smarttrashcan/feedback',
      JSON.stringify({ id, feedback }),
      { qos: 0, retain: false }
    );
  });

  socket.on('disconnect', () => {
    console.log('Client disconnected:', socket.id);
  });
});

// ====== REST API ======

// Simple health check
app.get('/', (req, res) => {
  res.send('Smart Trashcan Backend Running');
});

// Login
app.post('/login', (req, res) => {
  try {
    const { username, password } = req.body;
    if (!username || !password)
      return res.status(400).json({ message: 'Username and password required' });

    const user = users.find(
      u => u.username === username && u.password === password
    );
    if (!user)
      return res.status(401).json({ message: 'Invalid credentials' });

    const token = jwt.sign(
      { id: user.id, username: user.username },
      JWT_SECRET,
      { expiresIn: '1h' }
    );

    res.json({ token });
  } catch (e) {
    console.error(e);
    res.status(500).json({ message: 'Internal server error' });
  }
});

// Example protected route (optional)
app.get('/profile', (req, res) => {
  const authHeader = req.headers['authorization'];
  if (!authHeader)
    return res.status(401).json({ message: 'No token' });

  const token = authHeader.split(' ')[1];
  try {
    const decoded = jwt.verify(token, JWT_SECRET);
    res.json({ profile: decoded });
  } catch (e) {
    res.status(403).json({ message: 'Invalid token' });
  }
});

// ====== Start server ======
server.listen(PORT, () => {
  console.log(`Backend running on http://localhost:${PORT}`);
});