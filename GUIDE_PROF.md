# Guide encadrant (prof) — déploiement du lab

Ce guide installe et met en place **tout le banc** sur une machine neuve : le **bras robot**
(ESP32), le **broker MQTT** (Mosquitto + mot de passe), et l'**installateur du lab** (FTP,
opérateur, wordlists). Une fois fait, les étudiants n'ont plus qu'à suivre `GUIDE_ETUDIANT.md`.

> Ce document contient le **mot de passe du lab** et la configuration — il est réservé à
> l'encadrant. Ne le distribue pas aux étudiants (eux doivent le découvrir pendant le TP).

---

## Installation en un clic (nouveau PC)

1. Copie le dossier `BrasRobot` complet sur le nouveau PC.
2. Clic droit sur **`installer_lab.ps1`** → *Exécuter avec PowerShell*
   (ou `powershell -ExecutionPolicy Bypass -File installer_lab.ps1` dans un terminal).

Le script fait **tout** automatiquement :
- installe Python, Mosquitto, PlatformIO (via `winget` si disponible, sinon il indique le lien) ;
- crée `lab.env` (config centrale, voir plus bas), demande le Wi-Fi s'il manque, et détecte
  seul l'IP du PC pour le broker ;
- génère `include/config.h` depuis `include/config.h.template` + `lab.env` ;
- resynchronise automatiquement `ftp_wordlists/` avec les identifiants MQTT actuels (important,
  voir ci-dessous) ;
- configure le mot de passe Mosquitto (demande l'élévation UAC) ;
- propose de flasher l'ESP32 puis de démarrer le lab.

Il peut être relancé autant de fois que nécessaire (idempotent) : utile après avoir changé
le Wi-Fi, ajouté un poste, etc.

Les sections suivantes détaillent ce que fait l'installeur, pour comprendre/dépanner.

---

## 1. Matériel

- Un PC Windows (la « machine du banc »).
- Le bras + la carte **ESP32** reliés, avec l'alimentation des servos.
- Le même réseau Wi-Fi pour le PC et l'ESP32.

---

## 2. La configuration centrale : `lab.env`

**Tout le réseau du lab part d'un seul fichier : `lab.env`** (à la racine, créé depuis
`lab.env.example` au premier lancement). Il contient le Wi-Fi, l'IP/les identifiants du
broker MQTT, et l'identifiant de poste (`INSTANCE_ID`, voir « Scalabilité » plus bas).

**Ne modifie plus `include/config.h` directement** pour le réseau : ce fichier est désormais
**généré automatiquement** par `outils/generer_config.py` à partir de
`include/config.h.template` + `lab.env`, à chaque lancement de `installer_lab.ps1` ou
`start_lab.bat`. (Les réglages physiques du bras — câblage, dimensions, limites de
servos — restent eux dans `include/config.h.template`, propres à ce bras.)

Particularités utiles de `lab.env` :
- `MQTT_SERVEUR=auto` → l'IP du PC est **redétectée à chaque génération** : si la machine
  change de réseau ou d'IP (DHCP), tout se remet à jour seul, sans rien retoucher.
- `MQTT_PASS` vide → un mot de passe fort est **généré automatiquement** à la première
  exécution et réécrit dans `lab.env` pour être réutilisé ensuite.
- ⚠️ **Important** : l'exercice 3 du TP (brute-force) suppose que le mot de passe MQTT
  actuel se trouve dans `ftp_wordlists/passwords.txt`. `generer_config.py` **synchronise
  automatiquement** cette wordlist avec `lab.env` à chaque génération — donc même si tu
  changes/régénères le mot de passe, le TP reste faisable sans intervention manuelle.

Si tu préfères tout faire à la main plutôt que via `installer_lab.ps1` :
```
copy lab.env.example lab.env
notepad lab.env            REM remplis WIFI_SSID / WIFI_PASS
python outils\generer_config.py
```

---

## 3. Flasher le bras

Une fois `lab.env` rempli et `include/config.h` généré (ESP32 branché en USB) :
```
pio run -t upload
```
Note l'IP affichée du bras au démarrage (moniteur série) ou via `http://brasrobot.local`.

---

## 4. Sécuriser le broker (mot de passe)

Par défaut Mosquitto accepte tout le monde. `installer_lab.ps1` le fait automatiquement ;
pour le refaire seul : **double-clic** sur `outils\setup_broker.bat` → **clic droit →
Exécuter en tant qu'administrateur** → « Oui » sur la fenêtre UAC.

Le script lit désormais `MQTT_USER`/`MQTT_PASS` **depuis `lab.env`** (plus rien en dur),
met `allow_anonymous false`, et redémarre le service. C'est **permanent** (même après reboot).

Vérifie :
```
mosquitto_sub -h <IP_PC> -t "#" -v
```
→ **sans** mot de passe doit être **refusé** (`not authorised`).

---

## 5. Lancer le lab

Double-clic sur **`start_lab.bat`** (à la racine). Il régénère `config.h`/wordlists depuis
`lab.env` puis démarre :
- le **FTP anonyme** (port défini par `FTP_PORT` dans `lab.env`, 21 par défaut) qui sert
  `users.txt` + `passwords.txt`,
- l'**opérateur** (le bras suit son cycle → génère le trafic), avec les identifiants courants.

Le broker, lui, tourne tout seul (service Mosquitto).

---

## 6. Scalabilité : plusieurs postes/bancs en parallèle

Pour déployer **plusieurs bancs** (salles, groupes) sans qu'ils se gênent sur le même réseau,
change une seule valeur dans le `lab.env` de chaque poste supplémentaire :
```
INSTANCE_ID=2     (puis 3, 4... un numéro différent par poste)
```
Relance `installer_lab.ps1` (ou `python outils\generer_config.py`). Chaque poste obtient
automatiquement, sans rien configurer d'autre :
- ses propres topics MQTT : `brasrobot2/...` (au lieu de `brasrobot/...`) ;
- son propre nom réseau : `brasrobot2.local` ;
- son propre SSID de secours : `BrasRobot2` ;
- son propre port FTP (`2102`, puis `2103`…) et sa propre wordlist (`ftp_wordlists_2/`,
  recopiée et tenue à jour automatiquement depuis `ftp_wordlists/`).

Chaque broker MQTT doit rester sur sa propre machine (un broker = un banc) ; seuls les
topics/ports/wordlists sont ainsi isolés pour éviter toute collision si plusieurs bancs
partagent le même réseau physique ou, pour les tests, le même PC.

---

## 7. Faire le TP

Tout est prêt : suis **`GUIDE_ETUDIANT.md`** (scan → FTP → brute-force → écoute/commande).

---

## 7 bis. Journal FTP : deux niveaux, simples et clairs

`start_lab.ps1` lance `outils/serveur_ftp.py` (et non plus un simple `python -m
pyftpdlib`), qui tient **deux journaux séparés**, chacun avec un rôle précis :

**1. Côté étudiant — `connexions.log` (visible, normal, dans le partage)**
```
ftp_wordlists/journal/connexions.log
```
Un dossier `journal/` normal, qui apparaît dans n'importe quel `ls`/`dir` du FTP, contenant
un fichier qui liste chaque connexion (IP, date/heure, login/échec de login,
déconnexion). Les étudiants ont le droit de le **supprimer** (`delete`) ou de le
**modifier** (`get` + édition locale + `put`) — c'est l'exercice 5 de `GUIDE_ETUDIANT.md`
(« Effacer ses traces (ou pas) »), qui montre les deux approches :
- suppression complète (`delete connexions.log`) ;
- suppression **ciblée** de leurs seules lignes (en téléchargeant, en filtrant les lignes
  contenant leur IP avec `sed -i '/<IP>/d'` ou l'équivalent PowerShell, puis en renvoyant
  le fichier) — plus discret, ne touche pas aux lignes des autres étudiants.

Testé : supprimer `connexions.log` **pendant** que le serveur continue d'écrire dedans
fonctionne sans erreur (chaque écriture ouvre/ferme le fichier immédiatement, pas de
verrou Windows) ; le fichier est automatiquement recréé à l'écriture suivante.

**2. Côté prof — le journal complet du protocole (hors du partage)**
```
outils/logs/ftp_<date>_<heure>.log
```
Ce dossier n'est **jamais exposé par le serveur FTP** (séparation normale entre "ce que
sert le FTP" et "les logs internes du processus serveur", pas une ruse) : il contient le
détail complet de chaque commande FTP de chaque connexion (`CWD`, `RETR`, `STOR`, `DELE`...),
avec IP, port et horodatage précis — y compris les suppressions/modifications de
`connexions.log` elles-mêmes. Même si un étudiant "nettoie" `connexions.log`, ce journal-ci
garde tout.

> **Transparence envers les étudiants :** informe-les en termes généraux, avant le TP, que
> leur activité réseau sur le banc est journalisée à des fins de correction — c'est une
> pratique normale pour tout banc de test supervisé.

Pense à vider `outils\logs\` entre deux groupes si tu veux repartir propre (le dossier
est de toute façon ignoré par Git).

### Lire le journal complet comme examinateur

Le fichier brut (`outils\logs\ftp_<date>.log`) mélange toutes les connexions de la session
(une ligne par commande, préfixée par IP:port). Pour s'y retrouver, utilise
`outils/analyser_logs_ftp.py`, qui regroupe les lignes par **passage** (= une connexion
jusqu'à la déconnexion) :

```
python outils\analyser_logs_ftp.py                          # toutes les sessions
python outils\analyser_logs_ftp.py --ip 192.168.1.50         # un seul étudiant (son IP)
python outils\analyser_logs_ftp.py --a-touche-journal        # qui a modifié/supprimé connexions.log
```

Exemple de sortie pour un passage :
```
=== Session 192.168.1.50:49159 [anonymous] [!] a modifie/supprime connexions.log ===
    debut : 2026-10-04 16:27:11   fin : 2026-10-04 16:27:37
    16:27:14  USER anonymous
    16:27:32  CWD journal
    16:27:32  RETR connexions.log
    16:27:32  DELE connexions.log
```

---

## 8. Exercice bonus : refermer les failles (après le TP de base)

Une fois le TP de base réussi, deux correctifs sont prêts à activer **sans toucher au
code**, uniquement via `lab.env`, pour montrer concrètement l'effet d'une sécurisation :

**a) Authentification HTTP sur les routes de contrôle**
```
HTTP_AUTH_ACTIF=true
HTTP_AUTH_USER=admin
HTTP_AUTH_PASS=        (laisse vide : généré automatiquement)
```
Puis `python outils\generer_config.py` et reflashe (`pio run -t upload`). Les routes
`/set`, `/ik`, `/repos`, `/limites`, `/trim`, `/cal` exigent alors un compte (popup
navigateur standard) ; `/etat`, `/pos` et les fichiers 3D restent en lecture libre.
Fais constater aux étudiants qu'une requête `curl http://<ip>/set?s=0&a=90` sans
identifiants reçoit désormais un `401`.

**b) MQTT chiffré (TLS, port 8883) en plus du 1883 en clair**
```
TLS_ACTIF=true
```
Puis relance `outils\setup_broker.bat` (admin) : il génère une CA + certificat
(`outils\generer_certs_tls.ps1`, fichiers dans `tls/`, non versionnés) et ouvre un
listener 8883. Démo Wireshark : capture le trafic `mosquitto_pub`/`sub` sur 1883 (le
payload JSON est lisible en clair), puis sur 8883 avec `--cafile tls\ca.crt` (payload
chiffré, illisible).

⚠️ **Limite connue** : ce TLS ne protège que PC ↔ broker (ex: outils d'attaque,
`operateur.py`). Le firmware ESP32 continue de parler en clair sur le port 1883 : lui
faire parler TLS demanderait `WiFiClientSecure` + un certificat racine embarqué, ce qui
alourdit sensiblement le firmware (flash/RAM) et sort du périmètre actuel. À mentionner
explicitement aux étudiants comme limite réelle, pas comme faille cachée.

Pense à repasser `HTTP_AUTH_ACTIF=false` et `TLS_ACTIF=false` (puis régénérer/reflasher)
avant la session suivante si tu veux repartir du TP de base.

---

## Récapitulatif « nouveau PC »
1. Copier le dossier `BrasRobot`.
2. Exécuter **`installer_lab.ps1`** (installe les dépendances, configure tout, propose de
   flasher et de lancer le lab).
3. Suivre `GUIDE_ETUDIANT.md`.

(Détail manuel équivalent si besoin : installer Mosquitto/Python+pyftpdlib/PlatformIO/Nmap →
remplir `lab.env` → `python outils\generer_config.py` → `outils\setup_broker.bat` (admin) →
`start_lab.bat`.)

## Fichiers clés
| Fichier | Rôle |
|---|---|
| `lab.env` (depuis `lab.env.example`) | **configuration centrale** : Wi-Fi, broker MQTT, n° de poste |
| `installer_lab.ps1` | installation one-click complète (dépendances + config + broker + flash + lancement) |
| `include/config.h.template` | réglages physiques du bras (câblage, dimensions, limites) |
| `include/config.h` | **généré automatiquement**, ne pas éditer à la main |
| `outils/generer_config.py` | régénère `config.h` + resynchronise les wordlists depuis `lab.env` |
| `outils/setup_broker.ps1`/`.bat` | active le mot de passe du broker (admin, une fois) — lit `lab.env` |
| `outils/configurer_firewall.ps1` | ouvre les ports du lab (21, 80, 1883, 8883, 502) dans le pare-feu Windows (admin) |
| `start_lab.bat` / `start_lab.ps1` | régénère la config puis démarre broker + FTP + opérateur |
| `outils/serveur_ftp.py` | FTP anonyme **journalisé** (voir §7 bis) : `connexions.log` visible/supprimable côté étudiant + journal complet hors FTP côté prof |
| `ftp_wordlists/journal/connexions.log` | journal visible dans le partage, que les étudiants peuvent supprimer/modifier (exercice 5) |
| `outils/logs/` | journal complet du protocole FTP, hors du partage (non versionné) |
| `outils/analyser_logs_ftp.py` | résumé lisible par passage/étudiant à partir du journal complet, filtrable par IP |
| `ftp_wordlists/` (et `ftp_wordlists_N/`) | `users.txt` + `passwords.txt` servis par le FTP, par poste |
| `outils/operateur.py` | fait tourner le bras (trafic normal), lit `lab.env` |
| `outils/generer_certs_tls.ps1` | génère la CA/certificat pour le listener MQTT 8883 (si `TLS_ACTIF=true`) |
| `tls/` | certificats TLS générés (non versionné) |
| `GUIDE_ETUDIANT.md` | le TP |

## Si tu mets le projet sous Git

Le dossier contient deux fichiers 3D volumineux (`tinkercad/robot_tinkercad.glb` ≈ 2 Mo,
`data/robot.glb.gz` ≈ 330 Ko). `.gitattributes` les déclare déjà pour **Git LFS** : avant
le premier commit, installe-le une fois (`git lfs install`), sinon ces fichiers gonflent
l'historique à chaque modification. `.gitignore` est déjà prêt (`lab.env`,
`include/config.h`, `tls/`, `ftp_wordlists_*/`, `.venv/`, `.pio/` ne sont jamais commités).
