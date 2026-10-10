# =====================================================================
#  tinkercad_vers_glb.py : rend ton export Tinkercad animable par la page web
#
#  Tinkercad fusionne tout le robot en UN seul objet : la page ne peut donc
#  rien faire bouger. Ce script :
#    1. sépare les pièces (chaque morceau qui ne touche pas les autres)
#    2. les range dans une hiérarchie Base > Epaule > Coude > Pince
#    3. place l'origine de chaque pièce sur l'axe de son servo
#    4. écrit data/robot.glb.gz (compressé, servi tel quel par l'ESP32)
#
#  Utilisation :   python outils/tinkercad_vers_glb.py
#  (lance-le sans argument pour voir la liste des pièces détectées)
#  Besoin de : python 3 + numpy   (pip install numpy)
# =====================================================================
import gzip, json, os, struct, sys
import numpy as np

ICI = os.path.dirname(os.path.abspath(__file__))
SOURCE = os.path.join(ICI, '..', 'tinkercad', 'robot_tinkercad.glb')   # export Tinkercad brut
SORTIE = os.path.join(ICI, '..', 'data', 'robot.glb.gz')

# ---------------- RÉGLAGES (coordonnées Tinkercad, en mm) ----------------
# Pièces : numéro affiché par le script (0 = la plus grosse) -> articulation.
# Chaque pièce suit l'articulation à laquelle elle est attachée.
PIECES = {
    'Socle':   [1, 6],     # fixe
    'Base':    [3],        # tourelle
    'Epaule':  [0, 7],     # bras
    'Coude':   [2, 5],     # avant-bras + poignet
    'Pince':   [4],        # mâchoire mobile
}
# Axes de rotation (lus sur les trous des servos dans Tinkercad)
AXE_BASE   = (-12.0, 0.0)      # (X, Y) de l'axe vertical de la base
EPAULE     = (-13.0, 101.5)    # (Y, Z) de l'axe de l'épaule
COUDE      = (71.0, 186.9)     # (Y, Z) de l'axe du coude
PINCE      = (157.0, 104.0)    # (Y, Z) de l'axe de la mâchoire
SOL_Z      = 0.0
# Le bras est modélisé pointant vers +Y dans Tinkercad : on le tourne pour
# qu'il pointe vers l'avant du robot (X).
# -------------------------------------------------------------------------


def lire_glb(chemin):
    d = open(chemin, 'rb').read()
    n = struct.unpack('<I', d[12:16])[0]
    j = json.loads(d[20:20 + n])
    b = d[20 + n + 8:]

    def acc(i):
        a = j['accessors'][i]
        bv = j['bufferViews'][a['bufferView']]
        k = {'SCALAR': 1, 'VEC3': 3, 'VEC4': 4}[a['type']]
        dt = {5126: np.float32, 5125: np.uint32, 5123: np.uint16, 5121: np.uint8}[a['componentType']]
        r = np.frombuffer(b, dt, a['count'] * k, bv.get('byteOffset', 0) + a.get('byteOffset', 0))
        return r.reshape(-1, k) if k > 1 else r

    if len(j['meshes']) != 1 or len(j['meshes'][0]['primitives']) != 1:
        sys.exit('Ce fichier ne ressemble pas à un export Tinkercad (un seul objet attendu).')
    p = j['meshes'][0]['primitives'][0]
    P = acc(p['attributes']['POSITION']).astype(np.float64) * 1000        # m -> mm
    C = acc(p['attributes']['COLOR_0'])[:, :3] if 'COLOR_0' in p['attributes'] else np.full((len(P), 3), 0.7)
    I = acc(p['indices']).astype(np.int64).reshape(-1, 3)
    return P, np.asarray(C, np.float64), I


def separer(P, I):
    """Composantes connexes (sommets soudés par position)."""
    _, sommet = np.unique(np.round(P * 100).astype(np.int64), axis=0, return_inverse=True)
    sommet = sommet.ravel()
    parent = np.arange(sommet.max() + 1)

    def racine(x):
        while parent[x] != x:
            parent[x] = parent[parent[x]]
            x = parent[x]
        return x
    for a, b, c in sommet[I]:
        ra = racine(a)
        parent[racine(b)] = ra
        parent[racine(c)] = ra
    comp = np.array([racine(x) for x in range(len(parent))])[sommet][I[:, 0]]
    ids = sorted(np.unique(comp), key=lambda r: -(comp == r).sum())
    return [I[comp == r] for r in ids]


def vers_three(P):
    """Tinkercad (X,Y,Z mm, Z en haut) -> three.js (Y en haut), origine sur l'axe de la base.
    Robot : x = Y_tk (devant), y = -(X_tk - axe) (gauche), z = Z_tk ; three = (x, z, -y)."""
    return np.c_[P[:, 1] - AXE_BASE[1], P[:, 2] - SOL_Z, P[:, 0] - AXE_BASE[0]]


def main():
    if not os.path.exists(SOURCE):
        sys.exit(f'Introuvable : {SOURCE}\nExporte ton robot depuis Tinkercad (Exporter > .GLB) et mets-le là.')
    P, C, I = lire_glb(SOURCE)
    morceaux = separer(P, I)
    print(f'{len(morceaux)} pièces détectées (coordonnées Tinkercad, mm) :')
    for k, t in enumerate(morceaux):
        q = P[np.unique(t)]
        print(f'  {k}: {len(t):6d} triangles   X {q[:,0].min():7.1f}..{q[:,0].max():7.1f}'
              f'   Y {q[:,1].min():7.1f}..{q[:,1].max():7.1f}   Z {q[:,2].min():7.1f}..{q[:,2].max():7.1f}')
    utilises = sorted(i for l in PIECES.values() for i in l)
    if utilises != list(range(len(morceaux))):
        sys.exit('\nPIECES ne correspond pas à la liste ci-dessus : corrige le réglage en haut du script.')

    Q = vers_three(P)
    pivot = {   # position absolue (three.js) de chaque articulation
        'Socle':  np.zeros(3),
        'Base':   np.zeros(3),
        'Epaule': np.array([EPAULE[0] - AXE_BASE[1], EPAULE[1] - SOL_Z, 0]),
        'Coude':  np.array([COUDE[0] - AXE_BASE[1], COUDE[1] - SOL_Z, 0]),
        'Pince':  np.array([PINCE[0] - AXE_BASE[1], PINCE[1] - SOL_Z, 0]),
    }
    parent = {'Socle': None, 'Base': None, 'Epaule': 'Base', 'Coude': 'Epaule', 'Pince': 'Coude'}

    binaire = bytearray(); vues = []; accs = []; meshes = []; noeuds = []

    def ajouter(arr, cible, typ, ct, norm=False, bornes=False):
        while len(binaire) % 4:
            binaire.append(0)
        vues.append({'buffer': 0, 'byteOffset': len(binaire), 'byteLength': arr.nbytes, 'target': cible})
        binaire.extend(arr.tobytes())
        a = {'bufferView': len(vues) - 1, 'componentType': ct, 'count': len(arr), 'type': typ}
        if norm: a['normalized'] = True
        if bornes: a['min'] = arr.min(0).tolist(); a['max'] = arr.max(0).tolist()
        accs.append(a)
        return len(accs) - 1

    index_noeud = {}
    for nom in ['Socle', 'Base', 'Epaule', 'Coude', 'Pince']:
        t = np.concatenate([morceaux[i] for i in PIECES[nom]])
        s = np.unique(t)
        renum = np.full(len(P), -1); renum[s] = np.arange(len(s))
        pos = (Q[s] - pivot[nom]).astype(np.float32)
        col = np.c_[np.clip(C[s] * 255 + .5, 0, 255), np.full(len(s), 255)].astype(np.uint8)
        ind = renum[t].ravel().astype(np.uint16 if len(s) < 65536 else np.uint32)
        prim = {'attributes': {'POSITION': ajouter(pos, 34962, 'VEC3', 5126, bornes=True),
                               'COLOR_0': ajouter(col, 34962, 'VEC4', 5121, norm=True)},
                'indices': ajouter(ind, 34963, 'SCALAR', 5123 if ind.dtype == np.uint16 else 5125),
                'material': 0}
        meshes.append({'name': nom, 'primitives': [prim]})
        # Un noeud par articulation (tourne autour de son origine) + le maillage dedans
        p = parent[nom]
        tr = pivot[nom] - (pivot[p] if p else 0)
        noeuds.append({'name': nom, 'translation': [float(v) for v in tr], 'mesh': len(meshes) - 1, 'children': []})
        index_noeud[nom] = len(noeuds) - 1
        if p: noeuds[index_noeud[p]]['children'].append(index_noeud[nom])
    for n in noeuds:
        if not n['children']: del n['children']
    racines = [index_noeud[n] for n in index_noeud if parent[n] is None]
    gltf = {
        'asset': {'version': '2.0', 'generator': 'BrasRobot tinkercad_vers_glb.py'},
        'scene': 0, 'scenes': [{'nodes': racines}], 'nodes': noeuds, 'meshes': meshes,
        'materials': [{'pbrMetallicRoughness': {'metallicFactor': 0.1, 'roughnessFactor': 0.7}}],
        'accessors': accs, 'bufferViews': vues, 'buffers': [{'byteLength': len(binaire)}],
    }
    js = json.dumps(gltf, separators=(',', ':')).encode()
    js += b' ' * (-len(js) % 4)
    while len(binaire) % 4:
        binaire.append(0)
    glb = (struct.pack('<4sII', b'glTF', 2, 28 + len(js) + len(binaire))
           + struct.pack('<I4s', len(js), b'JSON') + js
           + struct.pack('<I4s', len(binaire), b'BIN\0') + bytes(binaire))
    os.makedirs(os.path.dirname(SORTIE), exist_ok=True)
    with gzip.open(SORTIE, 'wb', 9) as f:
        f.write(glb)
    brut = os.path.join(os.path.dirname(SORTIE), 'robot.glb')
    if os.path.exists(brut):
        print(f'\nATTENTION : {brut} existe et serait servi à la place du .gz : supprime-le.')

    e, c = np.array(EPAULE), np.array(COUDE)
    print(f'\n-> {SORTIE}  ({len(glb)//1024} Ko, {os.path.getsize(SORTIE)//1024} Ko compressé)')
    print(f'Mesures du modèle pour config.h : H_BASE = {EPAULE[1]-SOL_Z:.1f}   L_BRAS = {np.linalg.norm(c-e):.1f}')


if __name__ == '__main__':
    main()
