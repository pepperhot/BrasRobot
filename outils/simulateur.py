# =====================================================================
#  simulateur.py : la page web du bras SANS l'ESP32 (pour tester la 3D)
#  Sert la page de include/page.h, les fichiers de data/ et imite les routes
#  /etat /set /ik /repos /limites avec les réglages de include/config.h.
#
#  Utilisation :  python outils/simulateur.py      puis ouvre http://localhost:8080
# =====================================================================
import math, os, re, sys
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import urlparse, parse_qs

RACINE = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..')
PORT = int(sys.argv[1]) if len(sys.argv) > 1 else 8080

page = open(os.path.join(RACINE, 'include', 'page.h'), encoding='utf-8').read()
PAGE = re.search(r'R"rawliteral\((.*)\)rawliteral"', page, re.S).group(1).encode()

cfg = open(os.path.join(RACINE, 'include', 'config.h'), encoding='utf-8').read()
geo = {k: float(re.search(rf'{n}\s*=\s*([\d.]+)', cfg).group(1))
       for k, n in (('h', 'H_BASE'), ('l1', 'L_BRAS'), ('l2', 'L_AVANTBRAS'))}
m = re.search(r'D_EPAULE\s*=\s*(-?[\d.]+)', cfg)
geo['d'] = float(m.group(1)) if m else 0.0
servos = []
for m in re.finditer(r'\{"(\w+)",\s*(\d+),\s*(\d+),\s*(\d+),\s*([\d.]+),\s*([\d.]+),\s*([\d.]+),\s*([\d.]+),\s*([\d.]+),\s*(-?\d+)\}', cfg):
    nom, pin, _, _, course, mn, mx, repos, zero, sens = m.groups()
    servos.append(dict(nom=nom, pin=int(pin), min=float(mn), max=float(mx), course=float(course),
                       repos=float(repos), zero=float(zero), sens=int(sens)))
cible = [s['repos'] for s in servos]


def borner(i, a):
    cible[i] = round(min(servos[i]['max'], max(servos[i]['min'], a)), 1)


def fk(a):
    g = lambda i: math.radians((a[i] - servos[i]['zero']) * servos[i]['sens'])
    t0, t1, t2 = g(0), g(1), g(2)
    r = geo['d'] + geo['l1'] * math.cos(t1) + geo['l2'] * math.cos(t1 + t2)
    return [r * math.cos(t0), r * math.sin(t0), geo['h'] + geo['l1'] * math.sin(t1) + geo['l2'] * math.sin(t1 + t2)]


def ik(x, y, z):   # même calcul que cinematiqueInverse() dans main.cpp
    r, h, L1, L2 = math.hypot(x, y) - geo['d'], z - geo['h'], geo['l1'], geo['l2']
    c2 = (r * r + h * h - L1 * L1 - L2 * L2) / (2 * L1 * L2)
    if abs(c2) > 1: return None, 'Point hors de portee'
    serv = lambda i, d: servos[i]['zero'] + d * servos[i]['sens']
    err = ''
    for signe in (-1, 1):
        t2 = signe * math.acos(c2)
        t1 = math.atan2(h, r) - math.atan2(L2 * math.sin(t2), L1 + L2 * math.cos(t2))
        res = [serv(0, math.degrees(math.atan2(y, x))), serv(1, math.degrees(t1)), serv(2, math.degrees(t2))]
        hors = [i for i in range(3) if not servos[i]['min'] <= res[i] <= servos[i]['max']]
        if not hors: return res, ''
        err = f"{servos[hors[0]]['nom']} hors limites ({res[hors[0]]:.1f} deg)"
    return None, err


def etat():
    import json
    return json.dumps({'cible': cible, 'xyz': [round(v, 1) for v in fk(cible)], 'mqtt': False, 'lib3d': False,
                       'geo': geo, 'servos': servos}).encode()


class Gestion(BaseHTTPRequestHandler):
    def envoyer(self, code, corps, type_='application/json', entetes=()):
        self.send_response(code)
        self.send_header('Content-Type', type_)
        for k, v in entetes: self.send_header(k, v)
        self.send_header('Content-Length', str(len(corps)))
        self.end_headers()
        self.wfile.write(corps)

    def do_GET(self):
        u = urlparse(self.path); q = {k: v[0] for k, v in parse_qs(u.query).items()}
        f = lambda k: float(q.get(k, 0))
        if u.path == '/': return self.envoyer(200, PAGE, 'text/html; charset=utf-8')
        if u.path == '/etat': return self.envoyer(200, etat())
        if u.path == '/set':
            borner(int(f('s')), f('a')); return self.envoyer(200, etat())
        if u.path == '/repos':
            for i, s in enumerate(servos): borner(i, s['repos'])
            return self.envoyer(200, etat())
        if u.path == '/ik':
            res, err = ik(f('x'), f('y'), f('z'))
            if res is None: return self.envoyer(400, err.encode(), 'text/plain')
            for i in range(3): borner(i, res[i])
            return self.envoyer(200, etat())
        if u.path == '/limites':
            if 'reset' not in q:
                i = int(f('s')); servos[i]['min'], servos[i]['max'] = f('min'), f('max')
            for i in range(len(servos)): borner(i, cible[i])
            return self.envoyer(200, etat())
        # Fichiers de data/ (comme LittleFS : x.gz servi compressé si x n'existe pas)
        chemin = os.path.normpath(os.path.join(RACINE, 'data', u.path.lstrip('/')))
        if not chemin.startswith(os.path.normpath(os.path.join(RACINE, 'data'))):
            return self.envoyer(403, b'Interdit', 'text/plain')
        type_ = 'model/gltf-binary' if u.path.endswith('.glb') else 'application/javascript' if u.path.endswith('.js') else 'application/octet-stream'
        entetes = []
        if not os.path.isfile(chemin) and os.path.isfile(chemin + '.gz'):
            chemin += '.gz'; entetes.append(('Content-Encoding', 'gzip'))
        if not os.path.isfile(chemin): return self.envoyer(404, b'Introuvable', 'text/plain')
        self.envoyer(200, open(chemin, 'rb').read(), type_, entetes)

    def log_message(self, *a):
        pass


print(f'Simulateur du bras : http://localhost:{PORT}   (Ctrl+C pour arrêter)')
ThreadingHTTPServer(('127.0.0.1', PORT), Gestion).serve_forever()
