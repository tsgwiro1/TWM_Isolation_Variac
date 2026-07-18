// TWM Isolation Variac – gemeinsame UI-Logik: Theme, Akzentfarbe, Header-Menüs (#23)
// Hinweis: Damit die Seite nicht kurz im falschen Theme aufblitzt, setzt jede
// HTML-Seite data-theme/data-accent bereits in einem kleinen Inline-Script im <head>.

const ACCENTS = ['teal', 'amber', 'blue', 'steel'];
const ACCENT_COLORS = { teal: '#48ab9e', amber: '#d9944a', blue: '#5f92d0', steel: '#96a0ac' };

function lsGet(key) { try { return localStorage.getItem(key); } catch (e) { return null; } }
function lsSet(key, val) { try { localStorage.setItem(key, val); } catch (e) {} }

function currentTheme() { return document.documentElement.dataset.theme === 'light' ? 'light' : 'dark'; }

function applyTheme(theme) {
    document.documentElement.dataset.theme = theme;
    lsSet('variac-theme', theme);
    const btn = document.getElementById('theme-btn');
    if (btn) btn.textContent = theme === 'dark' ? '☀' : '☾'; // ☀ / ☾
}

function applyAccent(accent) {
    if (ACCENTS.indexOf(accent) === -1) accent = 'teal';
    document.documentElement.dataset.accent = accent;
    lsSet('variac-accent', accent);
}

// Verdrahtet die Header-Bedienelemente (auf jeder Seite identisch aufgebaut).
function initHeader() {
    applyTheme(currentTheme()); // Icon des Theme-Buttons initialisieren

    const themeBtn = document.getElementById('theme-btn');
    if (themeBtn) themeBtn.addEventListener('click', () => {
        applyTheme(currentTheme() === 'dark' ? 'light' : 'dark');
    });

    // Popover-Menüs (Doku + Akzentfarbe): Button togglet, Klick daneben schließt.
    document.querySelectorAll('.menu-anchor > button').forEach(btn => {
        btn.addEventListener('click', (e) => {
            e.stopPropagation();
            const menu = btn.parentElement.querySelector('.menu');
            const wasOpen = menu.classList.contains('open');
            document.querySelectorAll('.menu.open').forEach(m => m.classList.remove('open'));
            if (!wasOpen) menu.classList.add('open');
        });
    });
    document.addEventListener('click', () => {
        document.querySelectorAll('.menu.open').forEach(m => m.classList.remove('open'));
    });

    // Akzentfarben-Menü dynamisch füllen
    const accMenu = document.getElementById('accent-menu');
    if (accMenu) {
        ACCENTS.forEach(a => {
            const b = document.createElement('button');
            b.className = 'menu-item';
            b.innerHTML = '<span class="swatch" style="background:' + ACCENT_COLORS[a] + '"></span>' +
                          a.charAt(0).toUpperCase() + a.slice(1);
            b.addEventListener('click', () => applyAccent(a));
            accMenu.appendChild(b);
        });
    }
}

// Kleine Helfer für die Seiten-Skripte
function $(id) { return document.getElementById(id); }

function apiPost(path) {
    return fetch(path, { method: 'POST' }).then(r => r.json());
}

// Neustart-Ablauf (Controller): Befehl feuern, Overlay zeigen, nach 10 s zurück zur Startseite.
function rebootWithOverlay() {
    fetch('/api/reboot', { method: 'POST' }).catch(() => { /* Gerät geht offline – erwartbar */ });
    const wrap = document.querySelector('.wrap');
    if (wrap) {
        wrap.innerHTML = '<div class="overlay-center">' +
            '<h1>System wird neu gestartet …</h1>' +
            '<p>Bitte warten, das Gerät verbindet sich neu mit dem Netzwerk.<br>' +
            'Du wirst in ca. 10 Sekunden automatisch weitergeleitet.</p>' +
            '<div class="big">&#8987;</div></div>';
    }
    setTimeout(() => { window.location.href = '/'; }, 10000);
}
