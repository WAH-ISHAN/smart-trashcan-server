// server.js
require('dotenv').config();
const express = require('express');
const http = require('http');
const { Server } = require('socket.io');
const cors = require('cors');
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
  { id: 2, username: 'user', password: '1234' }
];

// ====== MQTT Setup ======
const mqttClient = mqtt.connect(MQTT_BROKER);
mqttClient.on('connect', () => {
  console.log('Connected to MQTT broker');
  mqttClient.subscribe('smarttrashcan/#', (err) => {
    if (!err) console.log('Subscribed to smarttrashcan topics');
  });
});
mqttClient.on('message', (topic, message) => {
  try {
    const data = JSON.parse(message.toString());
    if (topic === 'smarttrashcan/health') io.emit('health_update', data);
    if (topic === 'smarttrashcan/accuracy') io.emit('accuracy_update', data);
    if (topic === 'smarttrashcan/detection') io.emit('detection_update', data);
    if (topic === 'smarttrashcan/activity') io.emit('chart_update', data);
  } catch (err) {
    console.error('MQTT message parse error', err);
  }
});

// ====== Socket.IO ======
io.on('connection', (socket) => {
  console.log('Client connected:', socket.id);

  socket.on('manual_control', ({ command, token }) => {
    console.log('Manual command:', command);
    mqttClient.publish('smarttrashcan/manual', JSON.stringify({ command }));
  });

  socket.on('ml_feedback', ({ id, feedback, token }) => {
    console.log('ML feedback:', id, feedback);
    mqttClient.publish('smarttrashcan/feedback', JSON.stringify({ id, feedback }));
  });

  socket.on('disconnect', () => console.log('Client disconnected:', socket.id));
});

// ====== REST API ======

// Health check
app.get('/', (req, res) => res.send('Smart Trashcan Backend Running'));

// Login
app.post('/login', (req, res) => {
  try {
    const { username, password } = req.body;
    if (!username || !password)
      return res.status(400).json({ message: 'Username and password required' });

    const user = users.find(u => u.username === username && u.password === password);
    if (!user)
      return res.status(401).json({ message: 'Invalid credentials' });

    const token = jwt.sign({ id: user.id, username: user.username }, JWT_SECRET, { expiresIn: '1h' });
    return res.json({ token });
  } catch (err) {
    console.error(err);
    return res.status(500).json({ message: 'Internal server error' });
  }
});

// Example protected route
app.get('/profile', (req, res) => {
  const authHeader = req.headers['authorization'];
  if (!authHeader) return res.status(401).json({ message: 'No token' });

  const token = authHeader.split(' ')[1];
  try {
    const decoded = jwt.verify(token, JWT_SECRET);
    res.json({ profile: decoded });
  } catch (err) {
    return res.status(403).json({ message: 'Invalid token' });
  }
});

// ====== Start server ======
server.listen(PORT, () => console.log(`Server running on http://localhost:${PORT}`));
