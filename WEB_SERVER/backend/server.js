// server.js
require('dotenv').config();
const path = require('path');
const express = require('express');
const http = require('http');
const { Server } = require('socket.io');
const cors = require('cors');
const mqtt = require('mqtt');
const jwt = require('jsonwebtoken');

const app = express();
const server = http.createServer(app);

// ====== Config ======
const PORT = process.env.PORT || 3001;
const MQTT_BROKER = process.env.MQTT_BROKER || 'mqtt://test.mosquitto.org';
const JWT_SECRET = process.env.JWT_SECRET || 'supersecret';

// ====== Middlewares ======
app.use(cors({ origin: '*' }));
app.use(express.json());

// ====== Socket.IO ======
const io = new Server(server, {
  cors: {
    origin: '*',
    methods: ['GET', 'POST'],
  },
});

// ====== Dummy Users ======
const users = [
  { id: 1, username: 'admin', password: '1234' },
  { id: 2, username: 'user', password: '1234' },
];

// ====== JWT helper ======
function verifyToken(token) {
  if (!token) return null;
  try {
    return jwt.verify(token, JWT_SECRET);
  } catch (err) {
    return null;
  }
}

// ====== MQTT Setup ======
const mqttClient = mqtt.connect(MQTT_BROKER, {
  reconnectPeriod: 5000,
  connectTimeout: 10 * 1000,
});

mqttClient.on('connect', () => {
  console.log('✅ Connected to MQTT broker:', MQTT_BROKER);
  mqttClient.subscribe('smarttrashcan/#', (err) => {
    if (err) console.error('❌ Failed to subscribe', err);
    else console.log('✅ Subscribed to smarttrashcan/#');
  });
});

mqttClient.on('error', (err) => {
  console.error('⚠️ MQTT error:', err.message);
});

mqttClient.on('reconnect', () => {
  console.log('🔄 MQTT client reconnecting...');
});

mqttClient.on('offline', () => {
  console.log('⚠️ MQTT client offline');
});

mqttClient.on('message', (topic, message) => {
  let data;
  try {
    data = JSON.parse(message.toString());
  } catch (err) {
    console.error('MQTT parse error on', topic, err);
    return;
  }

  switch (topic) {
    case 'smarttrashcan/health':
      io.emit('health_update', data);
      break;
    case 'smarttrashcan/accuracy':
      io.emit('accuracy_update', data);
      break;
    case 'smarttrashcan/detection':
      io.emit('detection_update', data);
      break;
    case 'smarttrashcan/activity':
      io.emit('chart_update', data);
      break;
    default:
      console.log('MQTT message on', topic, data);
  }
});

// ====== Socket.IO events ======
io.on('connection', (socket) => {
  console.log('Client connected:', socket.id);

  socket.on('manual_control', ({ command, token }) => {
    const user = verifyToken(token);
    if (!user) {
      socket.emit('error_message', { message: 'Invalid or missing token' });
      return;
    }

    const allowed = ['F', 'B', 'L', 'R', 'S', 'P', 'D', 'X'];
    if (!allowed.includes(command)) {
      socket.emit('error_message', { message: 'Invalid command' });
      return;
    }

    console.log(`Manual command from ${user.username}:`, command);

    mqttClient.publish(
      'smarttrashcan/manual',
      JSON.stringify({ command, user: user.username }),
      (err) => {
        if (err) {
          console.error('Failed to publish manual command:', err);
          socket.emit('error_message', { message: 'MQTT publish failed' });
        }
      }
    );
  });

  socket.on('ml_feedback', ({ id, feedback, token }) => {
    const user = verifyToken(token);
    if (!user) {
      socket.emit('error_message', { message: 'Invalid or missing token' });
      return;
    }

    if (!['correct', 'incorrect'].includes(feedback)) {
      socket.emit('error_message', { message: 'Invalid feedback type' });
      return;
    }

    console.log(`ML feedback from ${user.username}:`, id, feedback);

    mqttClient.publish(
      'smarttrashcan/feedback',
      JSON.stringify({ id, feedback, user: user.username }),
      (err) => {
        if (err) {
          console.error('Failed to publish feedback:', err);
          socket.emit('error_message', { message: 'MQTT publish failed' });
        }
      }
    );
  });

  socket.on('disconnect', () => {
    console.log('Client disconnected:', socket.id);
  });
});

// ====== REST API ======
app.get('/', (req, res) => {
  res.send('Smart Trashcan Backend Running');
});

app.post('/login', (req, res) => {
  try {
    const { username, password } = req.body;
    if (!username || !password)
      return res
        .status(400)
        .json({ message: 'Username and password required' });

    const user = users.find(
      (u) => u.username === username && u.password === password
    );
    if (!user) return res.status(401).json({ message: 'Invalid credentials' });

    const token = jwt.sign(
      { id: user.id, username: user.username },
      JWT_SECRET,
      { expiresIn: '1h' }
    );
    return res.json({ token });
  } catch (err) {
    console.error('Login error', err);
    return res.status(500).json({ message: 'Internal server error' });
  }
});

app.get('/profile', (req, res) => {
  const authHeader = req.headers['authorization'];
  if (!authHeader)
    return res.status(401).json({ message: 'No token provided' });

  const token = authHeader.split(' ')[1];
  const decoded = verifyToken(token);
  if (!decoded) return res.status(403).json({ message: 'Invalid token' });

  res.json({ profile: decoded });
});

// Optional: serve React build
if (process.env.SERVE_REACT === 'true') {
  const buildPath = path.join(__dirname, 'build');
  app.use(express.static(buildPath));
  app.get('*', (req, res) => {
    res.sendFile(path.join(buildPath, 'index.html'));
  });
}

server.listen(PORT, () => {
  console.log(`Server running on http://localhost:${PORT}`);
});