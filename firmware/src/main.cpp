#include <Arduino.h>
#include <SdFat.h>

#define SD_CONFIG SdSpiConfig(SS, DEDICATED_SPI, SD_SCK_MHZ(15))

// count of voltage read
#define READ_CNT 100
// voltmeter pin
#define VOLTPIN A7 //240 min(3v), 327 max(4.1v)

#ifdef ARDUINO_AVR_NANO
// min voltage analog read (3v)
#define V_MIN 56 // 240 for lgt328; 56 for nano
// max voltage analog read (4.1v)
#define V_MAX 74 // 327 for lgt328; 74 for nano
#endif

#ifdef ARDUINO_ARCH_LGT
// min voltage analog read (3v)
#define V_MIN 240 // 240 for lgt328; 56 for nano
// max voltage analog read (4.1v)
#define V_MAX 327 // 327 for lgt328; 74 for nano
#endif

// voltmeter update each ms
#define V_UPD 2000
// scroll update each ms
#define S_UPD 100
// scroll end or start
#define S_UPD_DIR 1000

SdFat32 SD;
File fp;
File root;
char tme[6];
char trackName[100];
char trackNameScroll[13];
byte file_type = 0;
int sel = 0;
int sPos = 0;
int fileCnt = 0;
int trackMin = 0;
int trackSec = 0;
uint8_t mode = 0;
bool play = true;
bool save = false;
bool uart = false;
bool pause = false;
bool fStart = true;
bool scroll = false;
bool fillPerm = false;
bool secChange = true;
bool oneTrack = false;
bool fForward = false;
bool scrollDir = true;
bool nextTrack = false;
bool prevTrack = false;
bool nfoChange = false;
bool icnChange = false;
bool volChange = false;
bool batChange = false;
bool modeChange = false;
bool randomTrack = false;
bool playFinished = false;
unsigned long fileFRM = 0;
unsigned long mls = 0;
unsigned long mlsV = 0;
unsigned long mlsE = 0;
unsigned long mlsS = 0;
unsigned int vUp = V_UPD;
unsigned int sUp = S_UPD;

#include "store.h"
#include "ay.h"
#include "keypad.h"

bool checkExt(char *fname) {
  bool res = (strcasestr(fname,".yrg") || strcasestr(fname,".rsf") || strcasestr(fname,".psg")) ? true : false;
  return res;
}

void fillBuffer(){
  int fillSz = 0;
  int freeSz = bufSize;
  if (fillPos>playPos) {
    fillSz = fillPos-playPos;
    freeSz = bufSize - fillSz;
  }
  if (playPos>fillPos) {
    freeSz = playPos - fillPos;
    fillSz = bufSize - freeSz;
  }
  
  if (file_type == 1) {
    if (fp.available() && fillPerm) {
      sz = fp.position();
      fillPos = fp.read(playBuf,bufSize)/16;
      fillPerm = false;
    }
  }
  
  if (file_type == 2 || file_type == 3) {
    sz = fp.position();
    freeSz--; // do not reach playPos
    while (freeSz>0){
      byte b = 0xFD;
      if (fp.available()){
        b = fp.read();
      }
      playBuf[fillPos] = b;
      fillPos++;
      if (fillPos==bufSize) fillPos=0;
      freeSz--;
    }
  }
}

void clearEQtrack() {
  oled.rect(EQ_SHIFT_H, EQ_SHIFT_V-16, EQ_SHIFT_H+96, EQ_SHIFT_V+5+2, OLED_CLEAR);
  oled.rect(0,EQ_SHIFT_V+13,127,63,OLED_CLEAR);
}

int countDirectory(File dir) {
  int res = 0;
  root.rewindDirectory();
  File entry;
  while (true) {
    entry.openNext(&root, O_RDONLY);
    if (!entry)  break;
    entry.getName(trackName, sizeof(trackName));
    if (!entry.isDir() && checkExt(trackName)) {
      res++;
    }
    entry.close();
  }
  return res;
}

void scrollTrackName() {
  if (scroll) {
    if (millis() - mlsS > sUp) {
      oled.setCursorXY(13, 55);
      memcpy(&trackNameScroll, &trackName[sPos], sizeof(trackNameScroll)-1);
      oled.print(trackNameScroll);
      sUp = S_UPD;
      if (strlen(trackName)-sPos >= sizeof(trackNameScroll)-1 && scrollDir) {
        sPos++;
        if (strlen(trackName)-sPos < sizeof(trackNameScroll)-1) {scrollDir = false; sUp = S_UPD_DIR;}
      }
      if (!scrollDir) {
        sPos--;
        if (sPos < 0) {scrollDir = true; sUp = S_UPD_DIR;}
      }
      mlsS = millis();
    }
  }
}

void oledRootInfo() {
  if (uart && fStart) {
    clearEQtrack();
    oled.rect(EQ_SHIFT_H, EQ_SHIFT_V+7, EQ_SHIFT_H+96, EQ_SHIFT_V+5);
    oled.setCursorXY(34,EQ_SHIFT_V+13);
    oled.print("Serial mode");
    oled.setCursorXY(13,EQ_SHIFT_V+13+8+4);
    oled.print("(Baud rate 57600)");
    fStart = false;
  }
  if (!uart && fStart) {
    clearEQtrack();
    nfoChange = true;
    icnChange = true;
    secChange = true;
    modeChange = true;
    volChange = true;
    fStart = false;
  }
  if (!uart) {
    if(nfoChange) {
      //clear track name field
      oled.rect(0,63-8,6*12,63,OLED_CLEAR);
      //track name
      oled.drawBitmap(0,55, icons_8x8[99], 8, 8);
      oled.setCursorXY(13,55);
      if (strlen(trackName) > sizeof(trackNameScroll)-1) scroll = true;
      else scroll = false;
      sPos = 0;
      memcpy(&trackNameScroll, &trackName[sPos], sizeof(trackNameScroll)-1);
      oled.print(trackNameScroll);
      //current Track / Tracks count
      oled.setCursorXY(44-8,EQ_SHIFT_V+13);
      oled.print("|");
      sprintf(tme, "%03d", sel+1);
      oled.print(tme);
      oled.print("/");
      sprintf(tme, "%03d", fileCnt);
      oled.print(tme);
      oled.print("|");
      //track Time
      if (trackMin == 0 && trackSec == 0) {
        oled.rect(97,EQ_SHIFT_V+13,97+(6*5),EQ_SHIFT_V+13+4,OLED_CLEAR);
        oled.rect(0,EQ_SHIFT_V+13,6*5,EQ_SHIFT_V+13+4,OLED_CLEAR);
      } else {
        oled.setCursorXY(97,EQ_SHIFT_V+13);
        sprintf(tme, "%02d:%02d", trackMin, trackSec);
        oled.print(tme);
        secChange = true;
      }
      nfoChange = false;
    }
    //sec elapsed
    if(secChange){
      int m = sec/60;
      int s = sec-(m*60);
      //sec elapsed
      sprintf(tme, "%02d:%02d", m, s);
      oled.rect(0,EQ_SHIFT_V+13,6*5,EQ_SHIFT_V+13+7,OLED_CLEAR);
      oled.setCursorXY(0,EQ_SHIFT_V+13);
      oled.print(tme);
      secChange = false;
    }
    
    //oled.rect(0,EQ_SHIFT_V+13,127,63,OLED_CLEAR);55
    if (pause) {
      static bool pse = true;
      if (millis() - mls > 500) { 
        if (pse) oled.drawBitmap(92,55, icons_8x8[29], 8, 8);
        else oled.rect(92,55,92+8,55+7,OLED_CLEAR);
        pse = !pse;
        mls = millis(); 
      }
    }
    if (!pause && !fForward && play && icnChange) {
      oled.drawBitmap(92,55, icons_8x8[27], 8, 8);
      icnChange = false;
    }
    if (!play) oled.rect(92,55,92+8,55+7,OLED_CLEAR);
    if (fForward) {
      static bool pse = true;
      if (millis() - mls > 500) { 
        if (pse) oled.drawBitmap(92,55, icons_8x8[30], 8, 8);
        else oled.rect(92,55,92+8,55+7,OLED_CLEAR);
        pse = !pse;
        mls = millis(); 
      }
    }
    if (modeChange) oled.drawBitmap(108,55, spModes[mode], 16, 8);
    modeChange = false;
  }
}

void changeVolume() {
  if (volChange) {
    oled.drawBitmap(0,0, spFrame[0], 11, 35); //vol label&frame
    oled.roundRect(2, map(deviceSettings.volume, 0, 63, 32, 9), 8, 34, OLED_FILL); //fill vol
    writeToAmp(AMP_REG2, (muteL << 7 | muteR << 6 | deviceSettings.volume));
    volChange = false;
  }
  if (isBrtns) {
    oled.setContrast(deviceSettings.brightness);
    isBrtns = false;
  }
}

void voltage() {
  if (millis() - mlsV > vUp || batChange) {
    static int chgP = 0;
    //static int chgP = 17;
    unsigned int InVolt = 0;
    // Reading from a port with averaging
    for (int i = 0; i < READ_CNT; i++) {
      InVolt += analogRead(VOLTPIN);
    }
    InVolt = InVolt / READ_CNT;
    if(InVolt < V_MIN) InVolt = V_MIN;
    if(InVolt > V_MAX) InVolt = V_MAX;
    int v = map(InVolt, V_MIN, V_MAX, 32, 9);
    oled.drawBitmap(116,0, spFrame[1], 11, 35); //bat label&frame
    if (!btn.p7) {
      chgP++;
      if ((v-chgP)<9) chgP = 0;
      if (v-chgP < 33 && v-chgP > 8)
        oled.roundRect(118, v-chgP, 124, 34, OLED_FILL); //fill bat
      vUp = 250;
      /* // fake charging indicator
      chgP--;
      if (chgP<9) chgP = 17;
      if (chgP < 33 && chgP > 8)
        oled.roundRect(118, chgP, 124, 34, OLED_FILL); //fill bat
      vUp = 250;
       */
    } else {
      if (v < 33 && v > 8)
        oled.roundRect(118, v, 124, 34, OLED_FILL); //fill bat
      vUp = V_UPD;
      chgP = 0;
    }
    mlsV = millis();
    batChange = false;
  }
}

void changeTrackIcon(bool next = true) {
  //clear play icon field
  oled.rect(92,55,92+8,55+7,OLED_CLEAR);
  if (next) oled.drawBitmap(92,55, icons_8x8[114], 8, 8);
  else oled.drawBitmap(92,55, icons_8x8[115], 8, 8);
}

void checkSave() {
  if (millis() - mlsE > 7000 && save) {
    eepromSave();
    mlsE = millis();
    save = false;
  }
}

void prepareFile(char *fname){
  play = false;
  if(!fStart) {
    //clear track position and EQ field
    clearEQtrack();
  } else {
    oled.clear();
    fStart = false;
  }
  sec = 0;
  trackMin = 0;
  trackSec = 0;
  fileFRM = 0;
  fillPos = 0;
  playPos = 0;
  strcpy(trackName, fname);
  nfoChange = true;
  icnChange = true;
  modeChange = true;
  volChange = true;
  batChange = true;
  oledRootInfo();
  changeVolume();
  voltage();
  if (file_type == 3) {
    fp = SD.open(fname);
    while (fp.available()) {
      byte b = fp.read();
      if (b==0xFF) {fileFRM++; break;}
    }
    bool fe = false;
    while (fp.available()) {
      byte b = fp.read();
      switch(b) {
        case 0xFF:
          fileFRM++;
        break;
        case 0xFE:
          fe = true;
        break;
        case 0xFD:
        break;
        default:
          if(fe) {fileFRM += b*4-1; fe = false; break;}
        break;
      }
    }
    fp.close();
    trackMin = (fileFRM/50)/60;
    trackSec = (fileFRM/50)-(trackMin*60);
  }
  fp = SD.open(fname);
  fileSize = fp.size();
  if (file_type == 2) {
    word freq, offset;
    if( fp.read(playBuf,4) <= 0 ) return; // short file
    //if(playBuf[0] != 'R' || playBuf[1] != 'S' || playBuf[2] != 'F') return; //not RSF
    switch(playBuf[3]) { // reading RSF HEADER v3 only supported!
      case 3: // RSF ver.3
        if( fp.read(playBuf,14) == 0 ) return; // short file
        memcpy(&freq,&playBuf[0],sizeof(word));
        memcpy(&offset,&playBuf[2],sizeof(word));
        memcpy(&fileFRM,&playBuf[4],sizeof(unsigned long));
      break;
      default:
        return;
    }
    trackMin = (fileFRM/50)/60;
    trackSec = (fileFRM/50)-(trackMin*60);
    // skip text info
    fp.seek(offset);
  }
  if (file_type == 1) {
    fileFRM = fileSize/16;
    trackMin = (fileFRM/50)/60;
    trackSec = (fileFRM/50)-(trackMin*60);
    fillPerm = true;
    for(int i=0;i<14;i++) ay_write(i, 0);
    memset(regBuf,0,14);
  }
  nfoChange = true;
  icnChange = true;
  modeChange = true;
  volChange = true;
  oledRootInfo();
  changeVolume();
  if (!fp) {
    return;
  }
  if (file_type == 3) {
    //if( fp.read(playBuf,4) <= 0 ) return; // short file
    //if(playBuf[0] != 'P' || playBuf[1] != 'S' || playBuf[2] != 'G' || playBuf[3] != 0x1A) return; //not PSG
    while (fp.available()) {
      byte b = fp.read();
      if (b==0xFF) break;
    }
  }
  resetAY();
  fillBuffer();
  //eq frame
  oled.fastLineH(EQ_SHIFT_V-16-4,EQ_SHIFT_H-1,EQ_SHIFT_H+96+1,OLED_FILL);
  oled.fastLineV(EQ_SHIFT_H-2,EQ_SHIFT_V+5+2,EQ_SHIFT_V-16-3,OLED_FILL);
  oled.fastLineV(EQ_SHIFT_H+96+2,EQ_SHIFT_V+5+2,EQ_SHIFT_V-16-3,OLED_FILL);
  //sei();
}

void openIndexTrack(bool rnd = false) {
  for (int i=0;i<16;i++) ay_write(i,0);
  cli();
  if (sel > fileCnt-1) sel = 0;
  if (sel < 0) sel = fileCnt-1;
  if (rnd) sel = random(0,fileCnt-1);
  root.rewindDirectory();
  int i = 0;
  file_type = 0;
  File entry;
  while (true) {
    entry.openNext(&root, O_RDONLY);
    if (!entry)  break;
    entry.getName(trackName, sizeof(trackName));
    if (!entry.isDir() && checkExt(trackName)) {
      if (i==sel) {
        if (strcasestr(trackName,".yrg")) file_type = 1;
        if (strcasestr(trackName,".rsf")) file_type = 2;
        if (strcasestr(trackName,".psg")) file_type = 3;
        if (file_type == 0){
          nextTrack = true;
          break;
        }
        prepareFile(trackName);
        break;
      }
      i++;
    }
    entry.close();
  }
  nextTrack = false;
  prevTrack = false;
  playFinished = false;
}

void initFile(){
  pinMode(10, OUTPUT);
  digitalWrite(10, HIGH);
  sprintf_P(trackName, insertSD);
  while (!SD.begin(SD_CONFIG)) {
    if(play) {
      oled.clear();
      oled.setCursor(35,4);
      oled.print(trackName);
      play = false;
    }
  }
  root = SD.open("/");
  fileCnt = countDirectory(root);
  sprintf_P(trackName, noFiles);
  while (fileCnt == 0) {
    if(fStart) {
      oled.clear();
      oled.setCursorXY(20,32);
      oled.print(trackName);
      fStart = false;
    }
    root.close();
    SD.end();
    play = true;
    initFile();
  }
  fStart = true;
  openIndexTrack();
}

void setup() {
  // randomSeed(analogRead(A6));
  uint32_t seed = 0;
  for (int i = 0; i < 16; i++) {
    seed *= 4;
    seed += analogRead(A6) & 3;
    randomSeed(seed);
  }
  eepromLoad();
  delay(100);
  setupAYclock();
  resetAY();
  setupTimer();
  setupSerial();
  pinMode(A7, INPUT);
  Wire.setClock(400000L); // for fast draw for OLED
  buttonsSetup();
  Wire.setClock(400000L); // for fast draw for OLED
  ampInit();
  Wire.setClock(400000L); // for fast draw for OLED
  oledInit();
  delay(2000);
  initFile();
}

void loop() {
  buttonsState(readI2C(PCF_ADDRESS));
  if (!uart) {
    fillBuffer();
    scrollTrackName();
  }
  generalTick();
  oledEQ();
  oledRootInfo();
  changeVolume();
  voltage();
  checkSave();
  sei();
  play = true;
  if (playFinished){
    play = false;
    fp.close();
    changeTrackIcon(true);
    if(randomTrack) {sel++; openIndexTrack(true);}
    else if(oneTrack) openIndexTrack();
    else {sel++; openIndexTrack();}
  }
  if (nextTrack){
    play = false;
    fp.close();
    changeTrackIcon(true);
    if(randomTrack) openIndexTrack(true);
    else openIndexTrack();
  }
  if (prevTrack){
    play = false;
    fp.close();
    changeTrackIcon(false);
    openIndexTrack();
  }
}
