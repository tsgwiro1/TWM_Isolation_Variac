var logSocket;
var autoScroll = true; 

function initLogSocket() {
    var gateway = 'ws://' + window.location.hostname + '/ws';
    logSocket = new WebSocket(gateway);

    logSocket.onopen = function(event) {
        console.log("Log WebSocket connected!");
    };

    logSocket.onmessage = function(event) {
        var logWindow = document.getElementById('liveLogWindow');
        if (logWindow) {
            logWindow.value += event.data;
            
            // Nur scrollen, wenn Auto-Scroll aktiv ist
            if (autoScroll) {
                logWindow.scrollTop = logWindow.scrollHeight;
            }
        }
    };

    logSocket.onclose = function(event) {
        console.log("WebSocket disconnected. Retrying in 3 seconds...");
        var logWindow = document.getElementById('liveLogWindow');
        if (logWindow) {
            logWindow.value += "\n--- Connection lost. Reconnecting... ---\n";
        }
        setTimeout(initLogSocket, 3000);
    };
}

// Initialisieren, wenn die Seite geladen ist
window.addEventListener('load', function() {
    initLogSocket();
    
    // Event Listener für die neue Checkbox
    var scrollCheck = document.getElementById('autoScrollCheck');
    if (scrollCheck) {
        scrollCheck.addEventListener('change', function(e) {
            autoScroll = e.target.checked;
            
            // Wenn man Auto-Scroll wieder aktiviert, direkt ganz nach unten springen
            if (autoScroll) {
                var logWindow = document.getElementById('liveLogWindow');
                logWindow.scrollTop = logWindow.scrollHeight;
            }
        });
    }
});