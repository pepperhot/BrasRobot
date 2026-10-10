# ============================================================
#  generer_certs_tls.ps1 : génère une petite CA + un certificat serveur
#  pour activer un listener MQTT chiffré (port 8883) en plus du port 1883
#  en clair du TP de base. Appelé automatiquement par setup_broker.ps1
#  quand TLS_ACTIF=true dans lab.env.
#
#  Nécessite openssl (fourni par Git for Windows : vérifie que
#  "C:\Program Files\Git\usr\bin" ou "C:\Program Files\Git\bin" est dans le PATH,
#  ou installe-le via "winget install -e --id ShiningLight.OpenSSL").
#
#  Usage démo (depuis un PC, PAS depuis le firmware ESP32 - voir GUIDE_PROF.md) :
#    mosquitto_sub -h <ip> -p 8883 --cafile tls\ca.crt -u <user> -P <pass> -t "#" -v
#  Compare la capture Wireshark sur le port 1883 (clair) et 8883 (chiffré).
# ============================================================
param([switch]$Force)

$racine = Split-Path -Parent $PSScriptRoot
$dossier = Join-Path $racine "tls"

if ((Test-Path (Join-Path $dossier "server.crt")) -and -not $Force) {
    Write-Host "[OK] Certificats TLS déjà présents dans '$dossier' (utilise -Force pour régénérer)."
    exit 0
}

if (-not (Get-Command openssl -ErrorAction SilentlyContinue)) {
    Write-Host "[!] openssl introuvable dans le PATH. Installe Git for Windows (il l'inclut) " -ForegroundColor Yellow
    Write-Host "    ou 'winget install -e --id ShiningLight.OpenSSL', puis relance." -ForegroundColor Yellow
    exit 1
}

New-Item -ItemType Directory -Force -Path $dossier | Out-Null
Push-Location $dossier
try {
    Write-Host "Génération de la CA du lab (usage pédagogique uniquement, ne pas réutiliser ailleurs)..."
    openssl genrsa -out ca.key 2048 2>$null
    openssl req -x509 -new -nodes -key ca.key -sha256 -days 3650 `
        -subj "/CN=BrasRobot-Lab-CA" -out ca.crt 2>$null

    Write-Host "Génération du certificat serveur (broker Mosquitto)..."
    openssl genrsa -out server.key 2048 2>$null
    openssl req -new -key server.key -subj "/CN=brasrobot-broker" -out server.csr 2>$null
    openssl x509 -req -in server.csr -CA ca.crt -CAkey ca.key -CAcreateserial `
        -out server.crt -days 1825 -sha256 2>$null

    Write-Host "[OK] Certificats générés dans '$dossier' (ca.crt à distribuer aux clients --cafile)." -ForegroundColor Green
} finally {
    Pop-Location
}
