# ============================================================
#  start_lab.ps1 : démarre toute la partie "usine" du lab
#  (broker MQTT + FTP anonyme + opérateur), avec les réglages
#  courants de lab.env.
#
#  Le broker Mosquitto est lancé directement ici (pas via le service
#  Windows "mosquitto", qui s'est montré peu fiable sur certaines
#  machines) : il faut avoir lancé outils\setup_broker.ps1 au moins
#  une fois avant (il écrit mosquitto.conf + le mot de passe).
# ============================================================
$racine = $PSScriptRoot
Set-Location $racine

python "$racine\outils\generer_config.py"
if ($LASTEXITCODE -ne 0) {
    Write-Host "[!] Génération de la config a échoué (voir ci-dessus). lab.env est-il rempli ?" -ForegroundColor Yellow
    Read-Host "Appuie sur Entrée pour fermer"
    exit 1
}

$valeurs = & python -c "import sys; sys.path.insert(0,'outils'); import lab_env; v=lab_env.charger(); print(v['FTP_PORT']); print(v['INSTANCE_ID'])"
$ftpPort = ($valeurs -split "`n")[0].Trim()
$instance = ($valeurs -split "`n")[1].Trim()

# Vérifie qu'un port est réellement ouvert après le lancement d'un service,
# au lieu de se contenter d'afficher "Démarrage..." sans jamais confirmer que
# ça a marché. Les 3 bugs réels rencontrés sur ce script (ld.exe, service
# Windows capricieux, guillemets manquants dans Start-Process) avaient TOUS
# le même symptôme : le message disait "démarré" alors que rien n'écoutait.
function Verifier-Port($nomService, $port, $tentatives = 10) {
    for ($i = 0; $i -lt $tentatives; $i++) {
        if (Get-NetTCPConnection -LocalPort $port -State Listen -ErrorAction SilentlyContinue) {
            Write-Host "[OK] $nomService écoute bien sur le port $port." -ForegroundColor Green
            return $true
        }
        Start-Sleep -Milliseconds 500
    }
    Write-Host "[!] $nomService NE semble PAS écouter sur le port $port après $($tentatives * 0.5)s." -ForegroundColor Red
    Write-Host "    -> ouvre la fenêtre qui vient de s'ouvrir pour voir l'erreur réelle affichée dedans." -ForegroundColor Red
    return $false
}

$mqExe = "C:\Program Files\mosquitto\mosquitto.exe"
$mqConf = "C:\Program Files\mosquitto\mosquitto.conf"
if ((Get-NetTCPConnection -LocalPort 1883 -State Listen -ErrorAction SilentlyContinue)) {
    Write-Host "[i] Un broker écoute déjà sur le port 1883 (probablement déjà lancé) : pas de 2e instance."
} elseif ((Test-Path $mqExe) -and (Test-Path $mqConf)) {
    Write-Host "Démarrage du broker MQTT (mosquitto)..."
    # IMPORTANT : Start-Process -ArgumentList ne met PAS de guillemets autour des
    # éléments contenant des espaces ("C:\Program Files\...", chemins de projet
    # avec espaces) - il faut les citer nous-mêmes, sinon l'argument est coupé
    # en plusieurs morceaux côté processus lancé (bug vécu : mosquitto.conf
    # introuvable car lu comme juste "C:\Program").
    Start-Process $mqExe -ArgumentList "-c `"$mqConf`" -v" -WindowStyle Normal
    Verifier-Port "Le broker MQTT" 1883 | Out-Null
} else {
    Write-Host "[!] Mosquitto introuvable ou non configuré. Lance d'abord outils\setup_broker.ps1 (admin)." -ForegroundColor Yellow
}

Write-Host "Démarrage du FTP anonyme (port $ftpPort, poste #$instance)..."
# outils/serveur_ftp.py (et non plus "python -m pyftpdlib" générique) :
# journalise toute l'activité FTP hors du dossier servi (voir GUIDE_PROF.md,
# section "Journal FTP"), au lieu d'un simple partage sans trace exploitable.
Start-Process python -ArgumentList "`"$racine\outils\serveur_ftp.py`"" -WindowStyle Normal
Verifier-Port "Le FTP" $ftpPort | Out-Null

Write-Host "Démarrage de l'opérateur (cycle du bras)..."
Start-Process python -ArgumentList "`"$racine\outils\operateur.py`"" -WindowStyle Normal
Start-Sleep -Milliseconds 500
# "python*" et pas "python" : certaines installations (ex: Python du Microsoft
# Store) exposent le process sous un nom versionné ("python3.11"), que
# "Get-Process python" ne trouve pas malgré un processus bien actif - vécu sur
# cette machine (faux-négatif : FTP tournait, le process n'était juste pas vu).
if (Get-Process -ErrorAction SilentlyContinue | Where-Object { $_.ProcessName -like "python*" }) {
    Write-Host "[OK] Processus Python actifs (FTP + opérateur)." -ForegroundColor Green
} else {
    Write-Host "[!] Aucun processus Python détecté : l'opérateur/FTP a probablement planté au démarrage." -ForegroundColor Red
}

Write-Host ""
Write-Host "Lab lancé : broker MQTT (1883) + FTP ($ftpPort) + opérateur en cours." -ForegroundColor Green
Write-Host "Ferme les fenêtres ouvertes pour arrêter le lab."
Read-Host "Appuie sur Entrée pour fermer cette fenêtre (les 3 autres restent actives)"
