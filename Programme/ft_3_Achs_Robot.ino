/* Version vom 06.07.2026
   Diese Version erweitert die Robotersteuerung um eine Displayfuehrung,
   eine Referenzfahrt sowie zwei getrennte Positionsspeicher fuer
   speicherbare Einzel- und Dauerfahrten.

   Externe Startfunktion:
   I6 startet Positionsspeicher A.
   I5 startet Positionsspeicher B.

   Diese Version ist fuer den allgemeinen Betrieb des ft-3-Achs-Roboters
   vorgesehen und enthaelt keine spezielle Kopplungslogik fuer das
   ft-Hochregallager.
*/
#include <Ftduino.h>
#include "Wire.h"
#include "I2CKeyPad.h"
#include "SSD1306AsciiAvrI2c.h"
#include "Antriebsmodul.h"

// --------------------------------------------------
// Konstanten
// --------------------------------------------------
constexpr int LINKS   = Ftduino::LEFT;
constexpr int RECHTS  = Ftduino::RIGHT;
constexpr int STOP    = Ftduino::BRAKE;
constexpr int MAXIMAL = Ftduino::MAX;

constexpr int ANZAHL_ANTRIEBSMODULE = 4;
constexpr int MODUL_AUSLEGER = 1;

constexpr int GREIFER_PORT   = Ftduino::M4;
constexpr int AUSLEGER_PORT  = Ftduino::M2;
constexpr int TURM_PORT      = Ftduino::M3;
constexpr int KARUSSELL_PORT = Ftduino::M1;

constexpr int GREIFER_SPEED = Ftduino::MAX;
constexpr int AUSLEGER_SPEED = Ftduino::MAX;
constexpr int TURM_SPEED = Ftduino::MAX;
constexpr int KARUSSELL_SPEED = Ftduino::MAX/1.33;

constexpr int GREIFER_COUNT_MAX   = 26;
constexpr int AUSLEGER_COUNT_MAX = 135;
constexpr int TURM_COUNT_MAX      = 4500;
constexpr int KARUSSEL_COUNT_MAX = 4330;

constexpr char TASTE_REFERENZFAHRT    = 'D';
constexpr char TASTE_BETRIEBSART      = 'A';
constexpr char TASTE_SPEICHERN        = '#';
constexpr char TASTE_LOESCHE_SPEICHER = 'C';
constexpr char TASTE_EXPORT_SPEICHER  = 'B';
constexpr char TASTE_SPEICHERWAHL     = '8';
constexpr char TASTE_EINZELFAHRT      = '3';
constexpr char TASTE_DAUERFAHRT       = '6';

constexpr int SPEICHER_MAX_POS = 24;
constexpr int SPEICHER_POS_UNBELEGT = 32767;

constexpr int SENSOR_UMSETZPLATZ_A = Ftduino::I7;
constexpr int SENSOR_UMSETZPLATZ_B = Ftduino::I6;
constexpr unsigned long STARTUP_DELAY_MS = 1000;
constexpr unsigned long SENSOR_ENTPRELLZEIT_MS = 1000;

// --------------------------------------------------
// Tastatur
// --------------------------------------------------
const char tstMatrix[] PROGMEM =
  "123A456B789C*0#DNF";

struct HandTasten {

  int port;
  char fahreZuReferenz;
  char fahreZuCountMax;
};

struct Umsetzplatz {
  int sensorPort;
  bool belegtAlt;
  bool rohBelegt;
  bool positiveFlanke;
  unsigned long zustandswechselSeit;
};

struct ModulEintrag {
  Antriebsmodul* modul;
  const char* name;

  Antriebsmodul* operator->() {
    return modul;
  }
};

const HandTasten handTasten[] PROGMEM =
{
  {GREIFER_PORT,   '1', '2'},
  {AUSLEGER_PORT,  '4', '5'},
  {TURM_PORT,      '7', '8'},
  {KARUSSELL_PORT, '*', '0'}
};

// --------------------------------------------------
// Betriebsarten
// --------------------------------------------------
enum Betriebsart {
  HAND,
  AUTOMATIK,
  SPEICHER
};

enum AutomatikModus {
  AUTO_AUS,
  AUTO_EINMAL,
  AUTO_DAUER
};

enum PositionsSpeicher {
  SPEICHER_A,
  SPEICHER_B
};

Betriebsart betriebsartAktuell = HAND;

// --------------------------------------------------
// Status
// --------------------------------------------------
bool referenzOk = false;
bool automatikAktiv = false;
bool tasteGehalten = false;
bool automatikStopAngefordert = false;
bool speicherExportOk = false;
bool menueAuswahlAktiv = false;
bool referenzAnzeigeBisLoslassen = false;

AutomatikModus automatikModus = AUTO_AUS;
PositionsSpeicher aktiverPositionsSpeicher = SPEICHER_A;
int automatikIndex = 0;

bool referenzStatusAlt [ANZAHL_ANTRIEBSMODULE] = {false};

Umsetzplatz umsetzplatzA = {
  SENSOR_UMSETZPLATZ_A, false, false, false, 0
};
Umsetzplatz umsetzplatzB = {
  SENSOR_UMSETZPLATZ_B, false, false, false, 0
};

// --------------------------------------------------
// Hardware
// --------------------------------------------------
const uint8_t KEYPAD_ADDRESS  = 0x20;
const uint8_t DISPLAY_ADDRESS = 0x3C;
I2CKeyPad keyPad(KEYPAD_ADDRESS);
SSD1306AsciiAvrI2c oled;

// --------------------------------------------------
// Positionsspeicher
// --------------------------------------------------
int posSpeicherA[SPEICHER_MAX_POS][ANZAHL_ANTRIEBSMODULE] = {
  // Hier die mit Speicher-Export erzeugte Liste einfuegen.
  // Muss 24 Zeilen mit je 4 Werten enthalten.
  {0,0,3027,2282},
  {0,69,3027,2282},
  {21,69,3027,2282},
  {21,69,2630,2282},
  {21,0,2630,2942},
  {21,0,3671,2942},
  {0,0,3671,2942},
  {0,0,3027,2942},
  {0,0,3027,2282},
  {32767,32767,32767,32767},
  {32767,32767,32767,32767},
  {32767,32767,32767,32767},
  {32767,32767,32767,32767},
  {32767,32767,32767,32767},
  {32767,32767,32767,32767},
  {32767,32767,32767,32767},
  {32767,32767,32767,32767},
  {32767,32767,32767,32767},
  {32767,32767,32767,32767},
  {32767,32767,32767,32767},
  {32767,32767,32767,32767},
  {32767,32767,32767,32767},
  {32767,32767,32767,32767},
  {32767,32767,32767,32767}
};

int posSpeicherB[SPEICHER_MAX_POS][ANZAHL_ANTRIEBSMODULE] = {
  // Hier die mit Speicher-Export erzeugte Liste einfuegen.
  // Muss 24 Zeilen mit je 4 Werten enthalten.
  {0,0,3027,2282},
  {0,72,3027,2282},
  {21,72,3027,2282},
  {21,72,2775,2282},
  {21,14,2775,1948},
  {21,14,3810,1948},
  {0,14,3810,1948},
  {0,14,3027,1948},
  {0,0,3027,2282},
  {32767,32767,32767,32767},
  {32767,32767,32767,32767},
  {32767,32767,32767,32767},
  {32767,32767,32767,32767},
  {32767,32767,32767,32767},
  {32767,32767,32767,32767},
  {32767,32767,32767,32767},
  {32767,32767,32767,32767},
  {32767,32767,32767,32767},
  {32767,32767,32767,32767},
  {32767,32767,32767,32767},
  {32767,32767,32767,32767},
  {32767,32767,32767,32767},
  {32767,32767,32767,32767},
  {32767,32767,32767,32767}
};

int (*ptrPosSpeicher)[ANZAHL_ANTRIEBSMODULE] = posSpeicherA;

// --------------------------------------------------
// OBJEKTE
// --------------------------------------------------
Antriebsmodul greifer(GREIFER_PORT, GREIFER_COUNT_MAX, GREIFER_SPEED);
Antriebsmodul ausleger(AUSLEGER_PORT, AUSLEGER_COUNT_MAX, AUSLEGER_SPEED);
Antriebsmodul turm(TURM_PORT, TURM_COUNT_MAX, TURM_SPEED);
Antriebsmodul karussell(KARUSSELL_PORT, KARUSSEL_COUNT_MAX, KARUSSELL_SPEED);

ModulEintrag module[] = 
{
  {&greifer, "Greifer"},
  {&ausleger, "Ausleger"},
  {&turm, "Turm"},
  {&karussell, "Karussell"}
};

// --------------------------------------------------
// Prototypen
// --------------------------------------------------
void displayIntro();
void displayReferenzBereit();
void displayReferenzStatus(bool);
void displayHinweis();
void displayHand();
void displayAutomatik(bool komplettNeu = false);
void displaySpeicher();
void displayHandModulStatus(int, char);
void displayHandAllStatus(char);
void displayZweistellig(int);
void hand(char,bool);
void druckeHandCount(char);
void automatik(char,bool);
void resetAutomatik();
void startAutomatik(AutomatikModus);
void beendeAutomatik();
void starteExterneAutomatik();
void aktualisiereUmsetzplaetze();
void aktualisiereUmsetzplatz(Umsetzplatz&);
void speicher(char,bool);
bool referenzfahrtUpdate(char,bool);
void initPositionsSpeicher();
void speichereArbeitsposition();
bool istGleicheArbeitsposition(int);
void druckePositionsSpeicher();
void fahreArbeitsposition(int);
int getGespeichertePositionen();
char getSpeicherKennung();
void waehlePositionsSpeicher(PositionsSpeicher);
void wechselPositionsSpeicher();

// --------------------------------------------------
// setup
// --------------------------------------------------
void setup() {
  delay(STARTUP_DELAY_MS);

  ftduino.init();
  ftduino.input_set_mode(SENSOR_UMSETZPLATZ_A, Ftduino::SWITCH);
  ftduino.input_set_mode(SENSOR_UMSETZPLATZ_B, Ftduino::SWITCH);
  Serial.begin(115200);
  Wire.begin();
  keyPad.begin();

  for (int i = 0; i < ANZAHL_ANTRIEBSMODULE; i++) {
    module[i]->init();
  }

  oled.begin(&Adafruit128x64, DISPLAY_ADDRESS);
  oled.setFont(System5x7);
  displayIntro();
  displayReferenzBereit();
}

// --------------------------------------------------
// loop
// --------------------------------------------------
void loop() {

  char taste = pgm_read_byte(&tstMatrix[keyPad.getKey()]);
  bool tastenFlanke = false;

  if (taste != 'N' && !tasteGehalten) {
    tastenFlanke = true;
    tasteGehalten = true;
  }
  else if (taste == 'N') {
    tasteGehalten = false;
  }

  aktualisiereUmsetzplaetze();

  // --------------------------------------------------
  // Referenzfahrt
  // --------------------------------------------------
  if (!referenzOk) {
    static bool statusAnzeige = false;

    if ( taste == TASTE_REFERENZFAHRT) {
      if (!statusAnzeige) {
        displayReferenzStatus(true);
        statusAnzeige = true;
      }
      else {
        displayReferenzStatus(false);
      }
    }
    else {
      if (statusAnzeige) {
        displayReferenzBereit();
        statusAnzeige = false;
      }
    }

    referenzfahrtUpdate(taste, true);

    for (int i = 0; i < ANZAHL_ANTRIEBSMODULE; i++) {
      module[i]->update();
    }

    return;
  }

  if (referenzAnzeigeBisLoslassen) {
    if (taste == TASTE_REFERENZFAHRT) {
      return;
    }

    referenzAnzeigeBisLoslassen = false;
    displayHinweis();
    return;
  }

  if (menueAuswahlAktiv) {
    if (tastenFlanke && taste == TASTE_BETRIEBSART) {
      menueAuswahlAktiv = false;
      displayHand();
    }

    return;
  }

  // --------------------------------------------------
  // Betriebsart wechseln
  // --------------------------------------------------
  if (tastenFlanke && taste == TASTE_BETRIEBSART) {
    if (betriebsartAktuell == HAND) {
      betriebsartAktuell = AUTOMATIK;
    }
    else if (betriebsartAktuell == AUTOMATIK) {
      betriebsartAktuell = SPEICHER;
    }
    else {
      betriebsartAktuell = HAND;
    }
  }

  // --------------------------------------------------
  // Display Betriebsarten
  // --------------------------------------------------
  static Betriebsart letzteBetriebsart = HAND;

  if (betriebsartAktuell != letzteBetriebsart) {
    if (letzteBetriebsart == AUTOMATIK) {
      resetAutomatik();
    }
    if (letzteBetriebsart == SPEICHER) {
      speicherExportOk = false;
    }

    switch (betriebsartAktuell) {
      case HAND:
        displayHand();
      break;
      case AUTOMATIK:
        resetAutomatik();
        displayAutomatik(true);
      break;
      case SPEICHER:
        displaySpeicher();
      break;
    }

    letzteBetriebsart =  betriebsartAktuell;
  }

  // --------------------------------------------------
  // Betriebsarten
  // --------------------------------------------------
  switch (betriebsartAktuell) {
    case HAND:
      hand( taste, tastenFlanke);
    break;
    case AUTOMATIK:
      automatik(taste, tastenFlanke);
    break;
    case SPEICHER:
      speicher(taste, tastenFlanke);
    break;
  }

  // --------------------------------------------------
  // Module update
  // --------------------------------------------------
  for (int i = 0; i < ANZAHL_ANTRIEBSMODULE; i++) {
    module[i]->update();
  }
}

// --------------------------------------------------
// HAND
// --------------------------------------------------
void hand(char taste, bool tastenFlanke) {

  static int lastBewegung
  [ANZAHL_ANTRIEBSMODULE] = {STOP};
  static bool displayInitialisiert = false;
  static char letzteDisplayTaste = '\0';
  static char letzteHandTaste = 'N';
  static char tasteFuerCountAusgabe = 'N';
  static bool handReferenzfahrtAktiv = false;
  static bool letzteRefStatus[ANZAHL_ANTRIEBSMODULE] = {false};
  static bool letzteCntStatus[ANZAHL_ANTRIEBSMODULE] = {false};

  if (tasteFuerCountAusgabe != 'N') {
    druckeHandCount(tasteFuerCountAusgabe);
    tasteFuerCountAusgabe = 'N';
  }

  if (tastenFlanke && taste == TASTE_REFERENZFAHRT) {
    handReferenzfahrtAktiv = true;
  }

  if (handReferenzfahrtAktiv) {
    bool fertig = referenzfahrtUpdate(taste, false);

    for (int i = 0; i < ANZAHL_ANTRIEBSMODULE; i++) {
      lastBewegung[i] = STOP;
    }

    if (taste != TASTE_REFERENZFAHRT || fertig) {
      handReferenzfahrtAktiv = false;
    }
  }

  for (int i = 0; i < ANZAHL_ANTRIEBSMODULE; i++) {
    int neu = STOP;
    HandTasten tasten;
    memcpy_P(&tasten, &handTasten[i], sizeof(HandTasten));

    if (taste == tasten.fahreZuReferenz) {
      neu = LINKS;
    }
    else if (taste == tasten.fahreZuCountMax) {
      neu = RECHTS;
    }

    if (neu != lastBewegung[i]) {
      module[i]->setBewegung(neu);
      lastBewegung[i] = neu;
    }
  }
  if (tastenFlanke && taste == TASTE_SPEICHERN) {
    speichereArbeitsposition();
    displayInitialisiert = false;
  }

  bool displayUpdateNoetig = !displayInitialisiert || taste != letzteDisplayTaste;

  for (int i = 0; i < ANZAHL_ANTRIEBSMODULE; i++) {
    bool refStatus = module[i]->referenzErreicht();
    bool cntStatus = module[i]->getPosition() >= module[i]->getCountMax();

    if (refStatus != letzteRefStatus[i] || cntStatus != letzteCntStatus[i]) {
      displayUpdateNoetig = true;
      letzteRefStatus[i] = refStatus;
      letzteCntStatus[i] = cntStatus;
    }
  }

  if (displayUpdateNoetig) {
    displayHandAllStatus(taste);
    letzteDisplayTaste = taste;
    displayInitialisiert = true;
  }

  if (taste == 'N' && letzteHandTaste != 'N') {
    tasteFuerCountAusgabe = letzteHandTaste;
  }
  letzteHandTaste = taste;
}

void druckeHandCount(char taste) {
  for (int i = 0; i < ANZAHL_ANTRIEBSMODULE; i++) {
    HandTasten tasten;
    memcpy_P(&tasten, &handTasten[i], sizeof(HandTasten));

    if (taste == tasten.fahreZuReferenz || taste == tasten.fahreZuCountMax) {
      Serial.print(module[i].name);
      Serial.print(F(":"));

      int len = strlen(module[i].name);
      for (int s = len; s < 10; s++) {
        Serial.print(' ');
      }

      int position = module[i]->getPosition();
      if (position < 1000) {
        Serial.print('0');
      }
      if (position < 100) {
        Serial.print('0');
      }
      if (position < 10) {
        Serial.print('0');
      }
      Serial.println(position);
      return;
    }
  }
}

// --------------------------------------------------
// AUTOMATIK
// --------------------------------------------------
void automatik(char taste, bool tastenFlanke) {
  static bool positionGesetzt = false;
  static bool wartezeitAktiv = false;
  static unsigned long wartezeitStart = 0;
  constexpr unsigned long WARTEZEIT_MS = 500;
  int gespeichertePositionen = getGespeichertePositionen();

  bool automatikWarAktiv = automatikAktiv;
  starteExterneAutomatik();
  if (!automatikWarAktiv && automatikAktiv) {
    positionGesetzt = false;
    wartezeitAktiv = false;
  }
  gespeichertePositionen = getGespeichertePositionen();

  // --------------------------------------------------
  // Start Automatik
  // --------------------------------------------------
  if (tastenFlanke && !automatikAktiv) {
    if (taste == TASTE_EINZELFAHRT) {
      startAutomatik(AUTO_EINMAL);
      positionGesetzt = false;
      wartezeitAktiv = false;
      return;
    }
    else if (taste == TASTE_DAUERFAHRT) {
      startAutomatik(AUTO_DAUER);
      positionGesetzt = false;
      wartezeitAktiv = false;
      return;
    }
  }

  // --------------------------------------------------
  // Keine Automatik aktiv
  // --------------------------------------------------
  if (!automatikAktiv) {
    return;
  }

  if (tastenFlanke &&
      ((taste == TASTE_EINZELFAHRT && automatikModus == AUTO_EINMAL) ||
       (taste == TASTE_DAUERFAHRT && automatikModus == AUTO_DAUER))) {
    automatikStopAngefordert = true;
    displayAutomatik();
  }

  // --------------------------------------------------
  // Wartezeit zwischen Positionen
  // --------------------------------------------------
  if (wartezeitAktiv) {

    if (millis() - wartezeitStart >= WARTEZEIT_MS) {
      if (automatikStopAngefordert) {
        resetAutomatik();
        displayAutomatik();
        positionGesetzt = false;
        wartezeitAktiv = false;
        return;
      }

      wartezeitAktiv = false;
      automatikIndex++;

      if (automatikIndex >= gespeichertePositionen && automatikModus == AUTO_DAUER) {
        automatikIndex = 0;
      }

      positionGesetzt = false;
      displayAutomatik();
    }

    return;
  }

  // --------------------------------------------------
  // Ende Speicher erreicht
  // --------------------------------------------------
  if (automatikIndex >= gespeichertePositionen || ptrPosSpeicher[automatikIndex][0] == SPEICHER_POS_UNBELEGT) {
    beendeAutomatik();
    positionGesetzt = false;
    return;
  }

  // --------------------------------------------------
  // Position setzen
  // --------------------------------------------------
  if (!positionGesetzt) {
    fahreArbeitsposition(automatikIndex);
    positionGesetzt = true;
    displayAutomatik();
  }

  // --------------------------------------------------
  // Prüfen ob alle Achsen fertig
  // --------------------------------------------------
  bool fertig = true;

  for (int i = 0; i < ANZAHL_ANTRIEBSMODULE; i++) {
    if (!module[i]->istFertig()) {
      fertig = false;
    }
  }

  // --------------------------------------------------
  // Position erreicht
  // --------------------------------------------------
  if (fertig) {
    wartezeitAktiv = true;
    wartezeitStart = millis();
  }
}

void resetAutomatik() {
  for (int i = 0; i < ANZAHL_ANTRIEBSMODULE; i++) {
    module[i]->stop();
  }

  automatikAktiv = false;
  automatikStopAngefordert = false;
  automatikModus = AUTO_AUS;
  automatikIndex = 0;
}

void startAutomatik(AutomatikModus modus) {
  if (getGespeichertePositionen() == 0) {
    displayAutomatik();
    return;
  }

  automatikAktiv = true;
  automatikStopAngefordert = false;
  automatikModus = modus;
  automatikIndex = 0;
  displayAutomatik();

}

void beendeAutomatik() {
  resetAutomatik();
  displayAutomatik();
}

void starteExterneAutomatik() {
  if (automatikAktiv) {
    return;
  }

  if (umsetzplatzA.positiveFlanke) {
    waehlePositionsSpeicher(SPEICHER_A);
    if (getGespeichertePositionen() == 0) {
      displayAutomatik();
      return;
    }

    startAutomatik(AUTO_EINMAL);
  }
  else if (umsetzplatzB.positiveFlanke) {
    waehlePositionsSpeicher(SPEICHER_B);
    if (getGespeichertePositionen() == 0) {
      displayAutomatik();
      return;
    }

    startAutomatik(AUTO_EINMAL);
  }
}

void aktualisiereUmsetzplaetze() {
  aktualisiereUmsetzplatz(umsetzplatzA);
  aktualisiereUmsetzplatz(umsetzplatzB);
}

void aktualisiereUmsetzplatz(Umsetzplatz& platz) {
  bool belegt = ftduino.input_get(platz.sensorPort) != 0;
  platz.positiveFlanke = false;

  if (belegt != platz.rohBelegt) {
    platz.rohBelegt = belegt;
    platz.zustandswechselSeit = millis();
  }

  if (platz.rohBelegt == platz.belegtAlt ||
      millis() - platz.zustandswechselSeit < SENSOR_ENTPRELLZEIT_MS) {
    return;
  }

  platz.positiveFlanke = platz.rohBelegt;

  platz.belegtAlt = platz.rohBelegt;
}

// --------------------------------------------------
// SPEICHER
// --------------------------------------------------
void speicher(char taste, bool tastenFlanke) {

  if (!tastenFlanke) {
    return;
  }

  if (taste == TASTE_SPEICHERWAHL) {
    wechselPositionsSpeicher();
    speicherExportOk = false;
    displaySpeicher();
    return;
  }

  if (taste == TASTE_LOESCHE_SPEICHER) {
    initPositionsSpeicher();
    displaySpeicher();
  }

  if (taste == TASTE_EXPORT_SPEICHER) {
    druckePositionsSpeicher();
    speicherExportOk = true;
    displaySpeicher();
  }
}

// --------------------------------------------------
// REFERENZFAHRT
// --------------------------------------------------
// Zuert wird der Ausleger referenziert (Endschalter I2), dann
// können die drei anderen Referenzpositionen angefahren werden.
bool referenzfahrtUpdate(char taste, bool menueNachFertig) {
  static bool ausleger_hat_referenziert = false;
  static bool referenzTasteWarGedrueckt = false;

  bool fertig = true;
  bool referenzTasteGedrueckt = taste == TASTE_REFERENZFAHRT;
  bool auslegerEingefahren = module[MODUL_AUSLEGER]->referenzErreicht();

  if (!referenzTasteGedrueckt || !referenzTasteWarGedrueckt) {
    ausleger_hat_referenziert = false;
  }
  referenzTasteWarGedrueckt = referenzTasteGedrueckt;

  if (auslegerEingefahren) {
    ausleger_hat_referenziert = true;
  }

  for (int i = 0; i < ANZAHL_ANTRIEBSMODULE; i++) {
    if (!module[i]->referenzErreicht()) {
      fertig = false;
      if (referenzTasteGedrueckt && (i == MODUL_AUSLEGER || ausleger_hat_referenziert)) {
        module[i]->referenzStart();
      }
      else {
        module[i]->stop();
      }
    }
    else {
      module[i]->stop();
      module[i]->referenzSetzen();
    }
  }
  if (fertig) {
    referenzOk = true;
    menueAuswahlAktiv = menueNachFertig;
    referenzAnzeigeBisLoslassen = menueNachFertig && referenzTasteGedrueckt;

    if (menueNachFertig && !referenzAnzeigeBisLoslassen) {
      displayHinweis();
    }

    ausleger_hat_referenziert = false;
    referenzTasteWarGedrueckt = false;
  }

  return fertig;
}

// --------------------------------------------------
// POSITIONSSPEICHER
// --------------------------------------------------
void initPositionsSpeicher() {

  for (int i = 0; i < SPEICHER_MAX_POS; i++) {
    for (int j = 0;j < ANZAHL_ANTRIEBSMODULE; j++) {
      ptrPosSpeicher[i][j] = SPEICHER_POS_UNBELEGT;
    }
  }
}

// --------------------------------------------------
// POSITION SPEICHERN
// --------------------------------------------------
void speichereArbeitsposition() {

  for (int i = 0; i < SPEICHER_MAX_POS; i++) {
    if (ptrPosSpeicher[i][0] == SPEICHER_POS_UNBELEGT) {
      if (i > 0 && istGleicheArbeitsposition(i - 1)) {
        return;
      }

      for (int j = 0; j < ANZAHL_ANTRIEBSMODULE; j++) {
        ptrPosSpeicher[i][j] =
          module[j]->getPosition();
      }

      displayHand();

      return;
    }
  }

}

bool istGleicheArbeitsposition(int index) {
  if (index < 0 || index >= SPEICHER_MAX_POS) {
    return false;
  }

  for (int i = 0; i < ANZAHL_ANTRIEBSMODULE; i++) {
    if (ptrPosSpeicher[index][i] != module[i]->getPosition()) {
      return false;
    }
  }

  return true;
}

// --------------------------------------------------
// POSITIONSSPEICHER DRUCKEN
// --------------------------------------------------
void druckePositionsSpeicher() {

  Serial.println();
  Serial.println("  // Reihenfolge: Greifer, Ausleger, Turm, Karussell");
 
  for (int i = 0; i < SPEICHER_MAX_POS; i++) {
    Serial.print("  {");
    for (int j = 0; j < ANZAHL_ANTRIEBSMODULE; j++) {
      Serial.print(ptrPosSpeicher[i][j]);
      if (j < ANZAHL_ANTRIEBSMODULE - 1) {
        Serial.print(",");
      }
    }
    Serial.print("}");
    if (i < SPEICHER_MAX_POS - 1) {
      Serial.print(",");
    }
    Serial.println();
  }
  Serial.println();
}

// --------------------------------------------------
// POSITION ANFAHREN
// --------------------------------------------------
void fahreArbeitsposition(int index) {

  if (index >= SPEICHER_MAX_POS) {
    return;
  }

  if (ptrPosSpeicher[index][0] == SPEICHER_POS_UNBELEGT) {
    return;
  }

  for (int i = 0; i < ANZAHL_ANTRIEBSMODULE; i++) {
    module[i]->setArbeitsposition(ptrPosSpeicher[index][i]);
  }
}

int getGespeichertePositionen() {
  for (int i = 0; i < SPEICHER_MAX_POS; i++) {
    if (ptrPosSpeicher[i][0] == SPEICHER_POS_UNBELEGT) {
      return i;
    }
  }

  return SPEICHER_MAX_POS;
}

char getSpeicherKennung() {
  return aktiverPositionsSpeicher == SPEICHER_A ? 'A' : 'B';
}

void waehlePositionsSpeicher(PositionsSpeicher positionsSpeicher) {
  aktiverPositionsSpeicher = positionsSpeicher;
  ptrPosSpeicher = positionsSpeicher == SPEICHER_A
    ? posSpeicherA
    : posSpeicherB;
}

void wechselPositionsSpeicher() {
  if (aktiverPositionsSpeicher == SPEICHER_A) {
    waehlePositionsSpeicher(SPEICHER_B);
  }
  else {
    waehlePositionsSpeicher(SPEICHER_A);
  }
}

// --------------------------------------------------
// ************* Display-Funktionen ************* 
// --------------------------------------------------

// --------------------------------------------------
// DISPLAY-INTRO
// --------------------------------------------------
void displayIntro() {

  oled.clear();
  oled.println();
  oled.println(F("    3-Achs Roboter   "));
  oled.println();
  oled.println(F("         mit         "));
  oled.println();
  oled.println(F("       ftDuino       "));

  delay(6000);

  oled.clear();
}

// --------------------------------------------------
// DISPLAY REFERENZ
// --------------------------------------------------
void displayReferenzBereit() {

  oled.clear();

  oled.setCursor(18,1);
  oled.print(F("Referenzfahrt"));

  oled.setCursor(28,3);
  oled.print(F("mit Taste D"));
}

void displayReferenzStatus(bool forceUpdate) {

  bool updateNoetig = forceUpdate;

  for (int i = 0; i < ANZAHL_ANTRIEBSMODULE; i++) {
    bool status = module[i]->referenzErreicht();
    if (status != referenzStatusAlt[i]) {
      referenzStatusAlt[i] = status;
      updateNoetig = true;
    }
  }

  if (!updateNoetig) {
    return;
  }

  oled.clear();

  oled.setCursor(18,0);
  oled.print(F("Referenzfahrt"));

  for ( int i = 0; i < ANZAHL_ANTRIEBSMODULE; i++) {
    oled.setCursor(0,i + 2);
    oled.print(module[i].name);
    int len = strlen(module[i].name);

    for (int s = len; s < 12; s++) {
      oled.print(' ');
    }

    if (referenzStatusAlt[i]) {
      oled.print(F("O"));
    }
    else {
      oled.print(F("X"));
    }
  }
}

void displayHinweis() {

  oled.clear();

  oled.setCursor(18,0);
  oled.print(F("H I N W E I S !"));

  oled.setCursor(0,2);
  oled.print(F("Referenz mit D ok"));

  oled.setCursor(0,4);
  oled.print(F("Menue Anwahl mit A"));

  oled.setCursor(0,5);
  oled.print(F("> HAND"));

  oled.setCursor(0,6);
  oled.print(F("> AUTOMATIK"));

  oled.setCursor(0,7);
  oled.print(F("> SPEICHER"));
}

// --------------------------------------------------
// DISPLAY HAND
// --------------------------------------------------
void displayHand() {

  oled.clear();

  oled.print(F("H A N D"));
  oled.setCursor(110,0);
  oled.print(F("Ps"));
  oled.println(getSpeicherKennung());
  oled.println();
  oled.println(F("           REF|CNT"));
  oled.println(F("Greifer     1 | 2 "));
  oled.println(F("Ausleger    4 | 5 "));
  oled.println(F("Turm        7 | 8 "));
  oled.println(F("Karussell   * | 0 "));

  int anzahl = getGespeichertePositionen();

  oled.print(F("Pos-Sichern # "));
  if (anzahl < 10) {
    oled.print('0');
  }
  oled.print(anzahl);
  oled.print('/');
  if (SPEICHER_MAX_POS < 10) {
    oled.print('0');
  }
  oled.print(SPEICHER_MAX_POS);
  oled.println();

  displayHandAllStatus('N');
}

// --------------------------------------------------
// DISPLAY AUTOMATIK
// --------------------------------------------------
void displayAutomatik(bool komplettNeu) {
  static AutomatikModus letzterModus = AUTO_AUS;
  static char letzteSpeicherKennung = '\0';
  int aktuellePosition = 0;
  int gespeichertePositionen = getGespeichertePositionen();
  char speicherKennung = getSpeicherKennung();

  if (automatikAktiv) {
    aktuellePosition = automatikIndex + 1;
  }

  if (komplettNeu || automatikModus != letzterModus ||
      speicherKennung != letzteSpeicherKennung) {
    oled.clear();
    oled.println(F("A U T O M A T I K"));
    oled.println();

    oled.print(F("Einzelfahrt "));
    oled.print(automatikModus == AUTO_EINMAL ? F("  Stop ") : F(" Start "));
    oled.print(TASTE_EINZELFAHRT);
    oled.println();

    oled.println(F("Ext.-Start   I5 o I6"));

    oled.print(F("Dauerbetrieb"));
    oled.print(automatikModus == AUTO_DAUER ? F("  Stop ") : F(" Start "));
    oled.print(TASTE_DAUERFAHRT);
    oled.println();

    oled.setCursor(0,7);
    oled.print(F("Position Ps"));
    oled.print(speicherKennung);
    oled.print(':');

    letzterModus = automatikModus;
    letzteSpeicherKennung = speicherKennung;
  }

  oled.setCursor(96,7);
  displayZweistellig(aktuellePosition);
  oled.print('/');
  displayZweistellig(gespeichertePositionen);
}

// --------------------------------------------------
// DISPLAY SPEICHER
// --------------------------------------------------
void displaySpeicher() {
  int gespeichertePositionen = getGespeichertePositionen();

  oled.clear();
  oled.print(F("S P E I C H E R"));
  oled.setCursor(110,0);
  oled.print(F("Ps"));
  oled.println(getSpeicherKennung());
  oled.println();

  oled.print(F("Speicherwahl mit "));
  oled.println(TASTE_SPEICHERWAHL);

  oled.print(F("Export "));
  oled.print(TASTE_EXPORT_SPEICHER);
  oled.print(F("| "));
  if (speicherExportOk) {
    oled.print(F("ok"));
  }
  else {
    displayZweistellig(SPEICHER_MAX_POS);
  }
  oled.print('/');
  displayZweistellig(SPEICHER_MAX_POS);
  oled.println();

  oled.print(F("Leeren "));
  oled.print(TASTE_LOESCHE_SPEICHER);
  oled.print(F("| "));
  displayZweistellig(gespeichertePositionen);
  oled.print('/');
  displayZweistellig(SPEICHER_MAX_POS);
  oled.println();
}

void displayZweistellig(int zahl) {
  if (zahl < 10) {
    oled.print('0');
  }
  oled.print(zahl);
}

void displayHandModulStatus(int index, char taste) {
  HandTasten tasten;
  memcpy_P(&tasten, &handTasten[index], sizeof(HandTasten));

  int row = 3 + index;
  bool refErreicht = module[index]->referenzErreicht();
  bool cntErreicht = module[index]->getPosition() >= module[index]->getCountMax();

  oled.setCursor(72,row);
  if (taste == tasten.fahreZuReferenz || taste == TASTE_REFERENZFAHRT) {
    oled.print(refErreicht ? F("O") : F("X"));
  }
  else {
    oled.print(tasten.fahreZuReferenz);
  }

  oled.setCursor(96,row);
  if (taste == tasten.fahreZuCountMax) {
    oled.print(cntErreicht ? F("O") : F("X"));
  }
  else {
    oled.print(tasten.fahreZuCountMax);
  }
}

void displayHandAllStatus(char taste) {
  for (int i = 0; i < ANZAHL_ANTRIEBSMODULE; i++) {
    displayHandModulStatus(i, taste);
  }
}
