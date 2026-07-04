document.addEventListener('DOMContentLoaded', function() {
    // Referenzen auf die HTML-Elemente
    const istVoltageElement = document.getElementById('ist-voltage');
    const sollVoltageElement = document.getElementById('soll-voltage');
	const gaugeNeedle = document.getElementById('gauge-needle');
    const newSollInput = document.getElementById('new-soll');
    const setButton = document.getElementById('set-button');
    const statusMessage = document.getElementById('status-message');

    // NEU: Referenzen auf die neuen Tasten
    const btnOnoff = document.getElementById('btn-onoff');
    const btnLimit = document.getElementById('btn-limit');
    const btnReg = document.getElementById('btn-reg');
    const btnP1 = document.getElementById('btn-p1');
    const btnP2 = document.getElementById('btn-p2');
    const btnP3 = document.getElementById('btn-p3');

    // NEU: Helfer-Funktion, um einen Wert von einem Bereich in einen anderen umzurechnen
    function mapRange(value, in_min, in_max, out_min, out_max) {
        return (value - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
    }

    // Funktion, um die Daten vom ESP32 zu holen und die Anzeige zu aktualisieren
    // (#22: /data ist in /api/status aufgegangen)
    function updateData() {
        fetch('/api/status')
            .then(response => response.json())
            .then(data => {
                // Werte aktualisieren
                istVoltageElement.textContent = data.voltage_actual.toFixed(1);
                sollVoltageElement.textContent = data.voltage_setpoint.toFixed(1);

                // Zeigerinstrument aktualisieren
                const voltage = data.voltage_actual;
                // Definiere den Bereich: 0V bis 260V entspricht -90 Grad bis +90 Grad
                const angle = mapRange(voltage, 0, 260, -90, 90);
                // Wende die Rotation auf die Nadel an
                if (gaugeNeedle) { // Sicherstellen, dass das Element existiert
                    gaugeNeedle.style.transform = `translate(-50%) rotate(${angle}deg)`;
                }

                // Tasten-Zustände aktualisieren
                const st = data.states || {};
                btnOnoff.classList.toggle('active', st.output_on);
                btnLimit.classList.toggle('active', st.limit_on);
                btnReg.classList.toggle('active', st.regulation_on);
                btnP1.classList.toggle('active', st.p1_on);
                btnP2.classList.toggle('active', st.p2_on);
                btnP3.classList.toggle('active', st.p3_on);
            })
            .catch(error => console.error('Fehler beim Abrufen der Daten:', error));
    }

    // Funktion, um einen neuen Sollwert zu senden
    function setSollValue() {
        const voltage = newSollInput.value;
        if (voltage === '' || isNaN(voltage)) {
            statusMessage.textContent = 'Bitte gültigen Wert eingeben.';
            return;
        }
        fetch(`/api/setpoint?voltage=${voltage}`, { method: 'POST' })
            .then(response => {
                if (response.ok) {
                    statusMessage.textContent = `Sollwert auf ${voltage} V gesetzt.`;
                    newSollInput.value = '';
                    setTimeout(updateData, 500); 
                } else {
                    statusMessage.textContent = 'Fehler beim Setzen des Wertes.';
                }
            })
            .catch(error => console.error('Fehler beim Senden:', error));
    }

    // NEU: Generische Funktion, um einen Tasten-Befehl zu senden
    function sendCommand(action) {
        fetch(`/api/command?action=${action}`, { method: 'POST' })
            .then(response => {
                if (!response.ok) {
                    console.error('Fehler beim Senden des Befehls:', action);
                }
                // Update-Anzeige leicht verzögert aufrufen, damit der ESP32 Zeit hat zu reagieren
                setTimeout(updateData, 250);
            })
            .catch(error => console.error('Kommunikationsfehler:', error));
    }

    // Event Listener für die Tasten
    setButton.addEventListener('click', setSollValue);
    newSollInput.addEventListener('keypress', event => { if (event.key === 'Enter') setSollValue(); });

    // NEU: Event Listener für die neuen Tasten
    btnOnoff.addEventListener('click', () => sendCommand('toggle_output'));
    btnLimit.addEventListener('click', () => sendCommand('toggle_limit'));
    btnReg.addEventListener('click', () => sendCommand('toggle_regulation'));
    btnP1.addEventListener('click', () => sendCommand('recall_p1'));
    btnP2.addEventListener('click', () => sendCommand('recall_p2'));
    btnP3.addEventListener('click', () => sendCommand('recall_p3'));

    // Daten-Update-Schleife und erster Aufruf
    setInterval(updateData, 2000);
    updateData();
});