<#
.SYNOPSIS
    Spannungs-Sequenz fuer den TWM Isolation Variac Controller (PowerShell-Variante).

.DESCRIPTION
    Faehrt eine feste Reihe von Soll-Spannungen an (20, 50, 75, 100, 150, 200, 230 V).
    Die Weiterschaltung erfolgt wahlweise automatisch (Zeit in Sekunden) oder manuell
    per Enter-Taste.

    Ablauf:
      1. Spannung auf 0 V setzen
      2. Strombegrenzung ein- oder ausschalten (Abfrage, Standard: ein)
      3. Spannungsregelung einschalten
      4. Ausgang einschalten
      5. Spannungsschritte nacheinander anfahren (je Schritt wird gewartet,
         bis die Ist-Spannung den Sollwert erreicht hat)
      6. Nach 230 V: Abfrage, ob die Strombegrenzung ausgeschaltet werden soll
      7. Abschlussabfrage: Ausgang aus + Spannung zurueck auf 0 V

    Verwendet nur Bordmittel (Invoke-RestMethod) - keine zusaetzliche Installation noetig.

.PARAMETER Address
    IP-Adresse oder Hostname des Controllers (Standard: 192.168.0.116).

.PARAMETER TimeoutSec
    HTTP-Timeout pro Anfrage in Sekunden (Standard: 5).

.EXAMPLE
    .\variac_sequence.ps1 -Address 192.168.0.116
#>

[CmdletBinding()]
param(
    [string]$Address = "192.168.0.116",
    [int]$TimeoutSec = 5
)

$ErrorActionPreference = "Stop"

# Anzufahrende Soll-Spannungen in Volt (in Reihenfolge).
$VoltageSteps = @(20, 50, 75, 100, 150, 200, 230)

# "Spannung erreicht"-Kriterium (GitHub-#6): Die Regelung braucht je nach
# Sprunghoehe mehrere Sekunden - deshalb pollen statt fixer Pause.
$ReachToleranceV = 2.0   # |Ist - Soll| <= Toleranz gilt als erreicht
$ReachTimeoutS = 30.0    # danach mit Warnung weitermachen
$ReachPollMs = 500       # Abfrageintervall

# Basis-URL aufbauen (http:// nicht doppelt voranstellen).
if ($Address -match '^(http|https)://') {
    $script:BaseUrl = $Address.TrimEnd('/')
} else {
    $script:BaseUrl = "http://" + $Address.TrimEnd('/')
}

function Get-VStatus {
    try {
        return Invoke-RestMethod -Uri "$script:BaseUrl/api/status" -TimeoutSec $TimeoutSec
    } catch {
        throw "Anfrage fehlgeschlagen ($script:BaseUrl/api/status): $($_.Exception.Message)"
    }
}

function Set-VVoltage {
    param([double]$Voltage)
    # InvariantCulture erzwingen, damit der Dezimalpunkt verwendet wird.
    $v = [string]::Format([System.Globalization.CultureInfo]::InvariantCulture, "{0}", $Voltage)
    try {
        # Ab Controller-FW V4.0.0: zustandsaendernde Aufrufe sind POST
        Invoke-RestMethod -Method Post -Uri "$script:BaseUrl/api/setpoint?voltage=$v" -TimeoutSec $TimeoutSec | Out-Null
    } catch {
        throw "Setpoint setzen fehlgeschlagen: $($_.Exception.Message)"
    }
}

function Invoke-VCommand {
    param([string]$Action)
    try {
        Invoke-RestMethod -Method Post -Uri "$script:BaseUrl/api/command?action=$Action" -TimeoutSec $TimeoutSec | Out-Null
    } catch {
        throw "Befehl '$Action' fehlgeschlagen: $($_.Exception.Message)"
    }
}

function Get-VState {
    param([string]$Key)
    $states = (Get-VStatus).states
    if ($null -eq $states -or -not ($states.PSObject.Properties.Name -contains $Key)) {
        throw "Statusfeld '$Key' nicht vorhanden."
    }
    return [bool]$states.$Key
}

function Set-VState {
    <#
        Stellt sicher, dass states[$Key] == $Desired ist.
        Die /command-Aktionen sind Umschalter (Toggle), daher wird zuerst der
        Ist-Zustand gelesen und nur bei Bedarf umgeschaltet.
    #>
    param(
        [string]$Key,
        [string]$Action,
        [bool]$Desired,
        [string]$Label
    )
    $current = Get-VState -Key $Key
    $word = if ($Desired) { "EIN" } else { "AUS" }
    if ($current -eq $Desired) {
        Write-Host "  $Label ist bereits $word."
        return
    }
    Invoke-VCommand -Action $Action
    Start-Sleep -Milliseconds 300  # kurze Pause, damit das Geraet den Zustand uebernimmt
    $new = Get-VState -Key $Key
    if ($new -ne $Desired) {
        $nowWord = if ($new) { "EIN" } else { "AUS" }
        throw "$Label konnte nicht auf $word gesetzt werden (ist weiterhin $nowWord)."
    }
    Write-Host "  $Label -> $word."
}

function Ask-YesNo {
    param([string]$Question, [bool]$Default = $false)
    $suffix = if ($Default) { " [J/n] " } else { " [j/N] " }
    while ($true) {
        $ans = (Read-Host ($Question + $suffix)).Trim().ToLower()
        if ($ans -eq "") { return $Default }
        if ($ans -in @("j", "ja", "y", "yes")) { return $true }
        if ($ans -in @("n", "nein", "no")) { return $false }
        Write-Host "Bitte 'j' oder 'n' eingeben."
    }
}

function Ask-Mode {
    while ($true) {
        Write-Host "Anfahrt der Spannungen automatisch oder per Enter?"
        Write-Host "  [a] automatisch (Zeitintervall)"
        Write-Host "  [e] manuell per Enter-Taste"
        $ans = (Read-Host "Auswahl [a/e]").Trim().ToLower()
        if ($ans -in @("a", "auto", "automatisch")) { return "auto" }
        if ($ans -in @("e", "enter", "m", "manuell")) { return "manual" }
        Write-Host "Bitte 'a' oder 'e' eingeben."
    }
}

function Ask-Interval {
    while ($true) {
        $ans = (Read-Host "Zeit bis zur naechsten Spannung in Sekunden").Trim().Replace(",", ".")
        $value = 0.0
        if (-not [double]::TryParse($ans, [System.Globalization.NumberStyles]::Float,
                [System.Globalization.CultureInfo]::InvariantCulture, [ref]$value)) {
            Write-Host "Bitte eine Zahl eingeben (z.B. 10 oder 5.5)."
            continue
        }
        if ($value -lt 0) {
            Write-Host "Bitte einen Wert >= 0 eingeben."
            continue
        }
        return $value
    }
}

function Show-VStatus {
    param([string]$Prefix = "Aktueller Status:")
    $st = Get-VStatus
    $states = $st.states
    $onOff = { param($b) if ($b) { "EIN" } else { "AUS" } }
    Write-Host $Prefix
    Write-Host ("  Spannung Ist/Soll : {0} V / {1} V" -f $st.voltage_actual, $st.voltage_setpoint)
    Write-Host ("  Ausgang           : {0}" -f (& $onOff $states.output_on))
    Write-Host ("  Strombegrenzung   : {0}" -f (& $onOff $states.limit_on))
    Write-Host ("  Regelung          : {0}" -f (& $onOff $states.regulation_on))
}

function Wait-VoltageReached {
    <#
        Wartet, bis die Ist-Spannung den Sollwert erreicht hat (GitHub-#6).
        Pollt den Status, bis |Ist - Soll| <= $ReachToleranceV oder $ReachTimeoutS
        abgelaufen ist. Rueckgabe: @{ Reached = [bool]; Actual = [double] oder $null }
    #>
    param([double]$Target)
    $deadline = (Get-Date).AddSeconds($ReachTimeoutS)
    $actual = $null
    while ((Get-Date) -lt $deadline) {
        Start-Sleep -Milliseconds $ReachPollMs
        $st = Get-VStatus
        if ($null -eq $st.voltage_actual) { continue }
        $actual = [double]$st.voltage_actual
        if ([math]::Abs($actual - $Target) -le $ReachToleranceV) {
            return @{ Reached = $true; Actual = $actual }
        }
    }
    return @{ Reached = $false; Actual = $actual }
}

function Wait-Step {
    param([string]$Mode, [double]$Interval, [string]$NextLabel)
    if ($Mode -eq "manual") {
        Read-Host "    >> Enter druecken fuer $NextLabel" | Out-Null
    } else {
        Write-Host ("    >> warte {0:0.##} s bis {1} ..." -f $Interval, $NextLabel)
        Start-Sleep -Milliseconds ([int]($Interval * 1000))
    }
}

function Invoke-SafeShutdown {
    Write-Host ""
    Write-Host "Sicherer Zustand wird hergestellt (0 V, Ausgang AUS) ..."
    try {
        Set-VVoltage -Voltage 0
        Start-Sleep -Milliseconds 300
        Set-VState -Key "output_on" -Action "toggle_output" -Desired $false -Label "Ausgang"
    } catch {
        Write-Host "  WARNUNG: Sicheres Abschalten fehlgeschlagen: $($_.Exception.Message)"
    }
}

function Invoke-Sequence {
    param([string]$Mode, [double]$Interval, [bool]$WithLimit = $true)

    # --- Vorbereitung: 0 V -> Strombegrenzung -> Regelung -> Ausgang ---
    Write-Host ""
    Write-Host "--- Vorbereitung ---"
    Write-Host "Spannung auf 0 V setzen ..."
    Set-VVoltage -Voltage 0
    Start-Sleep -Milliseconds 300

    # GitHub-#7: Sequenz wahlweise mit oder ohne Strombegrenzung fahren.
    $limitWord = if ($WithLimit) { "aktivieren" } else { "ausschalten" }
    Write-Host "Strombegrenzung $limitWord ..."
    Set-VState -Key "limit_on" -Action "toggle_limit" -Desired $WithLimit -Label "Strombegrenzung"

    Write-Host "Spannungsregelung einschalten ..."
    Set-VState -Key "regulation_on" -Action "toggle_regulation" -Desired $true -Label "Regelung"

    Write-Host "Ausgang einschalten ..."
    Set-VState -Key "output_on" -Action "toggle_output" -Desired $true -Label "Ausgang"

    # --- Spannungsschritte ---
    Write-Host ""
    Write-Host "--- Spannungs-Sequenz ---"
    for ($i = 0; $i -lt $VoltageSteps.Count; $i++) {
        $voltage = $VoltageSteps[$i]
        Write-Host ""
        Write-Host ("[{0}/{1}] Soll-Spannung {2} V" -f ($i + 1), $VoltageSteps.Count, $voltage)
        Set-VVoltage -Voltage $voltage
        # GitHub-#6: warten, bis die Regelung den Sollwert erreicht hat,
        # statt nach fixer Pause einen zu fruehen Messwert auszugeben.
        $result = Wait-VoltageReached -Target $voltage
        if ($result.Reached) {
            Write-Host ("    Ist-Spannung: {0} V" -f $result.Actual)
        } else {
            $actualText = if ($null -ne $result.Actual) { "{0} V" -f $result.Actual } else { "?" }
            Write-Host ("    WARNUNG: {0} V nach {1:0.##} s nicht erreicht (Ist: {2})" -f $voltage, $ReachTimeoutS, $actualText)
        }

        # Vor dem naechsten Schritt warten (nach dem letzten Schritt nicht).
        if ($i -lt $VoltageSteps.Count - 1) {
            $nextLabel = "{0} V" -f $VoltageSteps[$i + 1]
            Wait-Step -Mode $Mode -Interval $Interval -NextLabel $nextLabel
        }
    }

    # --- Nach 230 V: Strombegrenzung abschalten? ---
    Write-Host ""
    Write-Host ("--- {0} V erreicht ---" -f $VoltageSteps[-1])
    if (Ask-YesNo "Strombegrenzung jetzt ausschalten?" $false) {
        Set-VState -Key "limit_on" -Action "toggle_limit" -Desired $false -Label "Strombegrenzung"
        Show-VStatus "`nStatus nach Abschalten der Strombegrenzung:"
    }

    # --- Abschluss ---
    Write-Host ""
    Read-Host "Test abgeschlossen? Enter druecken, um Ausgang auszuschalten und die Spannung auf 0 V zu stellen" | Out-Null
    Invoke-SafeShutdown
    Show-VStatus "`nEndzustand:"
}

# ===================== Hauptprogramm =====================

Write-Host "TWM Isolation Variac - Spannungs-Sequenz"
Write-Host "Controller: $script:BaseUrl"

# Verbindung pruefen.
try {
    Show-VStatus "`nVerbindung OK. Aktueller Status:"
} catch {
    Write-Host ""
    Write-Host "FEHLER: Keine Verbindung zum Controller: $($_.Exception.Message)"
    Write-Host "Pruefe IP-Adresse (-Address) und Netzwerkverbindung."
    exit 1
}

Write-Host ""
Write-Host ("Schrittfolge: " + (($VoltageSteps | ForEach-Object { "$_ V" }) -join " -> "))
if (-not (Ask-YesNo "`nSequenz jetzt starten?" $true)) {
    Write-Host "Abgebrochen."
    exit 0
}

$mode = Ask-Mode
$interval = if ($mode -eq "auto") { Ask-Interval } else { 0.0 }
# GitHub-#7: Strombegrenzung fuer die Sequenz waehlbar (sicherer Default: mit).
$withLimit = Ask-YesNo "Sequenz mit Strombegrenzung fahren?" $true

try {
    Invoke-Sequence -Mode $mode -Interval $interval -WithLimit $withLimit
} catch {
    Write-Host ""
    Write-Host "FEHLER: $($_.Exception.Message)"
    Invoke-SafeShutdown
    exit 1
}

Write-Host ""
Write-Host "Fertig."
exit 0
