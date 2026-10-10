# =====================================================================
#  setup_broker.ps1 : configure le broker MQTT avec mot de passe.
#  À LANCER EN ADMINISTRATEUR (installer_lab.ps1 le fait automatiquement).
#  Après ça, le broker exige toujours l'identifiant (même après reboot).
#
#  Lit les identifiants depuis lab.env (racine du projet) -> change-les
#  là-bas, pas ici, pour que firmware et broker restent synchronisés.
# =====================================================================
$racine = Split-Path -Parent $PSScriptRoot
$fichierEnv = Join-Path $racine "lab.env"

if (-not (Test-Path $fichierEnv)) {
    Write-Host "[!] lab.env introuvable. Lance d'abord installer_lab.ps1 (ou copie lab.env.example en lab.env)." -ForegroundColor Yellow
    exit 1
}

$valeurs = @{}
Get-Content $fichierEnv | ForEach-Object {
    if ($_ -match '^\s*([A-Z_]+)\s*=\s*(.*)$') {
        $valeurs[$matches[1]] = $matches[2].Trim()
    }
}

$mqttUser = if ($valeurs.MQTT_USER) { $valeurs.MQTT_USER } else { "admin" }
$mqttPass = $valeurs.MQTT_PASS
if (-not $mqttPass) {
    Write-Host "[!] MQTT_PASS est vide dans lab.env. Lance 'python outils\generer_config.py' d'abord (il en génère un)." -ForegroundColor Yellow
    exit 1
}

$mq = "C:\Program Files\mosquitto"
if (-not (Test-Path $mq)) {
    Write-Host "[!] Mosquitto introuvable dans '$mq'. Installe-le : https://mosquitto.org/download/" -ForegroundColor Yellow
    exit 1
}

& "$mq\mosquitto_passwd.exe" -c -b "$mq\pwd.txt" $mqttUser $mqttPass

$conf = @"
listener 1883 0.0.0.0
allow_anonymous false
password_file $mq\pwd.txt
"@

# ---- Exercice bonus : listener MQTT chiffré (8883), en plus du 1883 en clair
# du TP de base. Voir lab.env (TLS_ACTIF) et README.md section Sécurité.
$tlsActif = ($valeurs.TLS_ACTIF -eq "true")
if ($tlsActif) {
    & "$PSScriptRoot\generer_certs_tls.ps1"
    $dossierTls = Join-Path $racine "tls"
    if (Test-Path (Join-Path $dossierTls "server.crt")) {
        $conf += @"

listener 8883 0.0.0.0
allow_anonymous false
password_file $mq\pwd.txt
cafile $dossierTls\ca.crt
certfile $dossierTls\server.crt
keyfile $dossierTls\server.key
"@
        Write-Host "[OK] Listener TLS ajouté sur le port 8883 (certificats dans '$dossierTls')." -ForegroundColor Green
    } else {
        Write-Host "[!] TLS_ACTIF=true mais les certificats n'ont pas pu être générés : port 8883 non activé." -ForegroundColor Yellow
    }
}

$conf | Set-Content -Encoding ascii "$mq\mosquitto.conf"

# ---- Le service Windows "mosquitto" s'est montré peu fiable sur certaines
# machines (reste "Stopped" après Restart-Service, sans erreur exploitable,
# alors que le même mosquitto.conf fonctionne parfaitement lancé directement).
# On ne s'appuie donc plus sur lui : on le désactive (s'il existe) pour éviter
# tout conflit de port, et c'est désormais start_lab.ps1 qui lance mosquitto.exe
# directement (comme le FTP et l'opérateur), ce qui est plus simple à déboguer.
$service = Get-Service mosquitto -ErrorAction SilentlyContinue
if ($service) {
    Stop-Service mosquitto -Force -ErrorAction SilentlyContinue
    Set-Service mosquitto -StartupType Disabled -ErrorAction SilentlyContinue
    Write-Host "[i] Service Windows 'mosquitto' désactivé (le broker est maintenant lancé par start_lab.ps1)." -ForegroundColor DarkYellow
}

Write-Host ""
Write-Host "[OK] Broker configure : compte $mqttUser / (mot de passe de lab.env), anonyme bloque." -ForegroundColor Green
Write-Host "     Lance (ou relance) start_lab.bat pour démarrer le broker avec cette config."
Write-Host "     Verifie ensuite : mosquitto_sub -h <ip> -t '#' -v  (sans -u/-P) doit etre REFUSE."
if ($tlsActif) {
    Write-Host "     TLS (8883) : mosquitto_sub -h <ip> -p 8883 --cafile tls\ca.crt -u $mqttUser -P <mdp> -t '#' -v"
}
