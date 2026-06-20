document.addEventListener('DOMContentLoaded', function() {
    const form = document.getElementById('settings-form');
    const statusMessage = document.getElementById('status-message');
    const saveButton = document.getElementById('save-button');

    // Lausche auf jede Eingabe im Formular
    form.addEventListener('input', function() {
        saveButton.classList.add('unsaved-changes');
        statusMessage.textContent = 'Ungespeicherte Änderungen vorhanden.';
    });

    // Funktion, um die aktuellen Einstellungen zu laden und das Formular auszufüllen
    function loadSettings() {
        fetch('/api/config')
            .then(response => response.json())
            .then(data => {
                // System
                document.getElementById('debug_enabled').checked = data.system.debug_enabled;
                document.getElementById('coarse_move_threshold').value = data.system.coarse_move_threshold;
                // Kalibrierung
                document.getElementById('min_pos').value = data.calibration.min_pos;
                document.getElementById('max_pos').value = data.calibration.max_pos;
                document.getElementById('min_voltage').value = data.calibration.min_voltage;
                document.getElementById('max_voltage').value = data.calibration.max_voltage;
                // Presets
                document.getElementById('p1').value = data.presets.p1;
                document.getElementById('p2').value = data.presets.p2;
                document.getElementById('p3').value = data.presets.p3;
            })
            .catch(error => {
                statusMessage.textContent = 'Fehler beim Laden der Konfiguration.';
                console.error('Error loading config:', error);
            });
    }

    // Funktion, um die neuen Einstellungen an den ESP32 zu senden
    function saveSettings(event) {
        event.preventDefault(); // Verhindert das Neuladen der Seite durch das Formular
        statusMessage.textContent = 'Speichere...';

        // Erstelle ein JSON-Objekt aus den Formulardaten
        const data = {
            system: {
                debug_enabled: document.getElementById('debug_enabled').checked,
                coarse_move_threshold: parseFloat(document.getElementById('coarse_move_threshold').value)
            },
            calibration: {
                min_pos: parseInt(document.getElementById('min_pos').value),
                max_pos: parseInt(document.getElementById('max_pos').value),
                min_voltage: parseFloat(document.getElementById('min_voltage').value),
                max_voltage: parseFloat(document.getElementById('max_voltage').value)
            },
            presets: {
                p1: parseInt(document.getElementById('p1').value),
                p2: parseInt(document.getElementById('p2').value),
                p3: parseInt(document.getElementById('p3').value)
            }
        };

        // Sende die Daten als POST-Request an den ESP32
        fetch('/api/config', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify(data)
        })
        .then(response => response.json())
        .then(res_data => {
            if (res_data.status === 'success') {
                statusMessage.textContent = 'Gespeichert! Werte sind aktiv.';
                saveButton.classList.remove('unsaved-changes');
                setTimeout(() => { statusMessage.textContent = ''; }, 3000);
            } else {
                // Zeige detaillierte Validierungsfehler an
                let errorMsg = 'Fehler: ' + (res_data.message || '');
                if(res_data.validation_errors) {
                    errorMsg = 'Validierungsfehler: ' + JSON.stringify(res_data.validation_errors);
                }
                statusMessage.textContent = errorMsg;
            }
        })
        .catch(error => {
            statusMessage.textContent = 'Kommunikationsfehler beim Speichern.';
            console.error('Error saving config:', error);
        });
    }

    // Event Listener für den Speicher-Button
    form.addEventListener('submit', saveSettings);

	const rebootButton = document.getElementById('reboot-button');
    if(rebootButton) {
        rebootButton.addEventListener('click', function() {
            if (confirm('Gerät wirklich neustarten? Alle nicht gespeicherten Änderungen gehen verloren.')) {
                
                // 1. Befehl feuern und ignorieren was zurückkommt (Fire and Forget)
                fetch('/api/reboot').catch(() => { /* Fehler ignorieren, Gerät geht ja offline */ });
                
                // 2. SOFORT die Anzeige aktualisieren, OHNE auf den ESP32 zu warten!
                const container = document.querySelector('.container');
                if (container) {
                    container.innerHTML = `
                        <div style="text-align: center; padding: 40px 10px;">
                            <h1 style="color: #bb86fc;">System wird neu gestartet...</h1>
                            <p style="margin-top: 20px; line-height: 1.6;">Bitte warten, das Gerät verbindet sich neu mit dem Netzwerk.<br>Du wirst in ca. 10 Sekunden automatisch weitergeleitet.</p>
                            <div style="font-size: 3em; margin-top: 30px; color: #03dac6;">&#8987;</div>
                        </div>
                    `;
                }

                // 3. Timer starten
                setTimeout(() => {
                    window.location.href = "/";
                }, 10000);
            }
        });
    }

    // Lade die Einstellungen, wenn die Seite geladen wird
    loadSettings();

    // --- Voltmeter-Panel (Paket J, über den seriellen Link) ---
    const vmMsg = (t) => { const e = document.getElementById('vm-status-message'); if (e) e.textContent = t; };

    function loadVoltmeterStatus() {
        fetch('/api/voltmeter/version')
            .then(r => r.json())
            .then(d => { if (d.version) document.getElementById('vm-version').textContent = d.version; })
            .catch(() => {});
        fetch('/api/voltmeter/status')
            .then(r => r.json())
            .then(d => {
                if (d.status === 'success') {
                    document.getElementById('vm-factor').textContent = d.scaling_factor.toFixed(3);
                    document.getElementById('vm-voffset').textContent = d.voltage_offset.toFixed(2);
                    document.getElementById('vm-adczero').textContent = d.adc_zero_offset.toFixed(1);
                }
            })
            .catch(() => {});
    }

    const vmRefresh = document.getElementById('vm-refresh');
    if (vmRefresh) vmRefresh.addEventListener('click', loadVoltmeterStatus);

    const vmSetFactor = document.getElementById('vm-set-factor');
    if (vmSetFactor) vmSetFactor.addEventListener('click', () => {
        const v = document.getElementById('vm-new-factor').value;
        if (v === '' || isNaN(v)) { vmMsg('Bitte gültigen Faktor eingeben.'); return; }
        vmMsg('Setze Faktor...');
        fetch('/api/voltmeter/factor?value=' + encodeURIComponent(v))
            .then(r => r.json())
            .then(d => { vmMsg(d.message || d.status); loadVoltmeterStatus(); })
            .catch(() => vmMsg('Kommunikationsfehler.'));
    });

    const vmAutozero = document.getElementById('vm-autozero');
    if (vmAutozero) vmAutozero.addEventListener('click', () => {
        if (!confirm('Auto-Zero-Kalibrierung starten? Dauert einige Sekunden.')) return;
        vmMsg('Auto-Zero läuft...');
        fetch('/api/voltmeter/autozero')
            .then(r => r.json())
            .then(d => { vmMsg(d.message || d.status); setTimeout(loadVoltmeterStatus, 8000); })
            .catch(() => vmMsg('Kommunikationsfehler.'));
    });

    loadVoltmeterStatus();
});