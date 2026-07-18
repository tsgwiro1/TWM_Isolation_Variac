// TWM Isolation Variac – Einstellungen (#23): Konfiguration, Backup/Restore,
// Voltmeter-Panel inkl. Firmware-Update in zwei Schritten.

(function () {
    'use strict';

    var dirty = false;

    // ---------- Statuszeilen ----------
    function statusMsg(text, ok) {
        var el = $('status-message');
        el.textContent = text;
        el.className = 'status-line' + (ok ? ' ok' : '');
    }
    function cfgMsg(text, ok) {
        var el = $('cfg-status');
        el.textContent = text;
        el.className = 'status-line' + (ok ? ' ok' : '');
    }
    function vmMsg(text) { $('vm-status-message').textContent = text; }

    // ---------- Dirty-Markierung ----------
    function markDirty() {
        dirty = true;
        var b = $('save-button');
        b.classList.remove('primary');
        b.classList.add('warn');
        b.textContent = '● Speichern & Anwenden';
        statusMsg('Ungespeicherte Änderungen vorhanden.');
    }
    function markClean() {
        dirty = false;
        var b = $('save-button');
        b.classList.remove('warn');
        b.classList.add('primary');
        b.textContent = 'Speichern & Anwenden';
    }

    // ---------- Konfiguration laden/speichern ----------
    function loadSettings() {
        return fetch('/api/config').then(function (r) { return r.json(); })
            .then(function (data) {
                $('debug_enabled').classList.toggle('on', !!(data.system && data.system.debug_enabled));
                var reg = data.regulation || {};
                $('reg_deadband_v').value = (reg.deadband_v != null) ? reg.deadband_v : 1.0;
                $('reg_damping').value = (reg.damping != null) ? reg.damping : 0.8;
                $('reg_settle_ms').value = (reg.settle_ms != null) ? reg.settle_ms : 150;
                $('reg_undershoot_v').value = (reg.undershoot_v != null) ? reg.undershoot_v : 5.0;
                $('min_pos').value = data.calibration.min_pos;
                $('max_pos').value = data.calibration.max_pos;
                $('min_voltage').value = data.calibration.min_voltage;
                $('max_voltage').value = data.calibration.max_voltage;
                $('p1').value = data.presets.p1;
                $('p2').value = data.presets.p2;
                $('p3').value = data.presets.p3;
                markClean();
            })
            .catch(function () { statusMsg('Fehler beim Laden der Konfiguration.'); });
    }

    function buildConfig() {
        return {
            system: { debug_enabled: $('debug_enabled').classList.contains('on') },
            regulation: {
                deadband_v: parseFloat($('reg_deadband_v').value),
                damping: parseFloat($('reg_damping').value),
                settle_ms: parseInt($('reg_settle_ms').value, 10),
                undershoot_v: parseFloat($('reg_undershoot_v').value)
            },
            calibration: {
                min_pos: parseInt($('min_pos').value, 10),
                max_pos: parseInt($('max_pos').value, 10),
                min_voltage: parseFloat($('min_voltage').value),
                max_voltage: parseFloat($('max_voltage').value)
            },
            presets: {
                p1: parseInt($('p1').value, 10),
                p2: parseInt($('p2').value, 10),
                p3: parseInt($('p3').value, 10)
            }
        };
    }

    function postConfig(data, onDone) {
        return fetch('/api/config', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify(data)
        })
        .then(function (r) { return r.json(); })
        .then(function (res) {
            if (res.status === 'success') {
                onDone(true, 'Gespeichert – Werte sind aktiv.');
            } else if (res.validation_errors) {
                onDone(false, 'Validierungsfehler: ' + JSON.stringify(res.validation_errors));
            } else {
                onDone(false, 'Fehler: ' + (res.message || ''));
            }
        })
        .catch(function () { onDone(false, 'Kommunikationsfehler beim Speichern.'); });
    }

    function saveSettings() {
        statusMsg('Speichere …');
        postConfig(buildConfig(), function (ok, msg) {
            statusMsg(msg, ok);
            if (ok) {
                markClean();
                setTimeout(function () { if (!dirty) statusMsg(''); }, 3000);
            }
        });
    }

    // ---------- Konfiguration wiederherstellen (Upload) ----------
    function restoreConfig(file) {
        var reader = new FileReader();
        reader.onload = function () {
            var data;
            try { data = JSON.parse(reader.result); }
            catch (e) { cfgMsg('Keine gültige JSON-Datei.'); return; }
            cfgMsg('Übertrage Konfiguration …');
            postConfig(data, function (ok, msg) {
                cfgMsg(ok ? ('„' + file.name + '" übernommen.') : msg, ok);
                if (ok) loadSettings(); // Formular mit den neuen Werten füllen
            });
        };
        reader.onerror = function () { cfgMsg('Datei konnte nicht gelesen werden.'); };
        reader.readAsText(file);
    }

    // ---------- Voltmeter ----------
    function loadVoltmeterStatus() {
        fetch('/api/voltmeter/version').then(function (r) { return r.json(); })
            .then(function (d) {
                if (d.version) {
                    $('vm-version').textContent = d.version;
                    $('vm-version2').textContent = d.version;
                }
            })
            .catch(function () {});
        fetch('/api/voltmeter/status').then(function (r) { return r.json(); })
            .then(function (d) {
                if (d.status === 'success') {
                    $('vm-factor').textContent = d.scaling_factor.toFixed(3);
                    $('vm-voffset').textContent = d.voltage_offset.toFixed(2);
                    $('vm-adczero').textContent = d.adc_zero_offset.toFixed(1);
                }
            })
            .catch(function () {});
    }

    function vmSimple(path, msg, reloadDelayMs) {
        vmMsg(msg);
        apiPost(path)
            .then(function (d) {
                vmMsg(d.message || d.status);
                if (reloadDelayMs) setTimeout(loadVoltmeterStatus, reloadDelayMs);
                else loadVoltmeterStatus();
            })
            .catch(function () { vmMsg('Kommunikationsfehler.'); });
    }

    // ---------- Firmware-Update (2 Schritte) ----------
    var fwPollTimer = null;

    function setStep2Enabled(enabled) {
        $('vm-fw-start').disabled = !enabled;
        $('step2-title').style.color = enabled ? 'var(--accent)' : 'var(--faint)';
        $('vm-fw-hint').style.display = enabled ? 'none' : 'block';
    }

    // Prüft, ob eine Firmware-Datei auf dem Controller liegt, und zeigt deren Version (#33).
    function loadFwFileVersion() {
        fetch('/api/voltmeter/update/fileversion').then(function (r) { return r.json(); })
            .then(function (d) {
                if (d.status === 'success') {
                    $('vm-fw-fileversion').textContent = d.version;
                    setStep2Enabled(true);
                } else {
                    $('vm-fw-fileversion').textContent = 'keine / ohne Tag';
                    // Datei ohne Versions-Tag ist trotzdem flashbar -> Existenz prüfen
                    fetch('/api/files').then(function (r) { return r.json(); })
                        .then(function (files) {
                            var found = (files || []).some(function (f) {
                                return f.name === 'voltmeter_fw.bin' || f.name === '/voltmeter_fw.bin';
                            });
                            setStep2Enabled(found);
                        })
                        .catch(function () { setStep2Enabled(false); });
                }
            })
            .catch(function () { $('vm-fw-fileversion').textContent = '–'; setStep2Enabled(false); });
    }

    function pollUpdateStatus() {
        fetch('/api/voltmeter/update/status').then(function (r) { return r.json(); })
            .then(function (d) {
                $('vm-fw-progress').textContent = (d.progress || 0) + ' %';
                vmMsg(d.message || '');
                if (d.state === 'running') {
                    fwPollTimer = setTimeout(pollUpdateStatus, 700);
                } else {
                    fwPollTimer = null;
                    if (d.state === 'success') loadVoltmeterStatus();
                }
            })
            .catch(function () { fwPollTimer = setTimeout(pollUpdateStatus, 1500); });
    }

    function uploadFw() {
        var input = $('vm-fw-file');
        if (!input.files || input.files.length === 0) { vmMsg('Bitte zuerst eine .bin-Datei wählen.'); return; }
        var fd = new FormData();
        fd.append('firmware', input.files[0]);
        vmMsg('Lade hoch …');
        // XMLHttpRequest statt fetch, damit wir den Upload-Fortschritt anzeigen können.
        var xhr = new XMLHttpRequest();
        xhr.open('POST', '/api/voltmeter/update/upload');
        xhr.upload.addEventListener('progress', function (e) {
            if (e.lengthComputable) {
                $('vm-fw-progress').textContent = 'Upload ' + Math.round((e.loaded / e.total) * 100) + ' %';
            }
        });
        xhr.addEventListener('load', function () {
            if (xhr.status === 200) {
                var m = 'Upload abgeschlossen.';
                try { m = JSON.parse(xhr.responseText).message || m; } catch (e) {}
                $('vm-fw-progress').textContent = 'Upload 100 %';
                vmMsg(m);
                loadFwFileVersion();
            } else {
                vmMsg('Upload fehlgeschlagen (HTTP ' + xhr.status + ').');
            }
        });
        xhr.addEventListener('error', function () { vmMsg('Upload fehlgeschlagen.'); });
        xhr.send(fd);
    }

    function startUpdate() {
        // Versionsvergleich vor dem Flashen (#33)
        var running = ($('vm-version').textContent || '').trim();
        fetch('/api/voltmeter/update/fileversion').then(function (r) { return r.json(); })
            .then(function (d) { return d.status === 'success' ? d.version : null; })
            .catch(function () { return null; })
            .then(function (fileV) {
                var ok;
                if (!fileV) {
                    ok = confirm('Keine Version aus der Datei lesbar. Trotzdem flashen? Nur am offenen Gerät (ST-Link als Rettung).');
                } else if (running && running !== '–' && fileV === running) {
                    ok = confirm('Auf dem Voltmeter läuft bereits ' + running + '. Update nicht nötig – trotzdem flashen?');
                } else {
                    ok = confirm('Flashen' + (running && running !== '–' ? ' von ' + running : '') + ' auf ' + fileV + '? Nur am offenen Gerät (ST-Link als Rettung).');
                }
                if (!ok) return;
                vmMsg('Starte Update …');
                apiPost('/api/voltmeter/update/start')
                    .then(function (d) {
                        vmMsg(d.message || d.status);
                        if (!fwPollTimer) pollUpdateStatus();
                    })
                    .catch(function () { vmMsg('Start fehlgeschlagen.'); });
            });
    }

    // ---------- Init ----------
    document.addEventListener('DOMContentLoaded', function () {
        initHeader();

        // Dirty-Tracking für alle Konfig-Felder
        ['reg_deadband_v', 'reg_damping', 'reg_settle_ms', 'reg_undershoot_v',
         'min_pos', 'max_pos', 'min_voltage', 'max_voltage', 'p1', 'p2', 'p3'
        ].forEach(function (id) { $(id).addEventListener('input', markDirty); });

        $('debug_enabled').addEventListener('click', function () {
            this.classList.toggle('on');
            markDirty();
        });

        $('save-button').addEventListener('click', saveSettings);
        $('reboot-button').addEventListener('click', function () {
            if (confirm('Gerät wirklich neustarten? Alle nicht gespeicherten Änderungen gehen verloren.')) {
                rebootWithOverlay();
            }
        });

        $('cfg-file').addEventListener('change', function (e) {
            var file = e.target.files && e.target.files[0];
            if (file) restoreConfig(file);
            e.target.value = '';
        });

        // Voltmeter-Panel
        $('vm-refresh').addEventListener('click', function () {
            vmMsg('Aktualisiere …');
            loadVoltmeterStatus();
            setTimeout(function () { vmMsg(''); }, 1500);
        });
        $('vm-set-factor').addEventListener('click', function () {
            var v = $('vm-new-factor').value;
            if (v === '' || isNaN(v)) { vmMsg('Bitte gültigen Faktor eingeben.'); return; }
            vmSimple('/api/voltmeter/factor?value=' + encodeURIComponent(v), 'Setze Faktor …');
        });
        $('vm-set-offset').addEventListener('click', function () {
            var v = $('vm-new-offset').value;
            if (v === '' || isNaN(v)) { vmMsg('Bitte gültigen Offset eingeben.'); return; }
            vmSimple('/api/voltmeter/offset?value=' + encodeURIComponent(v), 'Setze Offset …');
        });
        $('vm-autozero').addEventListener('click', function () {
            if (!confirm('Auto-Zero-Kalibrierung starten? Dauert einige Sekunden.')) return;
            vmSimple('/api/voltmeter/autozero', 'Auto-Zero läuft …', 8000);
        });
        $('vm-reboot').addEventListener('click', function () {
            if (!confirm('Voltmeter neu starten?')) return;
            vmSimple('/api/voltmeter/reboot', 'Voltmeter startet neu …', 8000);
        });
        $('vm-reset-defaults').addEventListener('click', function () {
            if (!confirm('Voltmeter-Kalibrierung auf Werkseinstellungen zurücksetzen?')) return;
            vmSimple('/api/voltmeter/reset-defaults', 'Setze zurück …');
        });

        // 3-Punkt-Kalibrierung
        document.querySelectorAll('.cal3-measure').forEach(function (btn) {
            btn.addEventListener('click', function () {
                var idx = btn.getAttribute('data-index');
                var n = parseInt(idx, 10) + 1;
                var v = $('cal3-v' + idx).value;
                if (v === '' || isNaN(v)) { vmMsg('Bitte Referenzspannung für Punkt ' + n + ' eingeben.'); return; }
                vmMsg('Messe Punkt ' + n + ' … (~2 s)');
                apiPost('/api/voltmeter/cal3/measure?index=' + idx + '&voltage=' + encodeURIComponent(v))
                    .then(function (d) {
                        vmMsg(d.status === 'success' ? ('Punkt ' + n + ' gemessen.') : ('Fehler: ' + (d.message || '')));
                    })
                    .catch(function () { vmMsg('Kommunikationsfehler.'); });
            });
        });
        $('cal3-finish').addEventListener('click', function () {
            vmMsg('Berechne Kalibrierung …');
            apiPost('/api/voltmeter/cal3/finish')
                .then(function (d) {
                    if (d.status === 'success') {
                        vmMsg('Kalibriert: Faktor ' + d.scaling_factor.toFixed(3) + ', Offset ' + d.voltage_offset.toFixed(2) + ' V');
                        loadVoltmeterStatus();
                    } else {
                        vmMsg('Fehler: ' + (d.message || ''));
                    }
                })
                .catch(function () { vmMsg('Kommunikationsfehler.'); });
        });

        // Firmware-Update
        $('vm-fw-file').addEventListener('change', function (e) {
            var file = e.target.files && e.target.files[0];
            $('vm-fw-filename').textContent = file ? file.name : 'Keine Datei gewählt';
            $('vm-fw-filename').style.color = file ? 'var(--text)' : 'var(--faint)';
            $('vm-fw-upload').disabled = !file;
        });
        $('vm-fw-upload').addEventListener('click', uploadFw);
        $('vm-fw-start').addEventListener('click', startUpdate);

        // Daten laden
        loadSettings();
        loadVoltmeterStatus();
        loadFwFileVersion();
        fetch('/api/status').then(function (r) { return r.json(); })
            .then(function (st) { if (st.fw_version) $('fw-foot').textContent = st.fw_version; })
            .catch(function () {});
    });
})();
