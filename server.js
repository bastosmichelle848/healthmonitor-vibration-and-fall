// ─── Health Monitor — REST API Server ───────────────────────────────────────
// Author: Michelle Bastos Silva — IPBeja 2025/2026
// Description: REST API for receiving and storing health monitoring data
//              from Node-RED (MQTT Gateway) and serving the web dashboard.
//
// Install dependencies:
//   npm install express sqlite3 cors
//
// Run:
//   node server.js
// ─────────────────────────────────────────────────────────────────────────────

const express = require('express');
const sqlite3 = require('sqlite3').verbose();
const cors    = require('cors');
const path    = require('path');

const app  = express();
const PORT = 3000;
const DB_PATH = path.join(__dirname, 'database.db');

// ─── Middleware ───────────────────────────────────────────────────────────────
app.use(cors());
app.use(express.json());
app.use(express.static(path.join(__dirname, 'public')));

// ─── Database Setup ───────────────────────────────────────────────────────────
const db = new sqlite3.Database(DB_PATH, (err) => {
  if (err) {
    console.error('Error opening database:', err.message);
  } else {
    console.log('Connected to SQLite database.');
  }
});

db.serialize(() => {
  // Table: vitals (heart rate + SpO2)
  db.run(`
    CREATE TABLE IF NOT EXISTS vitals (
      id        INTEGER PRIMARY KEY AUTOINCREMENT,
      heartRate REAL    NOT NULL,
      spO2      REAL    NOT NULL,
      timestamp DATETIME DEFAULT CURRENT_TIMESTAMP
    )
  `);

  // Table: quedas (vibration / fall events)
  db.run(`
    CREATE TABLE IF NOT EXISTS quedas (
      id        INTEGER PRIMARY KEY AUTOINCREMENT,
      nivel     TEXT    NOT NULL,
      contagem  INTEGER NOT NULL,
      timestamp DATETIME DEFAULT CURRENT_TIMESTAMP
    )
  `);

  console.log('Database tables ready.');
});

// ─── Routes: Vitals ──────────────────────────────────────────────────────────

// POST /vitals — receive heart rate and SpO2 from Node-RED
app.post('/vitals', (req, res) => {
  const { heartRate, spO2 } = req.body;

  if (!heartRate || !spO2) {
    return res.status(400).json({ error: 'heartRate and spO2 are required' });
  }

  const sql = `INSERT INTO vitals (heartRate, spO2) VALUES (?, ?)`;
  db.run(sql, [heartRate, spO2], function (err) {
    if (err) {
      console.error('Error inserting vitals:', err.message);
      return res.status(500).json({ error: err.message });
    }
    console.log(`[VITALS] HR: ${heartRate} bpm | SpO2: ${spO2}%`);
    res.status(201).json({ id: this.lastID, heartRate, spO2 });
  });
});

// GET /vitals — return all vitals history
app.get('/vitals', (req, res) => {
  const limit = req.query.limit || 100;
  const sql   = `SELECT * FROM vitals ORDER BY timestamp DESC LIMIT ?`;
  db.all(sql, [limit], (err, rows) => {
    if (err) {
      return res.status(500).json({ error: err.message });
    }
    res.json(rows);
  });
});

// GET /vitals/latest — return the most recent reading
app.get('/vitals/latest', (req, res) => {
  const sql = `SELECT * FROM vitals ORDER BY timestamp DESC LIMIT 1`;
  db.get(sql, [], (err, row) => {
    if (err) {
      return res.status(500).json({ error: err.message });
    }
    res.json(row || {});
  });
});

// ─── Routes: Quedas (Falls / Vibration) ──────────────────────────────────────

// POST /queda — receive vibration/fall event from Node-RED
app.post('/queda', (req, res) => {
  const { nivel, contagem } = req.body;

  if (!nivel || contagem === undefined) {
    return res.status(400).json({ error: 'nivel and contagem are required' });
  }

  const sql = `INSERT INTO quedas (nivel, contagem) VALUES (?, ?)`;
  db.run(sql, [nivel, contagem], function (err) {
    if (err) {
      console.error('Error inserting queda:', err.message);
      return res.status(500).json({ error: err.message });
    }
    console.log(`[QUEDA] Level: ${nivel} | Count: ${contagem}`);
    res.status(201).json({ id: this.lastID, nivel, contagem });
  });
});

// GET /quedas — return all fall/vibration events
app.get('/quedas', (req, res) => {
  const limit = req.query.limit || 100;
  const sql   = `SELECT * FROM quedas ORDER BY timestamp DESC LIMIT ?`;
  db.all(sql, [limit], (err, rows) => {
    if (err) {
      return res.status(500).json({ error: err.message });
    }
    res.json(rows);
  });
});

// ─── Routes: Dashboard ───────────────────────────────────────────────────────

// GET / — serve the web dashboard (index.html in /public folder)
app.get('/', (req, res) => {
  res.sendFile(path.join(__dirname, 'public', 'index.html'));
});

// ─── Start Server ─────────────────────────────────────────────────────────────
app.listen(PORT, () => {
  console.log(`\n🏥 Health Monitor API running at http://localhost:${PORT}`);
  console.log(`   POST /vitals   — receive heart rate and SpO2`);
  console.log(`   GET  /vitals   — get vitals history`);
  console.log(`   POST /queda    — receive fall/vibration event`);
  console.log(`   GET  /quedas   — get fall events history\n`);
});

// ─── Graceful Shutdown ────────────────────────────────────────────────────────
process.on('SIGINT', () => {
  db.close((err) => {
    if (err) console.error(err.message);
    console.log('\nDatabase connection closed. Server stopped.');
    process.exit(0);
  });
});
