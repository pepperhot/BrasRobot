"""
analyser_logs_ftp.py : transforme le journal complet du protocole FTP
(outils/logs/*.log, écrit par outils/serveur_ftp.py) en rapport lisible par
SESSION (= une connexion jusqu'à la déconnexion), pour l'examinateur.

Ce journal est indépendant de "connexions.log" (le fichier visible dans le
partage que les étudiants peuvent supprimer/modifier, voir GUIDE_ETUDIANT.md) :
même si un étudiant supprime ou modifie celui-ci, ce journal-ci garde la
trace complète de tout ce qui s'est passé au niveau du protocole.

Usage :
    python outils/analyser_logs_ftp.py                     # tout le monde
    python outils/analyser_logs_ftp.py --ip 192.168.1.50     # un seul étudiant
    python outils/analyser_logs_ftp.py --a-touche-journal    # qui a modifié/
                                                               # supprimé connexions.log
"""
import argparse
import glob
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import lab_env  # noqa: E402

DOSSIER_LOGS = os.path.join(lab_env.RACINE, "outils", "logs")

# Exemple de ligne : "2026-10-04 16:27:32,546 INFO 192.168.1.65:49159-[anonymous] ..."
RE_LIGNE = re.compile(
    r"^(?P<ts>\S+ \S+) (?P<niveau>\w+) "
    r"(?P<ip>[\d.]+):(?P<port>\d+)-\[(?P<user>[^\]]*)\] (?P<msg>.*)$"
)

NOM_LOG_VISIBLE = "connexions.log"


class Session:
    def __init__(self, ip, port):
        self.ip = ip
        self.port = port
        self.debut = None
        self.fin = None
        self.utilisateur = None
        self.commandes = []       # (ts, commande, argument)
        self.a_touche_journal_visible = False

    def ajouter(self, ts, msg):
        if "FTP session opened" in msg:
            self.debut = ts
        elif "FTP session closed" in msg:
            self.fin = ts
        elif msg.startswith("USER '") and "logged in" in msg:
            self.utilisateur = msg.split("'")[1]
        elif msg.startswith("<- "):
            partie = msg[3:].strip()
            cmd, _, arg = partie.partition(" ")
            self.commandes.append((ts, cmd, arg))
            if cmd in ("DELE", "STOR", "APPE", "RNFR", "RNTO") and NOM_LOG_VISIBLE in arg:
                self.a_touche_journal_visible = True

    def resume_commandes(self):
        pertinentes = {"USER", "CWD", "RETR", "STOR", "APPE", "DELE", "RNFR", "RNTO", "MKD", "RMD"}
        return [(ts, c, a) for ts, c, a in self.commandes if c in pertinentes]


def charger_sessions(fichiers):
    sessions = {}
    ordre = []
    for chemin in fichiers:
        with open(chemin, "r", encoding="utf-8", errors="replace") as f:
            for ligne in f:
                m = RE_LIGNE.match(ligne.strip())
                if not m:
                    continue
                cle = (m["ip"], m["port"])
                if cle not in sessions:
                    sessions[cle] = Session(m["ip"], m["port"])
                    ordre.append(cle)
                sessions[cle].ajouter(m["ts"], m["msg"])
    return [sessions[c] for c in ordre]


def afficher(session):
    # Pas d'emoji : certaines consoles Windows (codepage cp1252/cp850) ne
    # savent pas l'afficher et font planter le print() avec UnicodeEncodeError.
    marque = " [!] a modifie/supprime connexions.log" if session.a_touche_journal_visible else ""
    print(f"\n=== Session {session.ip}:{session.port} [{session.utilisateur or '?'}]{marque} ===")
    print(f"    debut : {session.debut}   fin : {session.fin}")
    for ts, cmd, arg in session.resume_commandes():
        print(f"    {ts}  {cmd} {arg}")


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--ip", help="N'afficher que les sessions venant de cette IP.")
    p.add_argument("--a-touche-journal", action="store_true",
                    help="N'afficher que les sessions ayant supprimé/modifié connexions.log.")
    p.add_argument("--fichier", help="N'analyser qu'un seul fichier de log précis "
                                      "(par défaut : tous les outils/logs/ftp_*.log).")
    args = p.parse_args()

    if args.fichier:
        fichiers = [args.fichier]
    else:
        fichiers = sorted(glob.glob(os.path.join(DOSSIER_LOGS, "ftp_*.log")))

    if not fichiers:
        print(f"[!] Aucun journal trouve dans {DOSSIER_LOGS}.")
        return

    sessions = charger_sessions(fichiers)

    if args.ip:
        sessions = [s for s in sessions if s.ip == args.ip]
    if args.a_touche_journal:
        sessions = [s for s in sessions if s.a_touche_journal_visible]

    if not sessions:
        print("[i] Aucune session ne correspond aux filtres demandes.")
        return

    print(f"{len(sessions)} session(s) trouvee(s) dans {len(fichiers)} fichier(s) de journal.")
    for s in sessions:
        afficher(s)


if __name__ == "__main__":
    main()
