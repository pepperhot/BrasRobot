# Bras Robot ESP32 : interface web 3D + MQTT

```
BrasRobot/
├── BrasRobot.ino   ← le programme (servos, calculs, serveur web, MQTT)
├── config.h        ← TES réglages : WiFi, MQTT, pins, dimensions, limites
├── page.h          ← la page web (curseurs + 3D) ; réglage du modèle Blender dedans
├── data/           ← fichiers à envoyer dans la mémoire LittleFS de l'ESP32
│   └── robot.glb.gz ← ton robot 3D animable (généré depuis Tinkercad)
├── tinkercad/      ← ton export Tinkercad brut (robot_tinkercad.glb)
└── outils/
    ├── tinkercad_vers_glb.py ← Tinkercad -> data/robot.glb.gz
    └── simulateur.py         ← la page web sur ton PC, sans ESP32
```

## 0. Voir le robot 3D sans l'ESP32

```
python outils/simulateur.py
```
puis ouvre http://localhost:8080 : c'est la vraie page, avec ton modèle Tinkercad animé par les curseurs.

## 1. Câblage

| Servo  | GPIO | Rôle |
|--------|------|------|
| Base   | 14   | rotation autour de l'axe vertical |
| Epaule | 27   | lève / baisse le bras |
| Coude  | 16   | plie l'avant-bras |
| Pince  | 17   | ouvre / ferme |

⚠️ **Alimentation :** ne branche jamais les servos sur le 5 V / 3,3 V de l'ESP32. Utilise une alim 5–6 V externe (≥ 3 A pour des MG996R), **GND commun** avec l'ESP32, et un condensateur de 470–1000 µF sur l'alim servos. Sinon, l'ESP32 redémarre à chaque mouvement (brown-out).

⚠️ **GPIO 16/17 :** OK sur un ESP32-**WROOM** (le cas de la plupart des cartes « DevKit »). Sur un ESP32-**WROVER** (avec PSRAM), ces pins sont prises : change-les dans `config.h`.

## 2. Installation

**Lab complet (prof) :** voir `GUIDE_PROF.md` → `installer_lab.ps1` installe et configure tout
en un clic (dépendances, Wi-Fi/MQTT via `lab.env`, broker, flash, lancement).

**À la main (Arduino IDE 2) :**
1. Cartes : **esp32 by Espressif** (version 3.x) → carte **ESP32 Dev Module**.
2. Bibliothèques : **ESP32Servo** (Kevin Harrington) et **PubSubClient** (Nick O'Leary).
3. Copie `lab.env.example` → `lab.env`, remplis WiFi/MQTT, puis génère la config réseau :
   `python outils/generer_config.py` (régénère `include/config.h` depuis `lab.env` ;
   les dimensions du bras, elles, restent dans `include/config.h.template`).
4. Téléverse le sketch, puis ouvre le moniteur série (115200) : l'adresse IP s'affiche. Tu peux aussi utiliser `http://brasrobot.local`.
5. **Fichiers 3D** : installe le plugin [arduino-littlefs-upload](https://github.com/earlephilhower/arduino-littlefs-upload), ferme le moniteur série, puis `Ctrl+Shift+P` → *Upload LittleFS to Pico/ESP8266/ESP32*. Cela envoie le dossier `data/`.
   - Sans cette étape, la page charge three.js depuis internet (ça marche seulement si ton téléphone ou ton PC a internet).

Testé : compile pour « ESP32 Dev Module », core 3.0.7, 82 % de la flash (schéma de partition par défaut).

Si le WiFi échoue, l'ESP32 crée le réseau **BrasRobot** (mdp `12345678`) → `http://192.168.4.1`.

## 3. Modèle 3D depuis Tinkercad

Tinkercad exporte tout le robot en **un seul objet** : impossible à animer tel quel.
1. Tinkercad → *Exporter* → **.GLB**, enregistre-le sous `tinkercad/robot_tinkercad.glb`.
2. `python outils/tinkercad_vers_glb.py` (besoin de `pip install numpy`). Le script sépare les pièces,
   les rattache aux servos et écrit `data/robot.glb.gz` (≈ 330 Ko).
3. Si tu modifies le modèle : relance le script. Il affiche la liste des pièces ; si elle change,
   corrige `PIECES` et les axes (`EPAULE`, `COUDE`…, en mm Tinkercad) en haut du script.
4. Envoie `data/` dans l'ESP32 (*Upload Filesystem Image* dans PlatformIO).

## 3 bis. (Autre option) Exporter ton bras depuis Blender

Le code anime des objets **par leur nom**. Dans Blender :

1. **Un objet par pièce mobile**, nommés exactement `Base`, `Epaule`, `Coude`, `Pince` (sans espace ni accent). Tu peux changer les noms dans `const MODELE` (page.h).
2. **Origine sur l'axe de rotation** : place le curseur 3D sur l'axe du servo (`Shift+S` → *Cursor to Selected* sur un sommet de l'axe), puis *Object → Set Origin → Origin to 3D Cursor*.
3. **Hiérarchie** : `Pince` enfant de `Coude`, enfant de `Epaule`, enfant de `Base`. Sélectionne l'enfant puis le parent, `Ctrl+P` → *Object (Keep Transform)*.
4. `Ctrl+A` → **Rotation & Scale** sur chaque pièce (les rotations partent de 0).
5. *File → Export → glTF 2.0*, format **glTF Binary (.glb)**, « +Y Up » coché.
6. Teste d'abord avec le bouton **Charger .glb** de la page. Quand c'est bon, renomme-le `robot.glb`, mets-le dans `data/` et renvoie LittleFS.

Dans `page.h`, section `const MODELE` :

```js
{ objet: 'Epaule', axe: 'z', sens: 1, poseBlender: 90 },
```
- `axe` : Blender X → `'x'`, Blender **Z** → `'y'`, Blender Y → `'z'` (le glTF met Y en haut).
- `poseBlender` : l'angle du **servo** qui correspond à la pose dans laquelle tu as modélisé le bras.
- `sens: -1` si la pièce tourne à l'envers.
- `echelle: 1000` si ton modèle est aux vraies dimensions en mètres : le point rouge (bout de pince calculé) tombera alors pile au bout de la pince 3D. `'auto'` ajuste la taille approximativement.

Si des pièces ne sont pas trouvées, la page affiche la liste des noms présents dans le fichier.

## 4. Calibration (à faire une fois)

1. **Course max** : `usMin/usMax` à 500/2500 µs donnent la course complète de la plupart des servos. Si un servo grésille ou force en butée, resserre (600/2400). Pour un servo 270°, mets `course` à 270.
2. **Limites** : règle-les depuis la page (section *Limites*). Elles sont sauvegardées dans la flash de l'ESP32 et respectées partout (web, MQTT, série, IK).
3. **zero / sens** (pour les calculs X/Y/Z) :
   - Base : angle servo quand le bras pointe **droit devant** → `zero`.
   - Epaule : angle servo quand le bras est **horizontal** → `zero`.
   - Coude : angle servo quand l'avant-bras est **dans le prolongement** du bras → `zero`.
   - Si les valeurs X/Y/Z bougent dans le mauvais sens → `sens = -1`.
4. **Mesures** `H_BASE`, `L_BRAS`, `L_AVANTBRAS` d'axe à axe, en mm.
5. Vérifie : mets le bras à une position, mesure la pince à la règle, compare avec X/Y/Z affichés.

## 5. Les calculs

Repère : **X devant, Y à gauche, Z en haut**, origine au sol sous l'axe de la base.

**Directe** (angles → position) :
```
r = D + L1·cos(θ1) + L2·cos(θ1+θ2)      distance horizontale (D = D_EPAULE, décalage de l'épaule)
z = H + L1·sin(θ1) + L2·sin(θ1+θ2)
x = r·cos(θ0)     y = r·sin(θ0)
```

**Inverse** (position → angles), fonction `cinematiqueInverse()` :
```
θ0 = atan2(y, x)                                  la base vise le point
r = √(x²+y²) − D    h = z − H
cos θ2 = (r² + h² − L1² − L2²) / (2·L1·L2)        loi des cosinus
θ2 = ±acos(...)                                   − = coude en haut, + = coude en bas
θ1 = atan2(h, r) − atan2(L2·sin θ2, L1 + L2·cos θ2)
```
Si |cos θ2| > 1, le point est hors de portée. Le code essaie « coude en haut » puis « coude en bas » et refuse si un servo dépasse ses limites.

## 6. MQTT

Mets `MQTT_ACTIF = true` et l'IP du broker dans `config.h`.

| Topic | Sens | Exemple |
|-------|------|---------|
| `brasrobot/servo/0` … `/3` | → ESP32 | `90` |
| `brasrobot/angles` | → ESP32 | `90,45,120,30` |
| `brasrobot/xyz` | → ESP32 | `150,0,100` (IK) |
| `brasrobot/repos` | → ESP32 | n'importe quoi |
| `brasrobot/etat` | ← ESP32 | `{"cible":[90,90,90,60],"xyz":[150.0,0.0,185.0]}` |
| `brasrobot/erreur` | ← ESP32 | `Point hors de portee` |
| `brasrobot/dispo` | ← ESP32 | `en ligne` / `hors ligne` (retained, LWT) |

```bash
mosquitto_sub -h 192.168.1.10 -t 'brasrobot/#' -v
mosquitto_pub -h 192.168.1.10 -t brasrobot/xyz -m "150,50,120"
```
La page web se met à jour toute seule quand le bras bouge par MQTT.

## 7. API HTTP (pour scripts Python, etc.)

```
GET /etat                    état complet (JSON)
GET /set?s=1&a=45.5          servo 1 à 45,5°
GET /ik?x=150&y=0&z=100      aller à un point
GET /repos
GET /limites?s=0&min=10&max=170     /limites?reset=1
```
Moniteur série : `0 90` (servo 0 à 90°) ou `xyz 150 0 100`.

## 8. Sécurité

Par défaut (TP de base), la page web et le MQTT n'ont **aucune authentification** :
n'importe qui sur le réseau peut bouger le bras — c'est volontaire, c'est le TP
(`GUIDE_ETUDIANT.md`). Deux correctifs sont déjà implémentés et activables sans toucher
au code, via `lab.env` (voir `GUIDE_PROF.md`, section Sécurité, pour le détail) :

- **HTTP Basic Auth** (`HTTP_AUTH_ACTIF=true`) : protège les routes de contrôle
  (`/set`, `/ik`, `/repos`, `/limites`, `/trim`, `/cal`) avec `serveur.authenticate(...)`
  (voir `autoriseControle()` dans `src/main.cpp`). Les routes de lecture (`/etat`, `/pos`,
  fichiers 3D) restent ouvertes.
- **MQTT + TLS** (`TLS_ACTIF=true`) : le broker expose un listener chiffré sur le port
  **8883** en plus du 1883 en clair (certificats auto-générés par
  `outils/generer_certs_tls.ps1`). ⚠️ Le firmware ESP32 ne s'y connecte pas lui-même —
  implémenter `WiFiClientSecure` + certificat racine embarqué sur l'ESP32 est possible
  mais alourdit sensiblement le firmware (flash, RAM) et n'a pas été fait ici. Le port
  8883 sert surtout à la démo pédagogique "avant/après" depuis un PC
  (`mosquitto_pub`/`mosquitto_sub --cafile tls\ca.crt -p 8883 ...`).
- MQTT : des ACL par topic dans Mosquitto restent une amélioration possible, non
  implémentée (actuellement, un compte valide peut publier/s'abonner à tous les topics).
- **Bon exercice** : ouvre Wireshark pendant que tu bouges un curseur (port 80/1883 en
  clair) puis compare avec le trafic sur le port 8883 une fois `TLS_ACTIF=true`.
