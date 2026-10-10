# ============================================================
#  configurer_firewall.ps1 : ouvre dans le pare-feu Windows les ports
#  utilisés par le lab, pour qu'un autre PC du réseau (le "poste étudiant"
#  qui scanne avec nmap) puisse réellement les atteindre.
#
#  À LANCER EN ADMINISTRATEUR (installer_lab.ps1 le fait automatiquement).
#  Sans ça, un scan nmap depuis une autre machine ne trouve souvent RIEN
#  (le pare-feu Windows bloque par défaut les connexions entrantes sur les
#  profils Public/Domaine), même si les services tournent bien en local.
# ============================================================
$ports = @(
    @{ Nom = "BrasRobot-HTTP";      Port = 80;   Desc = "Page web du bras (ESP32)" }
    @{ Nom = "BrasRobot-FTP";       Port = 21;   Desc = "FTP anonyme du lab" }
    @{ Nom = "BrasRobot-MQTT";      Port = 1883; Desc = "Broker MQTT (Mosquitto)" }
    @{ Nom = "BrasRobot-MQTT-TLS";  Port = 8883; Desc = "Broker MQTT chiffré (exercice bonus)" }
    @{ Nom = "BrasRobot-Modbus";    Port = 502;  Desc = "Modbus TCP (lab ICS)" }
)

foreach ($p in $ports) {
    if (-not (Get-NetFirewallRule -DisplayName $p.Nom -ErrorAction SilentlyContinue)) {
        New-NetFirewallRule -DisplayName $p.Nom -Description $p.Desc `
            -Direction Inbound -Protocol TCP -LocalPort $p.Port -Action Allow | Out-Null
        Write-Host "[OK] Pare-feu : port $($p.Port) ouvert ($($p.Nom))."
    } else {
        Write-Host "[OK] Pare-feu : règle '$($p.Nom)' déjà présente."
    }
}

# Mosquitto s'installe parfois sans règle de pare-feu propre : au cas où le
# port seul ne suffit pas (profil réseau "Public"), on autorise aussi l'exe.
$mosquittoExe = "C:\Program Files\mosquitto\mosquitto.exe"
if ((Test-Path $mosquittoExe) -and -not (Get-NetFirewallRule -DisplayName "BrasRobot-mosquitto.exe" -ErrorAction SilentlyContinue)) {
    New-NetFirewallRule -DisplayName "BrasRobot-mosquitto.exe" -Direction Inbound `
        -Program $mosquittoExe -Action Allow | Out-Null
    Write-Host "[OK] Pare-feu : mosquitto.exe autorisé en entrée."
}

Write-Host ""
Write-Host "[i] Si un scan nmap depuis une autre machine ne voit toujours rien :" -ForegroundColor DarkYellow
Write-Host "    vérifie le profil réseau actif (Public force souvent plus de restrictions) :"
Write-Host "    Get-NetConnectionProfile   (passe-le en 'Private' si c'est un réseau de confiance)."
