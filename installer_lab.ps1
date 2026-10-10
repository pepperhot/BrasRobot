# ============================================================
#  installer_lab.ps1 : installation "one-click" du lab complet.
#
#  Lance ce script (clic droit -> "Exécuter avec PowerShell", ou
#  `powershell -ExecutionPolicy Bypass -File installer_lab.ps1`)
#  sur un PC neuf. Il :
#   1. Vérifie/installe les dépendances (Python, Mosquitto, PlatformIO, Nmap)
#      via winget quand c'est possible.
#   2. Crée lab.env si absent et complète les champs manquants
#      (IP du broker détectée automatiquement, mot de passe MQTT
#      généré aléatoirement si vide) -> point 1 "config qui se met à jour".
#   3. Génère include/config.h + synchronise les wordlists FTP depuis lab.env.
#   4. Ouvre les ports du lab dans le pare-feu Windows (21, 80, 1883, 8883, 502)
#      et configure le mot de passe du broker Mosquitto (élévation UAC).
#      Sans l'ouverture du pare-feu, un scan nmap depuis une autre machine du
#      réseau ne trouve souvent rien, même si les services tournent en local.
#   5. Propose de flasher l'ESP32 (si branché) et de démarrer le lab.
#
#  Peut être relancé à volonté (idempotent) : utile aussi pour "scalabiliser"
#  le lab sur un nouveau poste (change juste INSTANCE_ID dans lab.env avant).
# ============================================================

$ErrorActionPreference = "Stop"
$racine = $PSScriptRoot
Set-Location $racine

function Titre($texte) {
    Write-Host ""
    Write-Host "== $texte ==" -ForegroundColor Cyan
}

function Commande-Disponible($nom) {
    return [bool](Get-Command $nom -ErrorAction SilentlyContinue)
}

# ---------- 1. Dépendances ----------
Titre "1/5 - Vérification des dépendances"

if (-not (Commande-Disponible "python")) {
    Write-Host "[!] Python introuvable." -ForegroundColor Yellow
    if (Commande-Disponible "winget") {
        Write-Host "    Installation via winget..."
        winget install -e --id Python.Python.3.11
    } else {
        Write-Host "    Installe-le manuellement : https://www.python.org (coche 'Add to PATH')." -ForegroundColor Yellow
        Read-Host "Appuie sur Entrée une fois Python installé pour continuer"
    }
} else {
    Write-Host "[OK] Python : $(python --version)"
}

Write-Host "Installation/mise à jour des paquets Python (pyftpdlib, platformio)..."
python -m pip install --quiet --upgrade pip
python -m pip install --quiet pyftpdlib platformio

if (-not (Test-Path "C:\Program Files\mosquitto\mosquitto.exe")) {
    Write-Host "[!] Mosquitto introuvable." -ForegroundColor Yellow
    if (Commande-Disponible "winget") {
        Write-Host "    Installation via winget..."
        winget install -e --id EclipseMosquitto.Mosquitto
    } else {
        Write-Host "    Installe-le manuellement : https://mosquitto.org/download/" -ForegroundColor Yellow
        Read-Host "Appuie sur Entrée une fois Mosquitto installé pour continuer"
    }
} else {
    Write-Host "[OK] Mosquitto déjà installé."
}

if (-not (Commande-Disponible "nmap")) {
    Write-Host "[i] Nmap non trouvé dans le PATH (utile côté 'attaquant' du TP, pas obligatoire ici)." -ForegroundColor DarkYellow
} else {
    Write-Host "[OK] Nmap disponible."
}

# On AJOUTE les entrées PATH Machine/Utilisateur manquantes (ex: Mosquitto/winget
# venant d'être installés) sans écraser le PATH actuel du processus : sinon, si ce
# script tourne dans un venv Python (Activate.ps1), on perdrait son dossier Scripts
# et "python"/"pio" cesseraient de pointer vers l'environnement voulu, causant
# ensuite "No module named platformio" à l'étape 5.
$pathActuel = $env:Path -split ";"
$pathCible = (([System.Environment]::GetEnvironmentVariable("Path","Machine")) + ";" +
              ([System.Environment]::GetEnvironmentVariable("Path","User"))) -split ";"
foreach ($p in $pathCible) {
    if ($p -and ($pathActuel -notcontains $p)) {
        $env:Path += ";$p"
        $pathActuel += $p
    }
}

# ---------- 2. lab.env ----------
Titre "2/5 - Configuration (lab.env)"

$fichierEnv = Join-Path $racine "lab.env"
if (-not (Test-Path $fichierEnv)) {
    Copy-Item (Join-Path $racine "lab.env.example") $fichierEnv
    Write-Host "[OK] lab.env créé depuis lab.env.example."
}

# Lit les valeurs actuelles
function Lire-Env($cle) {
    $ligne = Get-Content $fichierEnv | Where-Object { $_ -match "^\s*$cle\s*=" } | Select-Object -First 1
    if ($ligne) { return ($ligne -split "=", 2)[1].Trim() }
    return ""
}
function Ecrire-Env($cle, $valeur) {
    $lignes = Get-Content $fichierEnv
    $trouve = $false
    $lignes = $lignes | ForEach-Object {
        if ($_ -match "^\s*$cle\s*=") { $trouve = $true; "$cle=$valeur" } else { $_ }
    }
    if (-not $trouve) { $lignes += "$cle=$valeur" }
    Set-Content -Path $fichierEnv -Value $lignes -Encoding utf8
}

$ssidActuel = Lire-Env "WIFI_SSID"
if (-not $ssidActuel) {
    $ssidDetecte = $null
    try { $ssidDetecte = (netsh wlan show interfaces | Select-String "^\s*SSID\s*:\s*(.+)$").Matches[0].Groups[1].Value.Trim() } catch {}
    $suggestion = if ($ssidDetecte) { " [$ssidDetecte]" } else { "" }
    $saisie = Read-Host "SSID du Wi-Fi du lieu$suggestion"
    if (-not $saisie -and $ssidDetecte) { $saisie = $ssidDetecte }
    if ($saisie) { Ecrire-Env "WIFI_SSID" $saisie }

    $motdepasse = Read-Host "Mot de passe Wi-Fi" -AsSecureString
    $motdepasseClair = [System.Runtime.InteropServices.Marshal]::PtrToStringAuto([System.Runtime.InteropServices.Marshal]::SecureStringToBSTR($motdepasse))
    if ($motdepasseClair) { Ecrire-Env "WIFI_PASS" $motdepasseClair }
} else {
    Write-Host "[OK] Wi-Fi déjà renseigné dans lab.env (SSID: $ssidActuel)."
}

$instance = Lire-Env "INSTANCE_ID"
Write-Host "[i] Poste actuel : INSTANCE_ID=$instance (change cette ligne dans lab.env pour ajouter un 2e banc sans collision)."

# ---------- 3. Génération config.h + wordlists ----------
Titre "3/5 - Génération de include/config.h et des wordlists"
python "$racine\outils\generer_config.py"
if ($LASTEXITCODE -ne 0) {
    Write-Host "[!] generer_config.py a échoué (code $LASTEXITCODE) : voir le message d'erreur ci-dessus." -ForegroundColor Red
}

# ---------- 4. Broker MQTT + pare-feu ----------
Titre "4/5 - Configuration du broker MQTT (mot de passe) et du pare-feu"
Write-Host "Une fenêtre d'autorisation administrateur va s'ouvrir (service Mosquitto + règles pare-feu)."
Write-Host "-> Si tu cliques 'Non' sur la fenêtre UAC, l'étape échoue silencieusement : vérifie le résultat ci-dessous."
# Pare-feu d'abord : sans ça, un scan nmap depuis une autre machine du réseau
# ne trouve souvent AUCUN port ouvert (profil Public/Domaine bloqué par défaut),
# même si FTP/MQTT tournent bien en local.
# -PassThru + vérif du code retour : un Start-Process -Verb RunAs qui échoue
# (UAC refusée, erreur dans le script élevé) ne lève AUCUNE exception ici,
# il faut explicitement regarder son ExitCode pour le savoir.
$p1 = Start-Process powershell -Verb RunAs -PassThru -Wait -ArgumentList @(
    "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", "`"$racine\outils\configurer_firewall.ps1`""
)
if ($p1.ExitCode -ne 0) { Write-Host "[!] configurer_firewall.ps1 a retourné le code $($p1.ExitCode)." -ForegroundColor Red }

$p2 = Start-Process powershell -Verb RunAs -PassThru -Wait -ArgumentList @(
    "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", "`"$racine\outils\setup_broker.ps1`""
)
if ($p2.ExitCode -ne 0) { Write-Host "[!] setup_broker.ps1 a retourné le code $($p2.ExitCode)." -ForegroundColor Red } else { Write-Host "[OK] Broker configuré." -ForegroundColor Green }

# ---------- 5. ESP32 + lancement ----------
Titre "5/5 - Carte ESP32 et lancement du lab"

$reponse = Read-Host "Flasher le firmware sur l'ESP32 maintenant (carte branchée en USB) ? (o/N)"
if ($reponse -match "^[oOyY]") {
    Push-Location $racine
    python -m platformio run -t upload
    if ($LASTEXITCODE -eq 0) {
        Write-Host "[OK] Flash réussi." -ForegroundColor Green
    } else {
        Write-Host "[!] Flash ÉCHOUÉ (code $LASTEXITCODE) : voir le message d'erreur ci-dessus (port COM, carte non branchée, pilote manquant...)." -ForegroundColor Red
    }
    Pop-Location
} else {
    Write-Host "[i] Tu pourras flasher plus tard avec : pio run -t upload"
}

Write-Host ""
$reponse2 = Read-Host "Démarrer le lab maintenant (FTP + opérateur) ? (O/n)"
if (-not ($reponse2 -match "^[nN]")) {
    & "$racine\start_lab.ps1"
} else {
    Write-Host "Tu pourras démarrer le lab plus tard avec start_lab.bat."
}

Write-Host ""
Write-Host "[OK] Installation terminée. Voir GUIDE_PROF.md pour le détail et GUIDE_ETUDIANT.md pour le TP." -ForegroundColor Green
