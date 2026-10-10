#include <Arduino.h>   // ajouté automatiquement par l IDE Arduino, obligatoire en .cpp
/* =====================================================================
   BRAS ROBOT ESP32 : interface web 3D + curseurs + cinématique + MQTT
   Carte : "ESP32 Dev Module"
   Bibliothèques (Gestionnaire de bibliothèques) :
     - ESP32Servo   (Kevin Harrington)
     - PubSubClient (Nick O'Leary)
   Réglages : config.h  |  Page web : page.h  |  Fichiers 3D : data/
   ===================================================================== */
#include <WiFi.h>
#include <ESPmDNS.h>
#include <WebServer.h>
#include <LittleFS.h>
#include <Preferences.h>
#include <ESP32Servo.h>
#include <PubSubClient.h>
#include <ModbusIP_ESP8266.h>   // esclave Modbus TCP (pilotage par un automate / SCADA)
#include "config.h"
#include "page.h"

Servo servo[NB_SERVOS];
float angleActuel[NB_SERVOS];   // position réellement envoyée (lissée)
float angleCible[NB_SERVOS];    // position demandée
float limMin[NB_SERVOS];        // limites logicielles (sauvegardées en flash)
float limMax[NB_SERVOS];
float offsetServo[NB_SERVOS];   // décalage de calibration par servo (sauvegardé en flash)

WebServer serveur(80);
WiFiClient clientWifi;
PubSubClient mqtt(clientWifi);
Preferences prefs;
bool etatChange = true;         // pour publier l'état MQTT seulement si besoin

// --- Modbus TCP (port 502) : l'ESP32 est un ESCLAVE, l'automate est le MAITRE ---
// Plan de registres (voir README section "Lab ICS") :
//   Holding 0..3  : consigne d'angle des servos, en dixièmes de degré (900 = 90.0°)
//   Holding 10    : commande (1 = aller en position repos, remis à 0 après exécution)
//   Input   0..3  : angle réel courant (×10)
//   Input   4..6  : position X, Y, Z de la pince en mm (signés)
ModbusIP mb;
const uint16_t MB_HR_ANGLE   = 0;    // holding : consignes d'angle (0..3), ×10
const uint16_t MB_HR_VITESSE = 4;    // holding : vitesse (°/s), modifiable à chaud
const uint16_t MB_HR_CMD     = 10;   // holding : commande (1 = repos)
const uint16_t MB_HR_SPOOF   = 12;   // holding : 1 = renvoie une position FIGÉE à la supervision
                                     //           (démo "l'IHM ment" / false data injection)
const uint16_t MB_IR_ANGLE   = 0;    // input  : angle réel (0..3), ×10
const uint16_t MB_IR_XYZ     = 4;    // input  : X,Y,Z (4..6), mm
const uint16_t MB_IR_CPT     = 7;    // input  : compteur de commandes appliquées (télémétrie)
const uint16_t MB_IR_UPTIME  = 8;    // input  : secondes depuis le démarrage
const uint16_t MB_IR_CHARGE  = 9;    // input  : charge simulée 0..100 % (valeur "procédé" pour l'IHM)

float    vitesseServo = VITESSE_DEG_S;      // vitesse courante (pilotable en Modbus)
uint16_t mbDerniereConsigne[NB_SERVOS];     // pour détecter qui a changé (Modbus vs web)
bool     mbSpoof = false;                    // mode leurre actif ?
uint16_t mbSpoofAngle[NB_SERVOS];            // valeurs figées renvoyées à la supervision
int16_t  mbSpoofXYZ[3];
uint32_t mbCompteur = 0;                      // nb de commandes Modbus appliquées

// =====================================================================
// 1. SERVOS
// =====================================================================
// Angle (float, précision 0.1°) -> impulsion en microsecondes
void ecrireServo(int i, float angle) {
  const ServoConfig& c = SERVOS[i];
  float a = angle + offsetServo[i];                    // décalage de calibration (aligne vrai moteur / site)
  if (c.inverse) a = c.course - a;                     // moteur monté à l'envers
  int us = c.usMin + (a / c.course) * (c.usMax - c.usMin);
  us = constrain(us, c.usMin, c.usMax);                // sécurité : reste dans la plage du servo
  servo[i].writeMicroseconds(us);
}

// Toute commande passe par ici : les limites sont toujours respectées
void definirCible(int i, float angle) {
  if (i < 0 || i >= NB_SERVOS) return;
  angleCible[i] = constrain(angle, limMin[i], limMax[i]);
  etatChange = true;
}

void allerRepos() {
  for (int i = 0; i < NB_SERVOS; i++) definirCible(i, SERVOS[i].repos);
}

// Appelé en boucle : rapproche chaque servo de sa cible à VITESSE_DEG_S
void majServos() {
  static unsigned long dernier = 0;
  unsigned long maintenant = millis();
  if (maintenant - dernier < 10) return;             // toutes les 10 ms
  float dt = fminf((maintenant - dernier) / 1000.0f, 0.1f);
  dernier = maintenant;

  for (int i = 0; i < NB_SERVOS; i++) {
    float ecart = angleCible[i] - angleActuel[i];
    if (ecart == 0) continue;
    float pas = vitesseServo * dt;                    // vitesse modifiable en Modbus
    if (vitesseServo <= 0 || fabsf(ecart) <= pas) angleActuel[i] = angleCible[i];
    else angleActuel[i] += (ecart > 0 ? pas : -pas);
    ecrireServo(i, angleActuel[i]);
  }
}

// =====================================================================
// 2. CINÉMATIQUE  (repère : X devant, Y à gauche, Z en haut, mm)
// =====================================================================
float versGeo(int i, float angleServo) { return (angleServo - SERVOS[i].zero) * SERVOS[i].sens; }
float versServo(int i, float angleGeo) { return SERVOS[i].zero + angleGeo * SERVOS[i].sens; }

// Directe : angles -> position du bout de la pince
void cinematiqueDirecte(const float a[], float& x, float& y, float& z) {
  float t0 = radians(versGeo(0, a[0]));   // base
  float t1 = radians(versGeo(1, a[1]));   // épaule
  float t2 = radians(versGeo(2, a[2]));   // coude
  float r = D_EPAULE + L_BRAS * cosf(t1) + L_AVANTBRAS * cosf(t1 + t2);   // distance horizontale
  z = H_BASE + L_BRAS * sinf(t1) + L_AVANTBRAS * sinf(t1 + t2);
  x = r * cosf(t0);
  y = r * sinf(t0);
}

// Inverse : position voulue -> angles des 3 premiers servos
// Essaie "coude en haut" puis "coude en bas". Renvoie false si impossible.
bool cinematiqueInverse(float x, float y, float z, float res[3], String& erreur) {
  float r = sqrtf(x * x + y * y) - D_EPAULE; // distance au sol depuis l'axe de l'épaule
  float h = z - H_BASE;                      // hauteur au-dessus de l'épaule
  // Loi des cosinus dans le triangle épaule-coude-pince
  float c2 = (r * r + h * h - L_BRAS * L_BRAS - L_AVANTBRAS * L_AVANTBRAS)
             / (2 * L_BRAS * L_AVANTBRAS);
  if (c2 < -1 || c2 > 1) { erreur = "Point hors de portee"; return false; }

  float t0 = degrees(atan2f(y, x));
  for (int essai = 0; essai < 2; essai++) {
    float t2 = (essai == 0 ? -1 : 1) * acosf(c2);
    float t1 = atan2f(h, r) - atan2f(L_AVANTBRAS * sinf(t2), L_BRAS + L_AVANTBRAS * cosf(t2));
    res[0] = versServo(0, t0);
    res[1] = versServo(1, degrees(t1));
    res[2] = versServo(2, degrees(t2));
    bool ok = true;
    for (int i = 0; i < 3; i++) {
      if (res[i] < limMin[i] || res[i] > limMax[i]) {
        ok = false;
        erreur = String(SERVOS[i].nom) + " hors limites (" + String(res[i], 1) + " deg)";
        break;
      }
    }
    if (ok) return true;
  }
  return false;
}

// =====================================================================
// 3. ÉTAT EN JSON (pour la page web et MQTT)
// =====================================================================
String etatJSON(bool complet) {
  float x, y, z;
  cinematiqueDirecte(angleCible, x, y, z);
  String j = "{\"cible\":[";
  for (int i = 0; i < NB_SERVOS; i++) { if (i) j += ","; j += String(angleCible[i], 1); }
  j += "],\"xyz\":[";
  j += String(x, 1); j += ","; j += String(y, 1); j += ","; j += String(z, 1);
  j += "]";
  if (complet) {
    j += ",\"mqtt\":";   j += (mqtt.connected() ? "true" : "false");
    j += ",\"lib3d\":";  j += (LittleFS.exists("/lib/build/three.module.min.js.gz") ? "true" : "false");
    j += ",\"geo\":{\"h\":"; j += String(H_BASE, 1);
    j += ",\"l1\":";         j += String(L_BRAS, 1);
    j += ",\"l2\":";         j += String(L_AVANTBRAS, 1);
    j += ",\"d\":";          j += String(D_EPAULE, 1);
    j += "},\"servos\":[";
    for (int i = 0; i < NB_SERVOS; i++) {
      const ServoConfig& c = SERVOS[i];
      if (i) j += ",";
      j += "{\"nom\":\"";  j += c.nom;
      j += "\",\"pin\":";  j += c.pin;
      j += ",\"min\":";    j += String(limMin[i], 1);
      j += ",\"max\":";    j += String(limMax[i], 1);
      j += ",\"course\":"; j += String(c.course, 1);
      j += ",\"repos\":";  j += String(c.repos, 1);
      j += ",\"zero\":";   j += String(c.zero, 1);
      j += ",\"sens\":";   j += c.sens;
      j += "}";
    }
    j += "]";
  }
  j += "}";
  return j;
}

// =====================================================================
// 4. SERVEUR WEB
// =====================================================================
void repondreEtat() { serveur.send(200, "application/json", etatJSON(true)); }

// Garde d'accès pour les routes de CONTRÔLE (/set, /ik, /repos, /limites, /trim, /cal).
// Désactivée par défaut (HTTP_AUTH_ACTIF=false dans config.h / lab.env) pour le TP de
// base ; passe-la à true pour l'exercice "bonus" qui referme cette faille. Les routes
// de simple LECTURE (/etat, /pos, fichiers 3D) restent volontairement ouvertes.
bool autoriseControle() {
  if (!HTTP_AUTH_ACTIF) return true;
  if (serveur.authenticate(HTTP_AUTH_USER, HTTP_AUTH_PASS)) return true;
  serveur.requestAuthentication();
  return false;
}

// /set?s=0&a=90.5   -> un servo
void routeSet() {
  if (!autoriseControle()) return;
  if (!serveur.hasArg("s") || !serveur.hasArg("a")) {
    serveur.send(400, "text/plain", "Parametres s et a requis"); return;
  }
  definirCible(serveur.arg("s").toInt(), serveur.arg("a").toFloat());
  repondreEtat();
}

// /ik?x=150&y=0&z=100   -> cinématique inverse
void routeIK() {
  if (!autoriseControle()) return;
  float r[3]; String erreur;
  if (cinematiqueInverse(serveur.arg("x").toFloat(), serveur.arg("y").toFloat(),
                         serveur.arg("z").toFloat(), r, erreur)) {
    for (int i = 0; i < 3; i++) definirCible(i, r[i]);
    repondreEtat();
  } else {
    serveur.send(400, "text/plain", erreur);
  }
}

// /limites?s=0&min=10&max=170   ou   /limites?reset=1
void routeLimites() {
  if (!autoriseControle()) return;
  if (serveur.hasArg("reset")) {
    prefs.clear();
    for (int i = 0; i < NB_SERVOS; i++) { limMin[i] = SERVOS[i].limMin; limMax[i] = SERVOS[i].limMax; }
  } else {
    int i = serveur.arg("s").toInt();
    float mn = serveur.arg("min").toFloat(), mx = serveur.arg("max").toFloat();
    if (i < 0 || i >= NB_SERVOS || mn < 0 || mx > SERVOS[i].course || mn >= mx) {
      serveur.send(400, "text/plain", "Limites invalides"); return;
    }
    limMin[i] = mn; limMax[i] = mx;
    prefs.putFloat(("min" + String(i)).c_str(), mn);
    prefs.putFloat(("max" + String(i)).c_str(), mx);
  }
  for (int i = 0; i < NB_SERVOS; i++) definirCible(i, angleCible[i]);  // re-borne
  repondreEtat();
}

// Fichiers de la mémoire LittleFS (three.js, robot.glb...)
// Un fichier "x.js.gz" est servi compressé quand on demande "x.js"
void routeFichier() {
  String chemin = serveur.uri();
  String type = "application/octet-stream";
  if (chemin.endsWith(".js"))   type = "application/javascript";
  if (chemin.endsWith(".glb"))  type = "model/gltf-binary";
  if (chemin.endsWith(".html")) type = "text/html";
  if (!LittleFS.exists(chemin) && LittleFS.exists(chemin + ".gz")) chemin += ".gz";
  if (!LittleFS.exists(chemin)) { serveur.send(404, "text/plain", "Introuvable"); return; }
  File f = LittleFS.open(chemin, "r");
  serveur.sendHeader("Cache-Control", "max-age=86400");
  serveur.streamFile(f, type);   // ajoute "Content-Encoding: gzip" pour les .gz
  f.close();
}

void demarrerServeur() {
  serveur.on("/", []() { serveur.send_P(200, "text/html", PAGE_HTML); });
  serveur.on("/etat", repondreEtat);
  // /pos : réponse minimale et rapide (juste les 4 angles cibles) pour l'animation 3D
  serveur.on("/pos", []() {
    String j = "[";
    for (int i = 0; i < NB_SERVOS; i++) { if (i) j += ","; j += String(angleCible[i], 1); }
    j += "]";
    serveur.send(200, "application/json", j);
  });
  // /trim?s=0&d=15  -> règle le décalage de calibration du servo (sauvegardé en flash)
  serveur.on("/trim", []() {
    if (!autoriseControle()) return;
    int i = serveur.arg("s").toInt();
    if (i < 0 || i >= NB_SERVOS) { serveur.send(400, "text/plain", "servo invalide"); return; }
    offsetServo[i] = serveur.arg("d").toFloat();
    prefs.putFloat(("off" + String(i)).c_str(), offsetServo[i]);
    ecrireServo(i, angleActuel[i]);   // applique tout de suite
    repondreEtat();
  });
  // /cal?s=0&reel=45  -> "le vrai moteur est physiquement à 45° en ce moment"
  //   Le robot calcule tout seul le décalage pour que site = réel. (sauvegardé)
  serveur.on("/cal", []() {
    if (!autoriseControle()) return;
    int i = serveur.arg("s").toInt();
    if (i < 0 || i >= NB_SERVOS) { serveur.send(400, "text/plain", "servo invalide"); return; }
    float reel = serveur.arg("reel").toFloat();
    offsetServo[i] += angleCible[i] - reel;                 // aligne le réel sur le site
    prefs.putFloat(("off" + String(i)).c_str(), offsetServo[i]);
    ecrireServo(i, angleActuel[i]);
    repondreEtat();
  });
  serveur.on("/set", routeSet);
  serveur.on("/ik", routeIK);
  serveur.on("/repos", []() { if (!autoriseControle()) return; allerRepos(); repondreEtat(); });
  serveur.on("/limites", routeLimites);
  serveur.onNotFound(routeFichier);
  serveur.begin();
}

// =====================================================================
// 5. MQTT
//   Reçoit :  brasrobot/servo/0 .. /3   "90"
//             brasrobot/angles          "90,45,120,30"
//             brasrobot/xyz             "150,0,100"
//             brasrobot/repos           (n'importe quoi)
//   Publie :  brasrobot/etat            {"cible":[...],"xyz":[...]}
//             brasrobot/erreur          message si IK impossible
//             brasrobot/dispo           "en ligne" / "hors ligne"
// =====================================================================
String sujet(const char* fin) { return String(MQTT_BASE) + "/" + fin; }

void recevoirMQTT(char* topic, byte* payload, unsigned int len) {
  char msg[64];
  len = min(len, (unsigned int)63);
  memcpy(msg, payload, len);
  msg[len] = 0;

  String t = String(topic).substring(strlen(MQTT_BASE) + 1);  // enlève "brasrobot/"
  if (t.startsWith("servo/")) {
    definirCible(t.substring(6).toInt(), atof(msg));
  } else if (t == "angles") {
    float a[4];
    int n = sscanf(msg, "%f,%f,%f,%f", &a[0], &a[1], &a[2], &a[3]);
    for (int i = 0; i < n && i < NB_SERVOS; i++) definirCible(i, a[i]);
  } else if (t == "xyz") {
    float p[3], r[3]; String erreur;
    if (sscanf(msg, "%f,%f,%f", &p[0], &p[1], &p[2]) == 3) {
      if (cinematiqueInverse(p[0], p[1], p[2], r, erreur))
        for (int i = 0; i < 3; i++) definirCible(i, r[i]);
      else
        mqtt.publish(sujet("erreur").c_str(), erreur.c_str());
    }
  } else if (t == "repos") {
    allerRepos();
  }
}

void gererMQTT() {
  if (!MQTT_ACTIF || WiFi.status() != WL_CONNECTED) return;

  if (!mqtt.connected()) {                      // reconnexion toutes les 5 s
    static unsigned long essai = 0;
    if (essai != 0 && millis() - essai < 5000) return;
    essai = millis();
    String id = "bras-" + String((uint32_t)ESP.getEfuseMac(), HEX);
    const char* user = strlen(MQTT_USER) ? MQTT_USER : nullptr;
    const char* pass = strlen(MQTT_USER) ? MQTT_PASS : nullptr;
    if (mqtt.connect(id.c_str(), user, pass, sujet("dispo").c_str(), 0, true, "hors ligne")) {
      mqtt.publish(sujet("dispo").c_str(), "en ligne", true);
      mqtt.subscribe(sujet("servo/+").c_str());
      mqtt.subscribe(sujet("angles").c_str());
      mqtt.subscribe(sujet("xyz").c_str());
      mqtt.subscribe(sujet("repos").c_str());
      Serial.println("MQTT connecte");
    } else {
      Serial.printf("MQTT echec (code %d)\n", mqtt.state());
    }
    return;
  }

  mqtt.loop();
  static unsigned long dernierPub = 0;
  if ((etatChange && millis() - dernierPub > 200) || millis() - dernierPub > 5000) {
    dernierPub = millis();
    etatChange = false;
    mqtt.publish(sujet("etat").c_str(), etatJSON(false).c_str());
  }
}

// =====================================================================
// 5 bis. MODBUS TCP  (l'ESP32 vu comme un actionneur industriel)
//   L'automate (OpenPLC...) écrit les consignes, lit les positions réelles.
//   Le web et le Modbus pilotent les MÊMES cibles : la vue 3D bouge quand
//   l'automate agit, ce qui rend l'impact d'une attaque visible.
// =====================================================================
void demarrerModbus() {
  mb.server();                                  // écoute sur le port 502
  for (int i = 0; i < NB_SERVOS; i++) {
    uint16_t v = (uint16_t)lroundf(angleCible[i] * 10);
    mb.addHreg(MB_HR_ANGLE + i, v);             // consigne (lecture/écriture par le maître)
    mb.addIreg(MB_IR_ANGLE + i, v);             // angle réel (lecture seule)
    mbDerniereConsigne[i] = v;
  }
  mb.addHreg(MB_HR_VITESSE, (uint16_t)lroundf(vitesseServo));
  mb.addHreg(MB_HR_CMD, 0);
  mb.addHreg(MB_HR_SPOOF, 0);
  mb.addIreg(MB_IR_CPT, 0);
  mb.addIreg(MB_IR_UPTIME, 0);
  mb.addIreg(MB_IR_CHARGE, 0);
  for (int i = 0; i < 3; i++) mb.addIreg(MB_IR_XYZ + i, 0);
}

void gererModbus() {
  mb.task();                                    // traite les requêtes des maîtres
  float x, y, z;

  // 1) Consignes reçues de l'automate -> on applique (via definirCible : limites respectées)
  for (int i = 0; i < NB_SERVOS; i++) {
    uint16_t recu = mb.Hreg(MB_HR_ANGLE + i);
    if (recu != mbDerniereConsigne[i]) {
      definirCible(i, recu / 10.0f);
      mbDerniereConsigne[i] = recu;
      mbCompteur++;                             // télémétrie : commande appliquée
    }
  }
  vitesseServo = constrain((float)mb.Hreg(MB_HR_VITESSE), 0.0f, 1000.0f);
  if (mb.Hreg(MB_HR_CMD) == 1) { allerRepos(); mb.Hreg(MB_HR_CMD, 0); }

  // Mode leurre : au moment où on l'active, on fige la position actuelle (la
  // "dernière valeur normale") ; elle sera renvoyée à la supervision tandis que
  // le bras, lui, continue de suivre les consignes -> l'écran ment.
  bool spoofDemande = mb.Hreg(MB_HR_SPOOF) != 0;
  if (spoofDemande && !mbSpoof) {
    cinematiqueDirecte(angleCible, x, y, z);
    for (int i = 0; i < NB_SERVOS; i++) mbSpoofAngle[i] = (uint16_t)lroundf(angleActuel[i] * 10);
    mbSpoofXYZ[0] = lroundf(x); mbSpoofXYZ[1] = lroundf(y); mbSpoofXYZ[2] = lroundf(z);
  }
  mbSpoof = spoofDemande;

  // 2) On renvoie l'état vers l'automate/supervision. Resync de la consigne si
  //    elle a changé ailleurs (web, série, MQTT).
  for (int i = 0; i < NB_SERVOS; i++) {
    uint16_t cible = (uint16_t)lroundf(angleCible[i] * 10);
    if (cible != mbDerniereConsigne[i]) {
      mb.Hreg(MB_HR_ANGLE + i, cible);
      mbDerniereConsigne[i] = cible;
    }
    // angle "vu" par la supervision : réel, ou figé si mode leurre
    mb.Ireg(MB_IR_ANGLE + i, mbSpoof ? mbSpoofAngle[i] : (uint16_t)lroundf(angleActuel[i] * 10));
  }
  cinematiqueDirecte(angleCible, x, y, z);
  int16_t vx = mbSpoof ? mbSpoofXYZ[0] : (int16_t)lroundf(x);
  int16_t vy = mbSpoof ? mbSpoofXYZ[1] : (int16_t)lroundf(y);
  int16_t vz = mbSpoof ? mbSpoofXYZ[2] : (int16_t)lroundf(z);
  mb.Ireg(MB_IR_XYZ + 0, (uint16_t)vx);
  mb.Ireg(MB_IR_XYZ + 1, (uint16_t)vy);
  mb.Ireg(MB_IR_XYZ + 2, (uint16_t)vz);

  // 3) Télémétrie (nourrit l'IHM et l'historian)
  float charge = 0;
  for (int i = 0; i < NB_SERVOS; i++) charge += fabsf(angleCible[i] - SERVOS[i].repos);
  charge = constrain(charge / (NB_SERVOS * 90.0f) * 100.0f, 0.0f, 100.0f);
  mb.Ireg(MB_IR_CPT, (uint16_t)mbCompteur);
  mb.Ireg(MB_IR_UPTIME, (uint16_t)(millis() / 1000));
  mb.Ireg(MB_IR_CHARGE, (uint16_t)lroundf(charge));
}

// =====================================================================
// 6. MONITEUR SÉRIE (debug) :  "0 90"  -> servo 0 à 90°
//                              "xyz 150 0 100"
// =====================================================================
void lireSerie() {
  if (!Serial.available()) return;
  String l = Serial.readStringUntil('\n');
  l.trim();
  float a, b, c; int s;
  if (sscanf(l.c_str(), "xyz %f %f %f", &a, &b, &c) == 3) {
    float r[3]; String erreur;
    if (cinematiqueInverse(a, b, c, r, erreur)) for (int i = 0; i < 3; i++) definirCible(i, r[i]);
    else Serial.println(erreur);
  } else if (sscanf(l.c_str(), "%d %f", &s, &a) == 2) {
    definirCible(s, a);
  }
  Serial.println(etatJSON(false));
}

// =====================================================================
// 7. WIFI
// =====================================================================
void demarrerWifi() {
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);                         // meilleure réactivité
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.print("Connexion WiFi");
  unsigned long t = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t < 15000) { delay(300); Serial.print("."); }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("\nConnecte ! Ouvre http://");
    Serial.println(WiFi.localIP());
  } else {
    WiFi.mode(WIFI_AP);
    WiFi.softAP(AP_SSID, AP_PASS);
    Serial.printf("\nWiFi introuvable -> reseau '%s' (mdp %s), ouvre http://", AP_SSID, AP_PASS);
    Serial.println(WiFi.softAPIP());
  }
  if (MDNS.begin(NOM_MDNS)) Serial.printf("ou http://%s.local\n", NOM_MDNS);
}

// =====================================================================
void setup() {
  Serial.begin(115200);
  Serial.setTimeout(50);
  delay(200);

  // Limites sauvegardées (sinon celles de config.h)
  prefs.begin("bras", false);
  for (int i = 0; i < NB_SERVOS; i++) {
    limMin[i] = prefs.getFloat(("min" + String(i)).c_str(), SERVOS[i].limMin);
    limMax[i] = prefs.getFloat(("max" + String(i)).c_str(), SERVOS[i].limMax);
    offsetServo[i] = prefs.getFloat(("off" + String(i)).c_str(), SERVOS[i].offset);
  }

  // Servos
  for (int i = 0; i < 4; i++) ESP32PWM::allocateTimer(i);
  for (int i = 0; i < NB_SERVOS; i++) {
    servo[i].setPeriodHertz(50);
    servo[i].attach(SERVOS[i].pin, SERVOS[i].usMin, SERVOS[i].usMax);
    angleCible[i] = angleActuel[i] = constrain(SERVOS[i].repos, limMin[i], limMax[i]);
    ecrireServo(i, angleActuel[i]);
  }

  if (!LittleFS.begin(true)) Serial.println("LittleFS indisponible");
  demarrerWifi();
  demarrerServeur();

  mqtt.setServer(MQTT_SERVEUR, MQTT_PORT);
  mqtt.setCallback(recevoirMQTT);
  mqtt.setBufferSize(512);

  demarrerModbus();
}

void loop() {
  serveur.handleClient();
  gererMQTT();
  gererModbus();
  majServos();
  lireSerie();
}
