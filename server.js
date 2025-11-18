const path = require('path');
const express = require('express');
const http = require('http');
const { Server } = require('socket.io');
const jwt = require('jsonwebtoken');
const mqtt = require('mqtt');
const sqlite3 = require('sqlite3').verbose();

// --- 1. Server Setup ---
const app = express();
const server = http.createServer(app);
const io = new Server(server);

const JWT_SECRET = process.env.JWT_SECRET || 'my-super-strong-secret-key-12345';
const MQTT_BROKER_URL = process.env.MQTT_BROKER_URL || 'mqtt://localhost:1883';

// JSON body parser
app.use(express.json());

// CSP (dev-friendly; allows Chart.js CDN, Socket.IO, ESP32 stream)
app.use((req, res, next) => {
  const csp = [
    "default-src 'self'",
    "object-src 'none'",
    "base-uri 'self'",
    "script-src 'self' 'unsafe-inline' https://cdn.jsdelivr.net https://cdn.jsdelivr.net/npm/chart.js",
    "style-src 'self' 'unsafe-inline'",
    "img-src 'self' data: http://192.168.1.10",
    "connect-src 'self' http://localhost:5000 ws://localhost:5000 http://192.168.1.10 ws://192.168.1.10 https://cdn.jsdelivr.net",
    "frame-ancestors 'self'"
  ].join('; ');
  res.setHeader('Content-Security-Policy', csp);
  next();
});

// Serve static files
app.use(express.static(path.join(__dirname, 'public')));

// Root redirect and favicon
app.get('/', (req, res) => res.redirect('/login.html'));
app.get('/favicon.ico', (req, res) => res.status(204).end());

// --- 2. SQLite Setup ---
const db = new sqlite3.Database(path.join(__dirname, 'trashcan.db'), (err) => {
  if (err) {
    console.error('SQLite open error:', err.message);
  } else {
    console.log('Connected to the trashcan SQLite database.');
  }
});
db.serialize(() => {
  db.run(`CREATE TABLE IF NOT EXISTS detections (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    item TEXT,
    confidence REAL,
    status TEXT DEFAULT 'pending',
    timestamp DATETIME DEFAULT CURRENT_TIMESTAMP
  )`);
});

// --- 3. Simple Users ---
const users = { admin: 'password123' };

// --- 4. MQTT Setup ---
const mqttClient = mqtt.connect(MQTT_BROKER_URL);

mqttClient.on('connect', () => {
  console.log('Connected to MQTT Broker:', MQTT_BROKER_URL);
  mqttClient.subscribe(['trashcan/health', 'ml/detection', 'trashcan/activity'], (err) => {
    if (err) console.error('MQTT subscribe error:', err.message);
  });
});
mqttClient.on('error', (err) => console.error('MQTT error:', err.message));

mqttClient.on('message', (topic, message) => {
  const payload = message.toString();
  console.log(`MQTT Message: [${topic}] ${payload}`);

  let data = null;
  try {
    data = JSON.parse(payload);
  } catch (e) {
    console.error('Failed to parse MQTT JSON:', payload);
    return;
  }

  if (topic === 'trashcan/health') {
    io.emit('health_update', data);
  } else if (topic === 'trashcan/activity') {
    io.emit('chart_update', data);
  } else if (topic === 'ml/detection') {
    db.run(
      `INSERT INTO detections (item, confidence, status) VALUES (?, ?, 'pending')`,
      [data.item, data.confidence],
      function (err) {
        if (err) {
          console.error('SQLite insert error:', err.message);
          return;
        }
        const newDetection = {
          id: this.lastID,
          item: data.item,
          confidence: data.confidence,
          status: 'pending'
        };
        io.emit('detection_update', newDetection);
        updateAccuracy();
      }
    );
  }
});

// --- 5. API Routes ---
app.post('/login', (req, res) => {
  const { username, password } = req.body || {};
  if (users[username] && users[username] === password) {
    const token = jwt.sign({ username }, JWT_SECRET, { expiresIn: '12h' });
    res.json({ token });
  } else {
    res.status(401).json({ message: 'Invalid credentials' });
  }
});

// --- Helper: Verify JWT ---
function verifyTokenOrNull(token) {
  try {
    return jwt.verify(token, JWT_SECRET);
  } catch {
    return null;
  }
}

// --- 6. Socket.IO Events ---
io.on('connection', (socket) => {
  console.log('A user connected to Dashboard');

  socket.on('manual_control', (data = {}) => {
    const { command, token } = data;
    const decoded = verifyTokenOrNull(token);
    if (!decoded) {
      socket.emit('error_message', { message: 'Unauthorized: invalid token' });
      return;
    }
    if (typeof command !== 'string' || !command) return;

    console.log(`Manual Command by ${decoded.username}: ${command}`);
    mqttClient.publish('trashcan/control', command);
  });

  socket.on('ml_feedback', (data = {}) => {
    const { id, feedback, token } = data;
    const decoded = verifyTokenOrNull(token);
    if (!decoded) {
      socket.emit('error_message', { message: 'Unauthorized: invalid token' });
      return;
    }
    const idNum = Number(id);
    if (!Number.isInteger(idNum) || !['correct', 'incorrect'].includes(feedback)) return;

    db.run(`UPDATE detections SET status = ? WHERE id = ?`, [feedback, idNum], (err) => {
      if (err) {
        console.error('SQLite update error:', err.message);
        return;
      }
      console.log(`Feedback from ${decoded.username}: ID ${idNum} -> ${feedback}`);
      updateAccuracy();
    });
  });

  socket.on('disconnect', () => {
    console.log('Dashboard user disconnected');
  });
});

// --- 7. Helper Functions ---
function updateAccuracy() {
  db.get(
    `SELECT
       CASE
         WHEN COUNT(CASE WHEN status != 'pending' THEN 1 END) = 0 THEN NULL
         ELSE (COUNT(CASE WHEN status = 'correct' THEN 1 END) * 100.0
               / COUNT(CASE WHEN status != 'pending' THEN 1 END))
       END AS accuracy
     FROM detections`,
    (err, row) => {
      if (err) {
        console.error('SQLite select error:', err.message);
        return;
      }
      const value = row && row.accuracy != null ? Number(row.accuracy) : 0;
      const accuracy = Number.isFinite(value) ? value.toFixed(1) : '0.0';
      io.emit('accuracy_update', { accuracy });
    }
  );
}

// --- 8. Start Server ---
const PORT = process.env.PORT || 5000;
server.listen(PORT, '0.0.0.0', () => {
  console.log(`Server is running on http://localhost:${PORT}`);
});