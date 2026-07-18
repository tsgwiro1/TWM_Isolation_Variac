// TWM Isolation Variac – Live Log (#23): WebSocket /ws mit RAM-Historie beim Verbinden,
// farbige Log-Level, Auto-Scroll, Leeren, Download.

(function () {
    'use strict';

    var MAX_LINES = 1000; // DOM-Limit; die volle Historie liegt in der Log-Datei
    var autoScroll = true;
    var ws = null;

    function setWsChip(cls, text) {
        var el = $('chip-ws');
        el.className = 'chip ' + cls;
        el.querySelector('span:last-child').textContent = text;
    }

    // Zeilenformat der Firmware: "[millis][LEVEL] Nachricht"
    var LINE_RE = /^\[(\d+)\]\[(\w+)\]\s?(.*)$/;

    // millis -> Uptime "HH:MM:SS.mmm"
    function fmtUptime(ms) {
        var s = Math.floor(ms / 1000);
        var p = function (n, l) { return String(n).padStart(l || 2, '0'); };
        return p(Math.floor(s / 3600)) + ':' + p(Math.floor(s / 60) % 60) + ':' + p(s % 60) + '.' + p(ms % 1000, 3);
    }

    function appendLine(raw) {
        if (!raw) return;
        var win = $('log-win');
        var div = document.createElement('div');
        var m = LINE_RE.exec(raw);
        if (m) {
            var level = m[2].toUpperCase();
            div.className = 'log-line lv-' + level.toLowerCase();
            var ts = document.createElement('span');
            ts.className = 'ts';
            ts.textContent = '[' + fmtUptime(parseInt(m[1], 10)) + '] ';
            var tag = document.createElement('span');
            tag.className = 'tag';
            tag.textContent = '[' + level + ']';
            var msg = document.createElement('span');
            msg.className = 'msg';
            msg.textContent = ' ' + m[3];
            div.appendChild(ts); div.appendChild(tag); div.appendChild(msg);
        } else {
            div.className = 'log-line';
            var plain = document.createElement('span');
            plain.className = 'msg';
            plain.textContent = raw;
            div.appendChild(plain);
        }
        win.appendChild(div);

        // DOM begrenzen: älteste Zeilen entfernen
        while (win.childNodes.length > MAX_LINES) win.removeChild(win.firstChild);

        $('line-count').textContent = win.childNodes.length;
        if (autoScroll) win.scrollTop = win.scrollHeight;
    }

    function appendChunk(text) {
        // Eine WS-Nachricht kann mehrere Zeilen enthalten (z. B. die Historie beim Verbinden)
        text.split('\n').forEach(function (line) {
            if (line.trim() !== '') appendLine(line);
        });
    }

    function connect() {
        ws = new WebSocket('ws://' + location.host + '/ws');
        ws.onopen = function () { setWsChip('ok', 'WS verbunden'); };
        ws.onmessage = function (ev) { appendChunk(ev.data); };
        ws.onclose = function () {
            setWsChip('warn', 'Getrennt');
            appendLine('--- Verbindung verloren, neuer Versuch in 3 s ---');
            setTimeout(connect, 3000);
        };
    }

    document.addEventListener('DOMContentLoaded', function () {
        initHeader();

        $('autoscroll').addEventListener('click', function () {
            this.classList.toggle('on');
            autoScroll = this.classList.contains('on');
            if (autoScroll) {
                var win = $('log-win');
                win.scrollTop = win.scrollHeight;
            }
        });
        $('btn-clear').addEventListener('click', function () {
            $('log-win').innerHTML = '';
            $('line-count').textContent = '0';
        });

        connect();
    });
})();
