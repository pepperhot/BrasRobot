// =====================================================================
//  page.h : la page web (HTML + CSS + JavaScript) stockée dans l'ESP32
//  Le réglage du modèle Blender est dans "const MODELE" plus bas.
// =====================================================================
#pragma once

const char PAGE_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="fr">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Bras Robot</title>
<style>
:root{--bg:#0f1318;--panel:#171c23;--txt:#e6e9ee;--mut:#8a94a3;--acc:#3fb6a8;--bord:#262d37;--err:#e0736b}
*{box-sizing:border-box}
body{margin:0;font-family:system-ui,-apple-system,sans-serif;background:var(--bg);color:var(--txt);font-size:14px}
header{height:50px;padding:0 16px;display:flex;justify-content:space-between;align-items:center;border-bottom:1px solid var(--bord)}
header h1{font-size:16px;margin:0}
main{display:grid;grid-template-columns:1fr 390px;height:calc(100vh - 50px)}
#vue{position:relative;min-height:300px;overflow:hidden}
#vue canvas{display:block}
#info3d{position:absolute;left:10px;bottom:10px;right:10px;font-size:12px;color:var(--mut);pointer-events:none}
#outils3d{position:absolute;top:10px;left:10px;display:flex;gap:6px}
aside{overflow:auto;padding:12px;border-left:1px solid var(--bord)}
@media(max-width:820px){main{grid-template-columns:1fr;height:auto}#vue{height:55vh}aside{border-left:0}}
section{background:var(--panel);border:1px solid var(--bord);border-radius:8px;padding:12px;margin-bottom:12px}
h2{font-size:12px;text-transform:uppercase;letter-spacing:.06em;color:var(--mut);margin:0 0 10px;display:flex;justify-content:space-between}
.servo{margin-bottom:14px}
.ligne{display:flex;align-items:center;gap:6px}
.ligne label{flex:1;font-weight:600}
.ligne small{color:var(--mut);font-weight:400}
input[type=range]{width:100%;margin:6px 0 2px;accent-color:var(--acc)}
input[type=number],select{width:72px;background:#0b0e12;color:var(--txt);border:1px solid var(--bord);border-radius:4px;padding:4px}
button,.btn{background:#232a34;color:var(--txt);border:1px solid var(--bord);border-radius:4px;padding:5px 10px;cursor:pointer;font:inherit}
button:hover,.btn:hover{border-color:var(--acc)}
button.p{background:var(--acc);color:#04201c;border-color:var(--acc);font-weight:600}
.lim{font-size:11px;color:var(--mut);display:flex;justify-content:space-between}
.grille{display:grid;grid-template-columns:repeat(3,1fr);gap:6px}
.xyz{font-family:ui-monospace,monospace;font-size:15px;margin-bottom:10px}
table{width:100%;border-collapse:collapse}
td{padding:3px 2px}
#msg{font-size:12px;min-height:16px}
.pastille{font-size:12px;padding:2px 8px;border-radius:10px;background:#232a34;color:var(--mut)}
.pastille.ok{background:#1d3b36;color:var(--acc)}
</style>
</head>
<body>
<header>
  <h1>Bras Robot</h1>
  <span><span id="msg"></span> <span id="mqtt" class="pastille">MQTT</span></span>
</header>
<main>
  <div id="vue">
    <div id="outils3d">
      <label class="btn">Charger .glb<input type="file" id="fichier" accept=".glb" hidden></label>
      <button id="vueReset">Recentrer</button>
    </div>
    <div id="info3d">Chargement de la 3D...</div>
  </div>
  <aside>
    <section>
      <h2>Articulations <button id="repos">Repos</button></h2>
      <div id="servos"></div>
    </section>

    <section>
      <h2>Position de la pince (mm)</h2>
      <div class="xyz" id="xyz">X — Y — Z —</div>
      <div class="ligne" style="margin-bottom:8px">
        X <input type="number" id="ix"> Y <input type="number" id="iy"> Z <input type="number" id="iz">
        <button class="p" id="aller">Aller</button>
      </div>
      <div class="ligne" style="margin-bottom:6px">Pas
        <select id="pas"><option>1</option><option>5</option><option selected>10</option><option>20</option><option>50</option></select> mm
      </div>
      <div class="grille">
        <button data-j="x,1">X +</button><button data-j="y,1">Y +</button><button data-j="z,1">Z +</button>
        <button data-j="x,-1">X −</button><button data-j="y,-1">Y −</button><button data-j="z,-1">Z −</button>
      </div>
    </section>

    <section>
      <h2>Limites des servos (°)</h2>
      <table id="limites"></table>
      <div class="ligne" style="margin-top:8px">
        <button class="p" id="limSave">Enregistrer</button>
        <button id="limReset">Valeurs de config.h</button>
      </div>
    </section>
  </aside>
</main>

<!-- =========== INTERFACE (marche même sans la 3D) =========== -->
<script>
const $ = id => document.getElementById(id);
const B = window.bras = { etat: null, cible: [], pret: false };  // partagé avec la 3D
let glisse = -1;                  // curseur en cours de manipulation
const dernierEnvoi = [], minuteur = [];

function msg(t, err) { $('msg').textContent = t; $('msg').style.color = err ? 'var(--err)' : 'var(--mut)'; }
async function api(url) {
  const r = await fetch(url);
  const t = await r.text();
  if (!r.ok) throw new Error(t);
  return t;
}

// ---- Cinématique directe (même calcul que dans l'ESP32) ----
B.geo = (i, a) => (a - B.etat.servos[i].zero) * B.etat.servos[i].sens;
B.fk = a => {
  const g = B.etat.geo, rad = d => d * Math.PI / 180;
  const t0 = rad(B.geo(0, a[0])), t1 = rad(B.geo(1, a[1])), t2 = rad(B.geo(2, a[2]));
  const r = (g.d || 0) + g.l1 * Math.cos(t1) + g.l2 * Math.cos(t1 + t2);
  return { x: r * Math.cos(t0), y: r * Math.sin(t0), z: g.h + g.l1 * Math.sin(t1) + g.l2 * Math.sin(t1 + t2) };
};

// ---- Curseurs ----
function construire() {
  $('servos').innerHTML = '';
  B.etat.servos.forEach((s, i) => {
    const d = document.createElement('div');
    d.className = 'servo';
    d.innerHTML = `<div class="ligne"><label>${s.nom} <small>GPIO ${s.pin}</small></label>
      <button data-d="-1">−</button><input type="number" step="0.5" id="n${i}"><button data-d="1">+</button></div>
      <input type="range" step="0.5" id="r${i}">
      <div class="lim"><span id="mi${i}"></span><span id="ma${i}"></span></div>`;
    $('servos').appendChild(d);
    const r = $('r' + i);
    r.oninput = () => { glisse = i; changer(i, +r.value); };
    r.onchange = () => { glisse = -1; envoyer(i, true); };
    $('n' + i).onchange = e => changer(i, +e.target.value, true);
    d.querySelectorAll('button').forEach(b => b.onclick = () => changer(i, B.cible[i] + (+b.dataset.d), true));
  });
  $('limites').innerHTML = '<tr><td></td><td>min</td><td>max</td></tr>' + B.etat.servos.map((s, i) =>
    `<tr><td>${s.nom}</td><td><input type="number" step="1" id="lmin${i}"></td><td><input type="number" step="1" id="lmax${i}"></td></tr>`).join('');
  majLimites();
}

function majLimites() {
  B.etat.servos.forEach((s, i) => {
    const r = $('r' + i), n = $('n' + i);
    r.min = n.min = s.min; r.max = n.max = s.max;
    $('mi' + i).textContent = s.min + '°';
    $('ma' + i).textContent = s.max + '°';
    $('lmin' + i).value = s.min; $('lmax' + i).value = s.max;
    $('lmin' + i).max = $('lmax' + i).max = s.course;
  });
}

function majCurseur(i, v) { B.cible[i] = v; $('r' + i).value = v; $('n' + i).value = v; }

function changer(i, v, immediat) {
  const s = B.etat.servos[i];
  v = Math.round(Math.min(s.max, Math.max(s.min, v)) * 10) / 10;
  majCurseur(i, v);
  majXYZ();
  envoyer(i, immediat);
}

// Envoi limité à ~20 par seconde pendant qu'on glisse
function envoyer(i, force) {
  clearTimeout(minuteur[i]);
  const t = performance.now();
  if (force || t - (dernierEnvoi[i] || 0) > 50) {
    dernierEnvoi[i] = t;
    api(`/set?s=${i}&a=${B.cible[i]}`).catch(e => msg(e.message, true));
  } else minuteur[i] = setTimeout(() => envoyer(i, true), 50);
}

function majXYZ() {
  const p = B.fk(B.cible);
  $('xyz').textContent = `X ${p.x.toFixed(1)}   Y ${p.y.toFixed(1)}   Z ${p.z.toFixed(1)}`;
}

// Applique un état reçu de l'ESP32 (sans écraser un curseur qu'on manipule)
function appliquer(e, forcer) {
  const servos = B.etat ? B.etat.servos : null;
  B.etat = e;
  e.cible.forEach((v, i) => {
    if (forcer || (i !== glisse && performance.now() - (dernierEnvoi[i] || 0) > 600)) majCurseur(i, v);
  });
  if (servos && JSON.stringify(servos) !== JSON.stringify(e.servos)) majLimites();
  $('mqtt').className = 'pastille' + (e.mqtt ? ' ok' : '');
  majXYZ();
}

// ---- Cinématique inverse ----
async function allerXYZ(x, y, z) {
  try {
    appliquer(JSON.parse(await api(`/ik?x=${x.toFixed(1)}&y=${y.toFixed(1)}&z=${z.toFixed(1)}`)), true);
    msg('');
  } catch (e) { msg(e.message, true); }
}
$('aller').onclick = () => allerXYZ(+$('ix').value, +$('iy').value, +$('iz').value);
document.querySelectorAll('[data-j]').forEach(b => b.onclick = () => {
  const [axe, s] = b.dataset.j.split(','), p = B.fk(B.cible);
  p[axe] += s * +$('pas').value;
  allerXYZ(p.x, p.y, p.z);
});

// ---- Boutons ----
$('repos').onclick = async () => appliquer(JSON.parse(await api('/repos')), true);
$('limSave').onclick = async () => {
  try {
    for (let i = 0; i < B.etat.servos.length; i++)
      appliquer(JSON.parse(await api(`/limites?s=${i}&min=${$('lmin' + i).value}&max=${$('lmax' + i).value}`)), true);
    majLimites(); msg('Limites enregistrées');
  } catch (e) { msg(e.message, true); }
};
$('limReset').onclick = async () => { appliquer(JSON.parse(await api('/limites?reset=1')), true); majLimites(); };

// ---- Synchronisation (changements venant de MQTT, du port série...) ----
// Interrogation LENTE (état complet : pastille MQTT, limites) toutes les secondes.
async function sonder() {
  try { appliquer(JSON.parse(await api('/etat'))); }
  catch (e) { msg('ESP32 injoignable', true); }
  setTimeout(sonder, 1000);
}
// Interrogation RAPIDE (juste les angles) pour une 3D réactive et fluide.
async function sonderPos() {
  try {
    JSON.parse(await api('/pos')).forEach((v, i) => {
      if (i !== glisse && performance.now() - (dernierEnvoi[i] || 0) > 600) majCurseur(i, v);
    });
    majXYZ();
  } catch (e) {}
  setTimeout(sonderPos, 120);
}

// ---- Démarrage ----
(async () => {
  while (!B.etat) {
    try { B.etat = JSON.parse(await api('/etat')); } catch (e) { msg('Connexion...', true); await new Promise(r => setTimeout(r, 1000)); }
  }
  construire();
  appliquer(B.etat, true);
  const p = B.fk(B.cible);
  $('ix').value = p.x.toFixed(0); $('iy').value = p.y.toFixed(0); $('iz').value = p.z.toFixed(0);
  B.pret = true;
  sonder();
  sonderPos();

  // three.js depuis l'ESP32 (LittleFS) si présent, sinon depuis internet
  const base = B.etat.lib3d ? '/lib/' : 'https://cdn.jsdelivr.net/npm/three@0.169.0/';
  const im = document.createElement('script');
  im.type = 'importmap';
  im.textContent = JSON.stringify({ imports: {
    'three': base + 'build/three.module.min.js',
    'three/addons/': base + 'examples/jsm/' } });
  document.head.appendChild(im);
  const s3d = document.createElement('script');
  s3d.type = 'module';
  s3d.textContent = $('code3d').textContent;
  document.body.appendChild(s3d);
  setTimeout(() => { if (!B.vue3d) $('info3d').textContent = '3D indisponible (three.js introuvable). Les curseurs fonctionnent quand même.'; }, 8000);
})();
</script>

<!-- =========== VUE 3D (three.js) =========== -->
<script type="text/plain" id="code3d">
import * as THREE from 'three';
import { OrbitControls } from 'three/addons/controls/OrbitControls.js';
import { GLTFLoader } from 'three/addons/loaders/GLTFLoader.js';

// ================== RÉGLAGE DE TON MODÈLE 3D ==================
// robot.glb est produit par outils/tinkercad_vers_glb.py depuis ton export Tinkercad.
// objet       : nom EXACT de l'objet dans le .glb (sans espace ni accent)
// axe         : axe de rotation dans three.js ('y' = vertical, 'z' = épaule/coude)
// sens        : 1 ou -1 si la pièce tourne à l'envers
// poseBlender : angle du SERVO qui correspond à la pose modélisée (Tinkercad ou Blender)
// echelle     : 'auto' ou nombre (1 si le modèle est en mm, 1000 s'il est en mètres)
const MODELE = {
  fichier: '/robot.glb',
  echelle: 1,
  pieces: [
    { objet: 'Base',   axe: 'y', sens: 1, poseBlender: 90 },
    { objet: 'Epaule', axe: 'z', sens: 1, poseBlender: 45.5 },   // bras modélisé à 45,5° au-dessus de l'horizontale
    { objet: 'Coude',  axe: 'z', sens: 1, poseBlender: 93 },     // bout de pince à 87° sous le bras
    { objet: 'Pince',  axe: 'z', sens: 1, poseBlender: 60 },
  ]
};
// ===================================================================

const B = window.bras, vue = document.getElementById('vue'), info = document.getElementById('info3d');
B.vue3d = true;

// Scène (unités : mm ; three.js a Y vers le haut -> robot (x,y,z) = three (x, z, -y))
const scene = new THREE.Scene();
scene.background = new THREE.Color(0x0f1318);
const camera = new THREE.PerspectiveCamera(45, 1, 1, 20000);
const renderer = new THREE.WebGLRenderer({ antialias: true });
renderer.setPixelRatio(window.devicePixelRatio);
vue.prepend(renderer.domElement);
const controls = new OrbitControls(camera, renderer.domElement);
controls.enableDamping = true;
function recentrer() {
  const g = B.etat.geo, t = g.h + g.l1 + g.l2;
  camera.position.set(t * 1.1, t * 0.9, t * 1.1);
  controls.target.set(0, g.h, 0);
}
document.getElementById('vueReset').onclick = recentrer;
new ResizeObserver(() => {
  renderer.setSize(vue.clientWidth, vue.clientHeight);
  camera.aspect = vue.clientWidth / vue.clientHeight;
  camera.updateProjectionMatrix();
}).observe(vue);

scene.add(new THREE.HemisphereLight(0xffffff, 0x334455, 1.4));
const soleil = new THREE.DirectionalLight(0xffffff, 1.6);
soleil.position.set(300, 600, 200);
scene.add(soleil);
scene.add(new THREE.GridHelper(800, 32, 0x33404d, 0x1c232c));
scene.add(new THREE.AxesHelper(60));   // rouge = X (avant du robot)

// Point rouge = bout de pince calculé par la cinématique (utile pour vérifier tes mesures)
const pointe = new THREE.Mesh(new THREE.SphereGeometry(5, 16, 12), new THREE.MeshBasicMaterial({ color: 0xff5040 }));
scene.add(pointe);

// ---------- Modèle simplifié (utilisé tant qu'aucun .glb n'est chargé) ----------
function brasSimple(g) {
  const mat = c => new THREE.MeshStandardMaterial({ color: c, metalness: 0.3, roughness: 0.55 });
  const boite = (x, y, z, c) => new THREE.Mesh(new THREE.BoxGeometry(x, y, z), mat(c));
  const articulation = (r, l) => { const m = new THREE.Mesh(new THREE.CylinderGeometry(r, r, l, 24), mat(0x3fb6a8)); m.rotation.x = Math.PI / 2; return m; };

  const racine = new THREE.Group();
  const socle = new THREE.Mesh(new THREE.CylinderGeometry(55, 65, 16, 32), mat(0x2b3440));
  socle.position.y = 8; racine.add(socle);

  const base = new THREE.Group(); racine.add(base);                         // rotation Y
  const col = boite(40, g.h - 16, 40, 0x3a4552); col.position.y = 16 + (g.h - 16) / 2; base.add(col);
  const epaule = new THREE.Group(); epaule.position.set(g.d || 0, g.h, 0); base.add(epaule);   // rotation Z
  epaule.add(articulation(18, 50));
  const bras = boite(g.l1, 24, 26, 0xd9dde3); bras.position.x = g.l1 / 2; epaule.add(bras);
  const coude = new THREE.Group(); coude.position.x = g.l1; epaule.add(coude);   // rotation Z
  coude.add(articulation(15, 44));
  const lp = Math.min(40, g.l2 * 0.4);                                           // longueur pince
  const avb = boite(g.l2 - lp, 20, 22, 0xd9dde3); avb.position.x = (g.l2 - lp) / 2; coude.add(avb);
  const pince = new THREE.Group(); pince.position.x = g.l2 - lp; coude.add(pince);
  const doigts = [1, -1].map(s => { const d = boite(lp, 10, 5, 0xf0a13a); d.position.x = lp / 2; pince.add(d); return d; });

  return {
    racine,
    maj(a) {
      base.rotation.y   = THREE.MathUtils.degToRad(B.geo(0, a[0]));
      epaule.rotation.z = THREE.MathUtils.degToRad(B.geo(1, a[1]));
      coude.rotation.z  = THREE.MathUtils.degToRad(B.geo(2, a[2]));
      const s = B.etat.servos[3], o = (a[3] - s.min) / Math.max(1, s.max - s.min);
      doigts[0].position.z = 4 + o * 18; doigts[1].position.z = -4 - o * 18;
    }
  };
}

// ---------- Modèle Blender (.glb) ----------
let modele = null;
function chargerGLB(url, auDemarrage, nom) {
  new GLTFLoader().load(url, gltf => {
    const m = gltf.scene;
    const g = B.etat.geo;
    if (MODELE.echelle === 'auto') {
      const t = new THREE.Box3().setFromObject(m).getSize(new THREE.Vector3());
      m.scale.setScalar((g.h + Math.max(g.l1, g.l2)) / Math.max(t.x, t.y, t.z));
    } else m.scale.setScalar(MODELE.echelle);

    const pieces = MODELE.pieces.map(p => {
      const o = m.getObjectByName(p.objet);
      return o ? { ...p, o, r0: o.rotation[p.axe] } : null;
    });
    const trouvees = pieces.filter(Boolean).length;
    const noms = []; m.traverse(o => { if (o.name && o !== m) noms.push(o.name); });

    if (modele) scene.remove(modele.racine);
    modele = {
      racine: m,
      maj(a) {
        pieces.forEach((p, i) => {
          if (p) p.o.rotation[p.axe] = p.r0 + THREE.MathUtils.degToRad(p.sens * (a[i] - p.poseBlender));
        });
      }
    };
    scene.add(m);
    info.textContent = `Modèle ${nom || url} : ${trouvees}/${pieces.length} pièces animées` +
      (trouvees < pieces.length ? ' — objets trouvés : ' + noms.join(', ') : '');
  }, undefined, () => {
    info.textContent = auDemarrage ? 'Modèle simplifié (pas de robot.glb dans l\'ESP32 : envoie le dossier data/ avec LittleFS).'
                                   : 'Impossible de lire ce fichier .glb';
  });
}
document.getElementById('fichier').onchange = e => {
  const f = e.target.files[0];
  if (f) chargerGLB(URL.createObjectURL(f), false, f.name);
};

// ---------- Démarrage + boucle d'animation ----------
const affiche = B.cible.slice();
modele = brasSimple(B.etat.geo);
scene.add(modele.racine);
recentrer();
info.textContent = 'Modèle simplifié';
chargerGLB(MODELE.fichier, true);

function boucle() {
  requestAnimationFrame(boucle);
  B.cible.forEach((v, i) => affiche[i] += (v - affiche[i]) * 0.25);   // mouvement fluide (plus réactif)
  modele.maj(affiche);
  const p = B.fk(affiche);
  pointe.position.set(p.x, p.z, -p.y);
  controls.update();
  renderer.render(scene, camera);
}
boucle();
</script>
</body>
</html>
)rawliteral";
