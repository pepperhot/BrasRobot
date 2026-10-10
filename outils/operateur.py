"""
operateur.py : simule le fonctionnement normal de l'usine (le bras suit
un cycle), ce qui génère le trafic MQTT "légitime" que les étudiants
observeront/détourneront pendant le TP.

Identifiants et broker lus depuis lab.env (racine du projet) : rien à
modifier ici, même sur un poste multi-instance (INSTANCE_ID dans lab.env).
"""
import os
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import lab_env  # noqa: E402

MOSQ_PUB = r"C:\Program Files\mosquitto\mosquitto_pub.exe"
CYCLE = ["90,90,90,60", "120,60,110,20"]


def main():
    valeurs = lab_env.charger()
    broker = valeurs["MQTT_SERVEUR"]
    user = valeurs["MQTT_USER"]
    mdp = valeurs["MQTT_PASS"]
    topic = f"{valeurs['MQTT_BASE']}/angles"

    if not os.path.isfile(MOSQ_PUB):
        print(f"[!] mosquitto_pub introuvable ({MOSQ_PUB}). Installe Mosquitto "
              "ou corrige MOSQ_PUB dans ce script.")
        return

    print(f"Contrôleur en marche sur '{topic}' (broker {broker}) : "
          "le bras suit son cycle normal (Ctrl+C pour arrêter).")
    try:
        while True:
            for pose in CYCLE:
                subprocess.run([MOSQ_PUB, "-h", broker, "-u", user, "-P", mdp,
                                 "-t", topic, "-m", pose])
                time.sleep(5)
    except KeyboardInterrupt:
        print("\nContrôleur arrêté.")


if __name__ == "__main__":
    main()
