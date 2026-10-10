"""
lab_env.py : lecture/écriture de lab.env, la configuration centrale du lab.

Utilisé par generer_config.py, operateur.py et simulateur.py pour que
tout le lab (firmware, broker, scripts) parte de la même source de vérité.
Pur stdlib, aucune dépendance.
"""
import os
import re
import secrets
import socket
import string

RACINE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FICHIER_ENV = os.path.join(RACINE, "lab.env")
FICHIER_EXEMPLE = os.path.join(RACINE, "lab.env.example")

DEFAUTS = {
    "WIFI_SSID": "",
    "WIFI_PASS": "",
    "AP_SSID": "BrasRobot",
    "AP_PASS": "12345678",
    "MQTT_SERVEUR": "auto",
    "MQTT_PORT": "1883",
    "MQTT_USER": "admin",
    "MQTT_PASS": "",
    "NOM_MDNS": "brasrobot",
    "INSTANCE_ID": "1",
    "FTP_PORT": "21",
    # Exercice bonus "sécurisation" (désactivés par défaut, voir GUIDE_PROF.md) :
    "HTTP_AUTH_ACTIF": "false",
    "HTTP_AUTH_USER": "admin",
    "HTTP_AUTH_PASS": "",
    "TLS_ACTIF": "false",
}

# Champs secrets générés automatiquement s'ils sont vides, puis persistés dans
# lab.env pour rester stables entre deux lancements (sinon firmware/broker se
# désynchroniseraient). Clé -> nom affiché dans le message de log.
SECRETS_AUTO = {
    "MQTT_PASS": "mot de passe MQTT",
    "HTTP_AUTH_PASS": "mot de passe HTTP Basic Auth",
}


def _parse(chemin):
    valeurs = {}
    if not os.path.isfile(chemin):
        return valeurs
    with open(chemin, "r", encoding="utf-8") as f:
        for ligne in f:
            ligne = ligne.strip()
            if not ligne or ligne.startswith("#") or "=" not in ligne:
                continue
            cle, _, valeur = ligne.partition("=")
            valeurs[cle.strip()] = valeur.strip()
    return valeurs


def detecter_ip_locale():
    """IP locale du PC sur le réseau du lab (celle que l'ESP32 doit joindre)."""
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        s.connect(("8.8.8.8", 80))
        return s.getsockname()[0]
    except OSError:
        return "127.0.0.1"
    finally:
        s.close()


def generer_mot_de_passe(longueur=16):
    alphabet = string.ascii_letters + string.digits
    return "".join(secrets.choice(alphabet) for _ in range(longueur))


def charger(auto_creer=True, auto_resoudre=True):
    """Charge lab.env (le crée depuis lab.env.example si absent), complète
    les champs manquants (IP auto, mot de passe MQTT aléatoire) et
    réécrit le fichier si des valeurs ont été générées."""
    if not os.path.isfile(FICHIER_ENV):
        if auto_creer and os.path.isfile(FICHIER_EXEMPLE):
            with open(FICHIER_EXEMPLE, "r", encoding="utf-8") as src:
                contenu = src.read()
            with open(FICHIER_ENV, "w", encoding="utf-8") as dst:
                dst.write(contenu)
            print(f"[lab_env] '{os.path.basename(FICHIER_ENV)}' créé depuis l'exemple. "
                  "Remplis WIFI_SSID / WIFI_PASS puis relance.")
        else:
            raise FileNotFoundError(
                "lab.env introuvable. Copie lab.env.example en lab.env et remplis-le."
            )

    valeurs = dict(DEFAUTS)
    valeurs.update(_parse(FICHIER_ENV))

    if auto_resoudre and valeurs.get("MQTT_SERVEUR", "auto").lower() == "auto":
        valeurs["MQTT_SERVEUR"] = detecter_ip_locale()
        # on n'écrit PAS "auto" -> IP dans le fichier : l'IP est redétectée
        # à chaque lancement (elle peut changer de réseau en réseau).

    if auto_resoudre:
        for cle, libelle in SECRETS_AUTO.items():
            if not valeurs.get(cle):
                valeurs[cle] = generer_mot_de_passe()
                _ecrire_champ(cle, valeurs[cle], libelle)

    instance = str(valeurs.get("INSTANCE_ID", "1")).strip() or "1"
    suffixe = "" if instance == "1" else instance
    valeurs["MQTT_BASE"] = f"brasrobot{suffixe}"
    valeurs["NOM_MDNS"] = f"{valeurs.get('NOM_MDNS', 'brasrobot')}{suffixe}"
    valeurs["AP_SSID"] = f"{valeurs.get('AP_SSID', 'BrasRobot')}{suffixe}"
    port_ftp = str(valeurs.get("FTP_PORT", "21")).strip() or "21"
    if suffixe and port_ftp == "21":
        # port par défaut inchangé + instance > 1 -> décale automatiquement
        # le port FTP pour éviter toute collision entre postes sur le même PC.
        port_ftp = str(2100 + int(instance))
    valeurs["FTP_PORT"] = port_ftp

    return valeurs


def _ecrire_champ(cle, valeur, libelle=None):
    """Persiste un champ généré automatiquement (ex: mot de passe), pour le
    réutiliser aux lancements suivants (sinon broker/firmware/HTTP se
    désynchroniseraient)."""
    if not os.path.isfile(FICHIER_ENV):
        return
    with open(FICHIER_ENV, "r", encoding="utf-8") as f:
        lignes = f.readlines()
    trouve = False
    for i, ligne in enumerate(lignes):
        if re.match(rf"^\s*{re.escape(cle)}\s*=", ligne):
            lignes[i] = f"{cle}={valeur}\n"
            trouve = True
            break
    if not trouve:
        lignes.append(f"{cle}={valeur}\n")
    with open(FICHIER_ENV, "w", encoding="utf-8") as f:
        f.writelines(lignes)
    print(f"[lab_env] {libelle or cle} généré et enregistré dans {os.path.basename(FICHIER_ENV)}.")


if __name__ == "__main__":
    for cle, val in charger().items():
        print(f"{cle}={val}")
