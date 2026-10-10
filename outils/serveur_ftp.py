"""
serveur_ftp.py : FTP anonyme du lab.

Deux niveaux de journalisation, clairs et simples :

1. CÔTÉ ÉTUDIANT : un fichier VISIBLE et normal dans le partage,
   "connexions.log", qui liste les connexions (IP, date/heure, login).
   Les étudiants ont le droit de le supprimer ou de le modifier (permission
   delete+write donnée explicitement) : c'est une partie du TP (voir
   GUIDE_ETUDIANT.md) de réfléchir à l'intérêt - et aux limites - d'effacer
   ses traces sur un système qu'on vient d'attaquer.

2. CÔTÉ PROF : le journal COMPLET du protocole FTP (chaque commande, chaque
   réponse, avec IP/port/horodatage) est écrit dans outils/logs/, un
   dossier qui n'est simplement jamais exposé par le serveur FTP (ce n'est
   pas un piège, juste une séparation normale : le partage FTP = ftp_wordlists/,
   les logs internes du serveur = outils/logs/, deux choses différentes).
   Lis-le avec "outils/analyser_logs_ftp.py" pour un rendu lisible par
   session plutôt que le fichier brut.
"""
import datetime
import logging
import os
import sys
import warnings

from pyftpdlib.authorizers import DummyAuthorizer
from pyftpdlib.handlers import FTPHandler
from pyftpdlib.servers import FTPServer

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import lab_env  # noqa: E402

RACINE = lab_env.RACINE
DOSSIER_LOGS = os.path.join(RACINE, "outils", "logs")
NOM_LOG_VISIBLE = "connexions.log"


def dossier_wordlists(valeurs):
    instance = str(valeurs.get("INSTANCE_ID", "1")).strip() or "1"
    if instance == "1":
        return os.path.join(RACINE, "ftp_wordlists")
    candidat = os.path.join(RACINE, f"ftp_wordlists_{instance}")
    return candidat if os.path.isdir(candidat) else os.path.join(RACINE, "ftp_wordlists")


class FiltreBruitInterne(logging.Filter):
    """pyftpdlib logue les réponses utiles (ex: "File removed.") en DEBUG,
    il faut donc ce niveau pour les capturer côté prof - mais DEBUG inclut
    aussi tout le bruit interne du moteur (pollers, dispatchers...), préfixé
    "[debug]". On garde tout le reste : connexions, login, chaque "<-"/"->"."""

    def filter(self, record):
        return not record.getMessage().startswith("[debug]")


def preparer_journal_prof():
    """Journal complet du protocole FTP, hors du dossier servi (outils/logs/),
    pour que l'encadrant garde une trace même si le fichier visible du
    partage est supprimé/modifié par un étudiant."""
    os.makedirs(DOSSIER_LOGS, exist_ok=True)
    horodatage = datetime.datetime.now().strftime("%Y%m%d_%H%M%S")
    chemin = os.path.join(DOSSIER_LOGS, f"ftp_{horodatage}.log")

    formatteur = logging.Formatter("%(asctime)s %(levelname)s %(message)s")
    filtre = FiltreBruitInterne()

    fichier = logging.FileHandler(chemin, encoding="utf-8")
    fichier.setFormatter(formatteur)
    fichier.addFilter(filtre)

    logger = logging.getLogger("pyftpdlib")
    logger.setLevel(logging.DEBUG)
    logger.addHandler(fichier)

    console = logging.StreamHandler()
    console.setFormatter(formatteur)
    console.addFilter(filtre)
    logger.addHandler(console)

    return chemin


class GestionnaireAvecLogVisible(FTPHandler):
    """Ajoute une ligne à connexions.log (DANS le partage, donc visible et
    supprimable par les étudiants) à la connexion/déconnexion/login. Écrit
    en ouverture/fermeture immédiate du fichier (pas de handle gardé ouvert)
    pour qu'un "delete connexions.log" en cours de session fonctionne
    normalement, sans erreur de fichier verrouillé côté Windows."""

    chemin_log_visible = None  # défini dans main()

    def _ecrire_ligne_visible(self, texte):
        try:
            with open(self.chemin_log_visible, "a", encoding="utf-8") as f:
                f.write(texte + "\n")
        except OSError:
            pass  # le fichier a pu être supprimé entre deux écritures : normal.

    def on_connect(self):
        self._ecrire_ligne_visible(
            f"{datetime.datetime.now():%Y-%m-%d %H:%M:%S} - {self.remote_ip} - connexion"
        )

    def on_login(self, username):
        self._ecrire_ligne_visible(
            f"{datetime.datetime.now():%Y-%m-%d %H:%M:%S} - {self.remote_ip} - "
            f"login reussi ({username})"
        )

    def on_login_failed(self, username, password):
        self._ecrire_ligne_visible(
            f"{datetime.datetime.now():%Y-%m-%d %H:%M:%S} - {self.remote_ip} - "
            f"login echoue ({username})"
        )

    def on_disconnect(self):
        self._ecrire_ligne_visible(
            f"{datetime.datetime.now():%Y-%m-%d %H:%M:%S} - {self.remote_ip} - deconnexion"
        )


def main():
    valeurs = lab_env.charger()
    port = int(valeurs["FTP_PORT"])
    dossier_ftp = dossier_wordlists(valeurs)
    # connexions.log vit dans un sous-dossier dédié "journal/" (visible et
    # normal dans un "ls", rien de caché) : pyftpdlib n'autorise de donner
    # des permissions d'écriture/suppression que sur un DOSSIER (pas sur un
    # fichier seul, ni sur la racine du partage elle-même).
    dossier_journal = os.path.join(dossier_ftp, "journal")
    os.makedirs(dossier_journal, exist_ok=True)
    chemin_log_visible = os.path.join(dossier_journal, NOM_LOG_VISIBLE)
    if not os.path.isfile(chemin_log_visible):
        open(chemin_log_visible, "a", encoding="utf-8").close()

    chemin_journal_prof = preparer_journal_prof()

    autorisation = DummyAuthorizer()
    # Lecture seule par défaut : protège l'intégrité de users.txt/passwords.txt
    # (le TP ne doit pas pouvoir être cassé/modifié).
    autorisation.add_anonymous(dossier_ftp, perm="elr")
    # ... sauf journal/, explicitement ouvert en suppression + écriture :
    # c'est voulu, voir GUIDE_ETUDIANT.md. (le RuntimeWarning de pyftpdlib à
    # ce sujet est donc attendu, on le masque pour ne pas alarmer le prof.)
    with warnings.catch_warnings():
        warnings.simplefilter("ignore", RuntimeWarning)
        autorisation.override_perm("anonymous", dossier_journal, perm="elradfmw", recursive=True)

    gestionnaire = GestionnaireAvecLogVisible
    gestionnaire.authorizer = autorisation
    gestionnaire.chemin_log_visible = chemin_log_visible
    gestionnaire.banner = "Serveur FTP - Usine (acces invite)"

    serveur = FTPServer(("0.0.0.0", port), gestionnaire)
    print(f"[OK] FTP anonyme sur le port {port} (dossier : {dossier_ftp}).")
    print(f"[i] Log visible (etudiants, supprimable) : {chemin_log_visible}")
    print(f"[i] Journal complet (prof, hors FTP) : {chemin_journal_prof}")
    serveur.serve_forever()


if __name__ == "__main__":
    main()
