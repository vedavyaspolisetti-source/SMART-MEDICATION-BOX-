const express = require('express');
const bodyParser = require('body-parser');
const cors = require('cors');
const admin = require('firebase-admin');
const path = require('path');
require('dotenv').config();

const app = express();
const PORT = process.env.PORT || 3000;

// Middleware
app.use(cors());
app.use(bodyParser.json());
app.use(express.static(path.join(__dirname, '../frontend'))); // Serve Frontend

// Firebase Admin Init
try {
    const serviceAccount = require(process.env.FIREBASE_SERVICE_ACCOUNT_PATH);
    admin.initializeApp({
        credential: admin.credential.cert(serviceAccount),
        databaseURL: process.env.FIREBASE_DATABASE_URL
    });
    console.log("Firebase Admin Initialized");
} catch (error) {
    console.error("Firebase Init Error:", error.message);
    console.error("Using Mock Data for Dev (until service account is provided)");
}

const db = admin.database();

// =====================================================
// API ENDPOINTS
// =====================================================

// 1. Get System Status (Battery)
app.get('/api/status', async (req, res) => {
    try {
        const snapshot = await db.ref('/medication_box/system_status').once('value');
        res.json(snapshot.val() || {});
    } catch (error) {
        res.status(500).json({ error: error.message });
    }
});

// 2. Get All Compartments
app.get('/api/compartments', async (req, res) => {
    try {
        const snapshot = await db.ref('/medication_box').once('value');
        const data = snapshot.val() || {};
        // Filter only compartments 1-4
        const compartments = {};
        for (let i = 1; i <= 4; i++) {
            compartments[`compartment_${i}`] = data[`compartment_${i}`] || {};
        }
        res.json(compartments);
    } catch (error) {
        res.status(500).json({ error: error.message });
    }
});

// 3. Update Compartment Schedule
app.post('/api/compartment/:id', async (req, res) => {
    const id = req.params.id;
    const data = req.body;

    if (id < 1 || id > 4) return res.status(400).json({ error: "Invalid ID" });

    try {
        await db.ref(`/medication_box/compartment_${id}`).update(data);
        res.json({ success: true, message: `Compartment ${id} updated` });
    } catch (error) {
        res.status(500).json({ error: error.message });
    }
});

// 4. Admin Reset (Protected)
app.post('/api/admin/reset', async (req, res) => {
    const { username, password } = req.body;

    if (username !== process.env.ADMIN_USERNAME || password !== process.env.ADMIN_PASSWORD) {
        return res.status(401).json({ error: "Unauthorized" });
    }

    try {
        const updates = {};
        for (let i = 1; i <= 4; i++) {
            updates[`/medication_box/compartment_${i}`] = {
                time: "",
                buzzer: false,
                medicine_taken: false,
                missed: false,
                medicines: []
            };
        }
        await db.ref().update(updates);
        res.json({ success: true, message: "System Reset Complete" });
    } catch (error) {
        res.status(500).json({ error: error.message });
    }
});

// Serve frontend for root
app.get('*', (req, res) => {
    res.sendFile(path.join(__dirname, '../frontend/index.html'));
});

app.listen(PORT, () => {
    console.log(`Server running on http://localhost:${PORT}`);
});
