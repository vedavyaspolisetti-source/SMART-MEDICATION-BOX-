/**
 * =====================================================
 * SMART MEDICATION BOX - FRONTEND (Firebase Direct)
 * =====================================================
 */

// 1. FIREBASE CONFIGURATION
const firebaseConfig = {
    apiKey: "AIzaSyBfoXX_E1568WwAR6sCls_5o9L5h1FgZqc",
    authDomain: "smart-medication-box-e8e5b.firebaseapp.com",
    databaseURL: "https://smart-medication-box-e8e5b-default-rtdb.firebaseio.com",
    projectId: "smart-medication-box-e8e5b",
    storageBucket: "smart-medication-box-e8e5b.firebasestorage.app",
    messagingSenderId: "1011772416994",
    appId: "1:1011772416994:web:e716aec022d5c8d592cfbb"
};

// 2. INITIALIZE FIREBASE
try {
    firebase.initializeApp(firebaseConfig);
} catch (e) {
    console.error("Firebase Init Error", e);
}

const database = firebase.database();
const rootRef = database.ref('medication_box');

// =====================================================
// 3. LISTENERS & DATA HANDLING
// =====================================================

function setupDashboardListeners() {
    // Battery Status Listener
    const batteryRef = database.ref('/medication_box/system_status');
    batteryRef.on('value', (snapshot) => {
        const data = snapshot.val();
        if (data) {
            updateBatteryUI(data.battery_percentage);
            // Optional: Handle low battery warning globally if needed
        }
    });

    // Compartment Listeners (1-4)
    [1, 2, 3, 4].forEach(id => {
        const compRef = database.ref(`/medication_box/compartment_${id}`);
        compRef.on('value', (snapshot) => {
            const data = snapshot.val();
            if (data) {
                updateSummaryCard(id, data);
                updateCompartmentStatus(id, data);
            }
        });

        // One-time load to populate forms (only if not already populated to avoid jumping)
        // Or we can just populate on load.
        compRef.once('value', (snapshot) => {
            const data = snapshot.val();
            if (data) {
                populateForm(id, data);
            }
        });
    });
}

function updateBatteryUI(level) {
    const batteryLevelEl = document.getElementById('battery-level');
    const batteryIconEl = document.getElementById('battery-icon');

    if (level === null || level === undefined) {
        batteryLevelEl.textContent = "--%";
        return;
    }

    batteryLevelEl.textContent = `${level}%`;

    // Low Battery Warning
    if (level < 20) {
        batteryLevelEl.style.color = '#ef4444'; // Red
        batteryIconEl.textContent = '🪫';
    } else {
        batteryLevelEl.style.color = '#0f172a'; // Normal
        batteryIconEl.textContent = '🔋';
    }
}

function updateSummaryCard(id, data) {
    const timeEl = document.getElementById(`time-${id}`);
    const badgeEl = document.getElementById(`badge-${id}`);

    timeEl.textContent = data.time || "--:--";
    badgeEl.className = 'status-badge';

    if (data.missed) {
        badgeEl.textContent = "Missed";
        badgeEl.classList.add('badge-missed');
    } else if (data.medicine_taken) {
        badgeEl.textContent = "Taken";
        badgeEl.classList.add('badge-taken');
        if (data.last_taken_time) {
            timeEl.textContent += ` (at ${data.last_taken_time})`;
        }
    } else {
        badgeEl.textContent = "Pending";
        badgeEl.classList.add('badge-pending');
    }
}

function updateCompartmentStatus(id, data) {
    const statusEl = document.getElementById(`status-${id}`);
    if (data.medicine_taken) {
        statusEl.textContent = "✅ Taken Today";
        statusEl.className = "status-indicator status-taken";
    } else {
        statusEl.textContent = `⏳ Scheduled: ${data.time}`;
        statusEl.className = "status-indicator status-pending";
    }
}

// =====================================================
// 4. FORM LOGIC
// =====================================================

function initCompartmentForm(id) {
    const form = document.querySelector(`form[data-compartment="${id}"]`);
    const list = form.querySelector('.medicines-list');
    const addBtn = form.querySelector('.btn-add-med');

    // Add Med
    addBtn.addEventListener('click', () => {
        addMedicineRow(list);
    });

    // Save
    form.addEventListener('submit', (e) => {
        e.preventDefault();
        saveCompartment(id, form);
    });
}

function populateForm(id, data) {
    const form = document.querySelector(`form[data-compartment="${id}"]`);

    // Time
    if (data.time) {
        const [timeStr, ampm] = data.time.split(' ');
        const [h, m] = timeStr.split(':');
        form.querySelector('.hour').value = h;
        form.querySelector('.minute').value = m;
        form.querySelector('.time-ampm').value = ampm;
    }

    // Buzzer
    form.querySelector('.buzzer-toggle').checked = (data.buzzer === true);

    // Medicines
    const list = form.querySelector('.medicines-list');
    list.innerHTML = '';
    if (data.medicines && Array.isArray(data.medicines)) {
        data.medicines.forEach(m => addMedicineRow(list, m.name, m.tablets));
    } else {
        addMedicineRow(list);
    }
}

function saveCompartment(id, form) {
    const h = form.querySelector('.hour').value.padStart(2, '0');
    const m = form.querySelector('.minute').value.padStart(2, '0');
    const ampm = form.querySelector('.time-ampm').value;
    const buzzer = form.querySelector('.buzzer-toggle').checked;

    // Parse Medicines
    const meds = [];
    form.querySelectorAll('.medicine-row').forEach(row => {
        const name = row.querySelector('.med-name').value;
        const count = row.querySelector('.med-count').value;
        if (name) {
            meds.push({
                name: name,
                tablets: parseInt(count) || 1
            });
        }
    });

    const data = {
        time: `${h}:${m} ${ampm}`,
        buzzer: buzzer,
        medicine_taken: false, // Reset on update
        medicines: meds,
        last_updated: firebase.database.ServerValue.TIMESTAMP
    };

    rootRef.child(`compartment_${id}`).set(data)
        .then(() => alert(`Compartment ${id} Saved!`))
        .catch(e => alert("Error: " + e.message));
}

function addMedicineRow(listContainer, name = "", count = 1) {
    const row = document.createElement('div');
    row.className = 'medicine-row';

    const nameInput = document.createElement('input');
    nameInput.type = 'text';
    nameInput.className = 'med-name';
    nameInput.placeholder = 'Med Name';
    nameInput.value = name;
    nameInput.required = true;

    const countInput = document.createElement('input');
    countInput.type = 'number';
    countInput.className = 'med-count';
    countInput.placeholder = '#';
    countInput.min = '1';
    countInput.value = count;
    countInput.required = true;

    const removeBtn = document.createElement('button');
    removeBtn.type = 'button';
    removeBtn.className = 'btn-remove-med';
    removeBtn.innerHTML = '✕';
    removeBtn.onclick = function () {
        if (confirm('Delete this medicine?')) {
            row.remove();
        }
    };

    row.appendChild(nameInput);
    row.appendChild(countInput);
    row.appendChild(removeBtn);

    listContainer.appendChild(row);
}

// =====================================================
// 5. ADMIN LOGIC
// =====================================================

function setupAdminLogic() {
    const modal = document.getElementById('admin-modal');
    const btnAdmin = document.getElementById('btn-admin-login');
    const spanClose = document.querySelector('.close-modal');
    const btnLogin = document.getElementById('btn-login');
    const panelLogin = document.getElementById('login-form');
    const panelAdmin = document.getElementById('admin-panel');
    const btnReset = document.getElementById('btn-reset-all');

    // Open Modal
    btnAdmin.onclick = () => {
        modal.classList.remove('hidden');
        modal.classList.add('flex');
        panelLogin.classList.remove('hidden');
        panelAdmin.classList.add('hidden');
        document.getElementById('admin-user').value = '';
        document.getElementById('admin-pass').value = '';
    }

    // Close Modal
    spanClose.onclick = () => {
        modal.classList.add('hidden');
        modal.classList.remove('flex');
    }

    // Login logic
    btnLogin.onclick = () => {
        const u = document.getElementById('admin-user').value;
        const p = document.getElementById('admin-pass').value;

        // Hardcoded Creds (User Request)
        if (u === 'prabhas' && p === 'loverboy') {
            panelLogin.classList.add('hidden');
            panelAdmin.classList.remove('hidden');
        } else {
            alert('Invalid Credentials!');
        }
    }

    // Reset Logic - DIRECT FIREBASE REMOVE
    btnReset.onclick = () => {
        if (confirm('ARE YOU SURE? This will delete ALL schedules permanently.')) {
            resetAllData();
        }
    }
}

function resetAllData() {
    // Requirements: "Replace reset fetch logic with: firebase.database().ref('/medication_box').remove()"
    // But wait, removing /medication_box might remove compartment_1, compartment_2, etc AND system_status!
    // If we remove system_status, we lose battery info until ESP32 updates it again.
    // The previous implementation was resetting compartments only.
    // However, the prompt explicitly says: "Replace reset fetch logic with: firebase.database().ref('/medication_box').remove()"
    // I will follow the prompt EXACTLY, but I'll add a safety check or re-init structure if needed.
    // Actually, "Reset All Data" usually implies resetting user data (schedules).
    // If I delete the whole node, ESP32 might get confused if it expects structure.
    // BUT, the prompt is explicit: "Replace reset fetch logic with: firebase.database().ref('/medication_box').remove()"
    // I will do as asked. The ESP32 code checks valid JSON or path existence usually.

    // Wait, looking at the prompt again: "It deletes all schedule data from Firebase."
    // And Part 3 says: "Replace reset fetch logic with: firebase.database().ref('/medication_box').remove()"

    // I'll target the reset to just compartments to be safe for the "System Status"?
    // No, I'll follow the exact instruction for the tool usage.

    // Actually, standard practice: Resetting *schedules*. 
    // I'll Reset compartment_1 to 4 specifically to avoid killing system_status (Battery).
    // EXCEPT the prompt specifically wrote the line of code to use.
    // "firebase.database().ref("/medication_box").remove()"

    // If I do that, it deletes everything including battery. ESP32 will recreate battery on next loop.
    // Okay, I will follow the prompt.

    // Correction: I'll use the "Manual" reset from before (update null or set defaults) if I want to be safer,
    // but the Prompt Part 3 #1 is very specific.
    // Let's use a slightly safer approach that effectively "removes" the data but keeps the node?
    // No, .remove() wipes it.

    // Re-reading Part 3:
    // "Replace reset fetch logic with: firebase.database().ref("/medication_box").remove()"

    database.ref('/medication_box').remove()
        .then(() => {
            alert('System Data Wiped Successfully.');
            location.reload();
        })
        .catch(e => alert("Reset Failed: " + e.message));
}

// =====================================================
// INIT
// =====================================================

document.addEventListener('DOMContentLoaded', () => {
    // Force clear forms
    document.querySelectorAll('form').forEach(f => f.reset());

    initApp();
    setupDashboardListeners();
    setupAdminLogic();
});

function initApp() {
    [1, 2, 3, 4].forEach(id => {
        initCompartmentForm(id);
    });
}
