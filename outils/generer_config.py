"""
generer_config.py : régénère include/config.h à partir de
include/config.h.template + lab.env.

À lancer :
  - automatiquement par installer_lab.ps1 / start_lab.bat,
  - manuellement après avoir modifié lab.env :
        python outils/generer_config.py
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import lab_env  # noqa: E402

RACINE = lab_env.RACINE
GABARIT = os.path.join(RACINE, "include", "config.h.template")
SORTIE = os.path.join(RACINE, "include", "config.h")


def _assurer_ligne(chemin, ligne):
    """Ajoute `ligne` au fichier wordlist si elle n'y est pas déjà."""
    existantes = []
    if os.path.isfile(chemin):
        with open(chemin, "r", encoding="utf-8") as f:
            existantes = [l.rstrip("\n") for l in f]
    if ligne not in existantes:
        with open(chemin, "a", encoding="utf-8") as f:
            f.write(("\n" if existantes else "") + ligne + "\n")
        return True
    return False


def synchroniser_wordlists(valeurs):
    """IMPORTANT : l'exercice de brute-force (GUIDE_ETUDIANT.md, ex.3) suppose
    que le couple MQTT_USER/MQTT_PASS actuel se trouve dans les wordlists
    servies par le FTP anonyme. Si on change/régénère MQTT_PASS (ex: mot de
    passe aléatoire à l'installation) sans les resynchroniser, le TP devient
    insoluble. On les tient donc toujours à jour ici, automatiquement."""
    dossier = os.path.join(RACINE, "ftp_wordlists")
    if valeurs.get("INSTANCE_ID", "1") != "1":
        dossier = os.path.join(RACINE, f"ftp_wordlists_{valeurs['INSTANCE_ID']}")
        modele = os.path.join(RACINE, "ftp_wordlists")
        if not os.path.isdir(dossier) and os.path.isdir(modele):
            import shutil
            shutil.copytree(modele, dossier)

    ajout_u = _assurer_ligne(os.path.join(dossier, "users.txt"), valeurs["MQTT_USER"])
    ajout_p = _assurer_ligne(os.path.join(dossier, "passwords.txt"), valeurs["MQTT_PASS"])
    if ajout_u or ajout_p:
        print(f"[OK] Wordlists ({dossier}) resynchronisées avec les identifiants actuels.")


def generer():
    valeurs = lab_env.charger()

    if not valeurs.get("WIFI_SSID"):
        print("[!] WIFI_SSID est vide dans lab.env : l'ESP32 démarrera directement "
              "en point d'accès de secours (voir AP_SSID). Remplis lab.env pour le Wi-Fi du lieu.")

    synchroniser_wordlists(valeurs)

    with open(GABARIT, "r", encoding="utf-8") as f:
        contenu = f.read()

    for cle, valeur in valeurs.items():
        contenu = contenu.replace("{{" + cle + "}}", str(valeur))

    restants = [m for m in contenu.split("{{")[1:]]
    if restants:
        manquants = {m.split("}}")[0] for m in restants}
        raise RuntimeError(f"Placeholders non résolus dans config.h.template : {manquants}")

    with open(SORTIE, "w", encoding="utf-8") as f:
        f.write(contenu)

    print(f"[OK] include/config.h généré (poste #{valeurs['INSTANCE_ID']}, "
          f"topics '{valeurs['MQTT_BASE']}/...', broker {valeurs['MQTT_SERVEUR']}:{valeurs['MQTT_PORT']}, "
          f"mDNS {valeurs['NOM_MDNS']}.local, FTP port {valeurs['FTP_PORT']}).")
    return valeurs


if __name__ == "__main__":
    generer()
