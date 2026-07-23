// TWM Isolation Variac – Dashboard (#23): Live-Daten über WebSocket /ws_status (#13),
// Fallback auf HTTP-Polling, Gauge + Trend als SVG.

(function () {
    'use strict';

    // ---- Konfiguration/Skalen (vmax/maxPos werden aus /api/config geladen) ----
    // GitHub-#20: tWarn/tMax sind KEINE eigenen Schwellen, sondern spiegeln die
    // Firmware wider. Führende Quelle ist src/system.cpp (MAXFANTEMP): ab dieser
    // Temperatur läuft der Lüfter auf 100 % und die Firmware loggt einen Alarm —
    // das Dashboard muss zum selben Zeitpunkt auf Warnfarbe wechseln.
    // Bei Änderung in system.cpp diese Werte mitziehen.
    var cfg = { vmax: 260, maxPos: 2500, tWarn: 60, tMax: 70 };

    // ---- Trend-Puffer: [ms-Zeitstempel, Volt] ----
    var TREND_MAX_S = 600;
    var history = [];
    var lastSetpoint = 0;

    var ws = null;
    var pollTimer = null;

    // ---------- Gauge (Bogen bei 225° startend, 270° Umfang) ----------
    function polar(deg, r) {
        var rad = deg * Math.PI / 180;
        return [120 + r * Math.sin(rad), 120 - r * Math.cos(rad)];
    }

    function initGauge() {
        var r = 92;
        var s = polar(225, r), e = polar(135, r);
        var d = 'M ' + s[0].toFixed(2) + ' ' + s[1].toFixed(2) +
                ' A ' + r + ' ' + r + ' 0 1 1 ' + e[0].toFixed(2) + ' ' + e[1].toFixed(2);
        $('g-track').setAttribute('d', d);
        $('g-arc').setAttribute('d', d);

        var g = $('g-ticks');
        g.innerHTML = '';
        [0, 0.25, 0.5, 0.75, 1].forEach(function (t) {
            var deg = 225 + t * 270;
            var a = polar(deg, r), b = polar(deg, r - 11), l = polar(deg, r - 26);
            var line = document.createElementNS('http://www.w3.org/2000/svg', 'line');
            line.setAttribute('x1', a[0].toFixed(1)); line.setAttribute('y1', a[1].toFixed(1));
            line.setAttribute('x2', b[0].toFixed(1)); line.setAttribute('y2', b[1].toFixed(1));
            line.setAttribute('stroke', 'var(--faint)'); line.setAttribute('stroke-width', '2');
            g.appendChild(line);
            var txt = document.createElementNS('http://www.w3.org/2000/svg', 'text');
            txt.setAttribute('x', l[0].toFixed(1)); txt.setAttribute('y', (l[1] + 4).toFixed(1));
            txt.setAttribute('fill', 'var(--dim)'); txt.setAttribute('font-size', '11');
            txt.setAttribute('font-family', "'IBM Plex Mono', monospace"); txt.setAttribute('text-anchor', 'middle');
            txt.textContent = Math.round(t * cfg.vmax);
            g.appendChild(txt);
        });
        $('t-vmax').textContent = cfg.vmax;
        $('t-vmid').textContent = Math.round(cfg.vmax / 2);
    }

    function clamp(v, a, b) { return Math.max(a, Math.min(b, v)); }

    // ---------- Anzeige aktualisieren ----------
    function setChip(el, cls, text) {
        el.className = 'chip ' + cls;
        el.querySelector('span:last-child').textContent = text;
    }

    function render(st) {
        var v = Number(st.voltage_actual) || 0;
        var sp = Number(st.voltage_setpoint) || 0;
        lastSetpoint = sp;

        $('ist').textContent = v.toFixed(1);
        $('soll').textContent = sp.toFixed(1);

        var t = clamp(v / cfg.vmax, 0, 1);
        $('g-arc').setAttribute('stroke-dasharray', (t * 100).toFixed(2) + ' 100');
        $('g-needle').setAttribute('transform', 'rotate(' + (225 + t * 270).toFixed(2) + ' 120 120)');

        // Messwert-Frische
        if (st.voltage_fresh) {
            $('fresh-dot').style.background = 'var(--ok)';
            $('fresh-dot').style.animation = 'vpulse 1.6s ease-in-out infinite';
            $('fresh-t').textContent = 'Messwert aktuell · <250 ms';
            $('fresh-chip').style.color = 'var(--dim)';
        } else {
            $('fresh-dot').style.background = 'var(--warn)';
            $('fresh-dot').style.animation = 'none';
            $('fresh-t').textContent = 'Messwert veraltet!';
            $('fresh-chip').style.color = 'var(--warn)';
        }

        var states = st.states || {};
        setChip($('chip-output'), states.output_on ? 'ok' : 'off',
                states.output_on ? 'Ausgang Ein' : 'Ausgang Aus');

        // Modus-Anzeige (Auto-Regelung / Handbetrieb)
        $('mode-t').textContent = states.regulation_on ? 'Auto-Regelung' : 'Handbetrieb';
        var mc = states.regulation_on ? 'var(--ok)' : 'var(--accent)';
        $('mode-chip').style.color = mc;
        $('mode-dot').style.background = mc;

        $('btn-output').classList.toggle('active', !!states.output_on);
        $('btn-limit').classList.toggle('active', !!states.limit_on);
        $('btn-reg').classList.toggle('active', !!states.regulation_on);
        $('btn-p1').classList.toggle('active', !!states.p1_on);
        $('btn-p2').classList.toggle('active', !!states.p2_on);
        $('btn-p3').classList.toggle('active', !!states.p3_on);

        // Meter
        var temp = Number(st.temperature) || 0;
        var tempWarn = temp >= cfg.tWarn;
        $('temp-v').textContent = temp.toFixed(1) + ' °C';
        $('temp-v').style.color = tempWarn ? 'var(--warn)' : 'var(--ok)';
        $('temp-bar').style.background = tempWarn ? 'var(--warn)' : 'var(--ok)';
        $('temp-bar').style.width = (clamp(temp / cfg.tMax, 0, 1) * 100).toFixed(1) + '%';

        var pos = Number(st.stepper_position) || 0;
        $('pos-v').textContent = pos;
        $('pos-bar').style.width = (clamp(pos / cfg.maxPos, 0, 1) * 100).toFixed(1) + '%';

        if (st.fw_version) $('fw-foot').textContent = st.fw_version;

        // Trend fortschreiben
        var now = Date.now();
        history.push([now, v]);
        while (history.length && now - history[0][0] > TREND_MAX_S * 1000) history.shift();
        renderTrend();
    }

    // ---------- Trend ----------
    function renderTrend() {
        var W = 560, pad = 6, top = 12, bot = 146;
        var rangeS = parseInt($('trend-range').value, 10);
        var now = Date.now();
        var pts = [];
        for (var i = 0; i < history.length; i++) {
            var age = (now - history[i][0]) / 1000;
            if (age > rangeS) continue;
            var x = (W - pad) - (age / rangeS) * (W - 2 * pad);
            var y = bot - clamp(history[i][1] / cfg.vmax, 0, 1) * (bot - top);
            pts.push([x, y]);
        }
        var line = pts.map(function (p) { return p[0].toFixed(1) + ',' + p[1].toFixed(1); }).join(' ');
        $('t-line').setAttribute('points', line);
        if (pts.length) {
            $('t-area').setAttribute('points',
                pts[0][0].toFixed(1) + ',' + bot + ' ' + line + ' ' + pts[pts.length - 1][0].toFixed(1) + ',' + bot);
        } else {
            $('t-area').setAttribute('points', '');
        }
        var spY = bot - clamp(lastSetpoint / cfg.vmax, 0, 1) * (bot - top);
        $('t-sp').setAttribute('y1', spY.toFixed(1));
        $('t-sp').setAttribute('y2', spY.toFixed(1));
    }

    // ---------- Verbindung: WebSocket mit Polling-Fallback ----------
    function startPolling() {
        if (pollTimer) return;
        pollTimer = setInterval(function () {
            fetch('/api/status').then(function (r) { return r.json(); })
                .then(function (st) {
                    setChip($('chip-online'), 'warn', 'Polling');
                    render(st);
                })
                .catch(function () { setChip($('chip-online'), 'warn', 'Getrennt'); });
        }, 2000);
    }
    function stopPolling() {
        if (pollTimer) { clearInterval(pollTimer); pollTimer = null; }
    }

    function connectWs() {
        ws = new WebSocket('ws://' + location.host + '/ws_status');
        ws.onopen = function () {
            stopPolling();
            setChip($('chip-online'), 'ok', 'Online');
        };
        ws.onmessage = function (ev) {
            try { render(JSON.parse(ev.data)); } catch (e) { /* fehlerhafte Nachricht ignorieren */ }
        };
        ws.onclose = function () {
            setChip($('chip-online'), 'warn', 'Getrennt');
            startPolling();                    // Anzeige lebt per HTTP weiter
            setTimeout(connectWs, 3000);       // und wir versuchen den WS erneut
        };
    }

    // ---------- Bedienung ----------
    function dashMsg(text, ok) {
        var el = $('dash-status');
        el.textContent = text;
        el.className = 'status-line' + (ok ? ' ok' : '');
        clearTimeout(dashMsg.timer);
        if (text) dashMsg.timer = setTimeout(function () { el.textContent = ''; }, 4000);
    }

    function command(action) {
        apiPost('/api/command?action=' + action)
            .then(function (d) { if (d.status !== 'success') dashMsg('Fehler: ' + (d.message || '')); })
            .catch(function () { dashMsg('Kommunikationsfehler.'); });
    }

    function setSoll() {
        var v = parseFloat($('soll-input').value);
        if (isNaN(v)) { dashMsg('Bitte gültigen Wert eingeben.'); return; }
        apiPost('/api/setpoint?voltage=' + encodeURIComponent(v))
            .then(function (d) {
                if (d.status === 'success') { dashMsg('Sollwert gesetzt.', true); $('soll-input').value = ''; }
                else dashMsg('Fehler: ' + (d.message || ''));
            })
            .catch(function () { dashMsg('Kommunikationsfehler.'); });
    }

    // ---------- Init ----------
    document.addEventListener('DOMContentLoaded', function () {
        initHeader();
        initGauge();

        $('btn-output').addEventListener('click', function () { command('toggle_output'); });
        $('btn-limit').addEventListener('click', function () { command('toggle_limit'); });
        $('btn-reg').addEventListener('click', function () { command('toggle_regulation'); });
        $('btn-p1').addEventListener('click', function () { command('recall_p1'); });
        $('btn-p2').addEventListener('click', function () { command('recall_p2'); });
        $('btn-p3').addEventListener('click', function () { command('recall_p3'); });
        $('btn-setsoll').addEventListener('click', setSoll);
        $('soll-input').addEventListener('keydown', function (e) { if (e.key === 'Enter') setSoll(); });
        $('trend-range').addEventListener('change', renderTrend);

        // Skalen/Presets aus der Konfiguration; Gauge danach neu beschriften
        fetch('/api/config').then(function (r) { return r.json(); })
            .then(function (c) {
                if (c.calibration) {
                    cfg.vmax = Math.max(50, Math.min(Number(c.calibration.max_voltage) || 260, 260));
                    cfg.maxPos = Number(c.calibration.max_pos) || cfg.maxPos;
                }
                if (c.presets) {
                    ['p1', 'p2', 'p3'].forEach(function (k) {
                        $(k + '-val').textContent = (c.presets[k] != null) ? Number(c.presets[k]).toFixed(1) : '–';
                    });
                }
                $('pos-max').textContent = cfg.maxPos;
                initGauge();
            })
            .catch(function () { /* Defaults behalten */ });

        connectWs();
    });
})();
