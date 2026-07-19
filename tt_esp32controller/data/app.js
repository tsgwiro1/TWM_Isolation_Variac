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

// ---- Benannte Browser-Tabs: eine Seite = ein Tab ----
// Links mit data-tab öffnen die Zielseite in einem benannten Tab. Existiert der
// Tab schon, wird nur dorthin gewechselt (und nur neu geladen, wenn er gerade
// eine andere Seite zeigt) — es entstehen keine Duplikat-Tabs.
var TAB_NAMES = {
    '': 'variac-dash', 'index.html': 'variac-dash',
    'settings.html': 'variac-settings',
    'log.html': 'variac-log',
    'doc_usage.html': 'variac-doc', 'doc_api.html': 'variac-doc', 'doc_settings.html': 'variac-doc'
};

var namedTabs = {}; // von diesem Tab geöffnete Fenster-Handles (Name -> window)

// Mobile Browser können per Script nicht zuverlässig zwischen Tabs wechseln
// (focus() auf fremde Fenster wird ignoriert; ob window.open(url, name) den
// benannten Tab nach vorne holt, variiert je Browser). Auf Mobilgeräten deshalb
// klassisch im selben Tab navigieren — benannte Tabs sind ein Desktop-Feature.
// Deckt iOS/iPadOS (alle Browser dort nutzen WebKit; iPadOS meldet sich als
// "MacIntel", ist aber am Multi-Touch erkennbar) und Android ab.
var IS_MOBILE = /Android|iP(hone|ad|od)/.test(navigator.userAgent)
    || (navigator.platform === 'MacIntel' && navigator.maxTouchPoints > 1);

function openInNamedTab(e) {
    var a = e.currentTarget;
    var name = a.dataset.tab;
    if (!name || !window.open) return; // Fallback: native target-Navigation
    e.preventDefault();
    if (IS_MOBILE) { location.href = a.href; return; }
    var norm = function (path) {
        var p = path.split('/').pop();
        return p === '' ? 'index.html' : p;
    };
    // Kennt dieser Tab das Ziel-Fenster schon und zeigt es die richtige Seite,
    // reicht ein Fokuswechsel — ganz ohne Neuladen.
    var w = namedTabs[name];
    try {
        if (w && !w.closed && norm(w.location.pathname) === norm(a.pathname)) {
            w.focus();
            return;
        }
    } catch (err) { /* Handle unbrauchbar -> unten regulär öffnen */ }
    // window.open MIT URL: existiert der benannte Tab, lädt der Browser die Seite
    // dort und wechselt hin; sonst entsteht genau ein neuer Tab. (Safari reused
    // benannte Tabs nur auf diesem Weg zuverlässig — nicht mit leerer URL.)
    w = window.open(a.href, name);
    if (!w) { location.href = a.href; return; } // Popup blockiert -> im selben Tab
    namedTabs[name] = w;
    if (w.focus) w.focus();
}

// Verdrahtet die Header-Bedienelemente (auf jeder Seite identisch aufgebaut).
function initHeader() {
    applyTheme(currentTheme()); // Icon des Theme-Buttons initialisieren

    // Eigenen Tab benennen — auch direkt geöffnete Tabs werden so wiederverwendet.
    var page = location.pathname.split('/').pop();
    if (TAB_NAMES[page] !== undefined) window.name = TAB_NAMES[page];

    document.querySelectorAll('a[data-tab]').forEach(function (a) {
        a.addEventListener('click', openInNamedTab);
    });

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
