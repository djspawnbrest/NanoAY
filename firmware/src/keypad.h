#pragma once
#include <EncButton.h>
#include <microWire.h>

#define PCF_ADDRESS 0x3F // try 0x3F or 0x27
// --- Audio Amplifier I2C definitions ---
#define AMP_ADDRESS 0x60       // TPA6130A2 device address (1100000 BIN; 96 DEC; 0x60 HEX)
// Register 1: HP_EN_L, HP_EN_R, Mode[1], Mode[0], Reserved, Reserved, Reserved, Thermal, SWS
#define AMP_REG1 0x1           // TPA6130A2 register 1 address
#define AMP_REG1_SETUP 0xc0    // default configuration: 11000000 - both channels enabled
// Register 2: Mute_L, Mute_R, Volume[5-0]
#define AMP_REG2 0x2           // TPA6130A2 register 2 address
#define AMP_REG2_SETUP 0x34    // default configuration: 00110100 - both channels on -0.3 dB Gain

struct pins{
  bool p0 = true;
  bool p1 = true;
  bool p2 = true;
  bool p3 = true;
  bool p4 = true;
  bool p5 = true;
  bool p6 = true;
  bool p7 = false;
} btn;

bool muteL = false;
bool muteR = false;
bool isBrtns = false;

EncButton<EB_TICK, VIRT_BTN> rt;
EncButton<EB_TICK, VIRT_BTN> pp;
EncButton<EB_TICK, VIRT_BTN> lt;
EncButton<EB_TICK, VIRT_BTN> up;
EncButton<EB_TICK, VIRT_BTN> dn;

void buttonsState(byte state){
  btn.p0 = !bitRead(state, 0);
  btn.p1 = !bitRead(state, 1);
  btn.p2 = !bitRead(state, 2);
  btn.p3 = !bitRead(state, 3);
  btn.p4 = !bitRead(state, 4);
  btn.p5 = !bitRead(state, 5);
  btn.p6 = !bitRead(state, 6);
  btn.p7 = !bitRead(state, 7);
}

byte readI2C(byte address) {
  byte res;
  Wire.requestFrom(address, (uint8_t)1);
  if (Wire.available()) res = Wire.read();
  return res;
}

void writeI2C(byte address, byte value){
  Wire.beginTransmission(address);
  Wire.write(value);
  Wire.endTransmission();
}

void writeToAmp(byte address, byte val) {
  Wire.beginTransmission(AMP_ADDRESS);  // start transmission to device   
  Wire.write( address );                // send register address
  Wire.write( val );                    // send value to write
  Wire.endTransmission();               // end transmission
}

void ampInit() {
  writeToAmp(AMP_REG1, AMP_REG1_SETUP);
  writeToAmp(AMP_REG2, (muteL << 7 | muteR << 6 | deviceSettings.volume));
}

void buttonsSetup() {
  writeI2C(PCF_ADDRESS, 0xFF);
  rt.setHoldTimeout(500);
  pp.setHoldTimeout(500);
  lt.setHoldTimeout(500);
  up.setHoldTimeout(500);
  dn.setHoldTimeout(500);
}

void playerModeChange() {
  cli();
  if(uart) {
    TIMSK1 &= ~(1<<OCIE1A); //disable AY timer interrupt
    UCSR0B |= (1<<RXCIE0); //enable RX interrupt
  } else {
    UCSR0B &= ~(1<<RXCIE0); //disable RX interrupt
    TIMSK1 |= (1<<OCIE1A); //enable AY timer interrupt
  }
  sei();
  pauseAY();
  fStart = true;
}

void buttonsTick(){
  if (rt.click()) {
    if (!randomTrack) sel ++;
    nextTrack = true;
  }
  if (rt.held()) {fForward = true; icnChange = true;}
  if (rt.release()) {fForward = false; icnChange = true;}
  if (pp.click()) {pause = !pause;  icnChange = true;}
  if (pp.held()) {
    mode++;
    if(mode >= 3) mode = 0;
    randomTrack = (mode==1)?true:false;
    oneTrack = (mode==2)?true:false;
    modeChange = true;
  }
  if (lt.click()) {sel--; prevTrack = true;}
}

void generalTick() {
  rt.tick(btn.p0);
  pp.tick(btn.p1);
  lt.tick(btn.p2);
  up.tick(btn.p4);
  dn.tick(btn.p3);
  if (lt.held()) {uart = !uart; playerModeChange();}
  if (pp.hold()) isBrtns = true;
  if (up.click() || up.hold()) {
    if (!isBrtns) {
      if (deviceSettings.volume < 63) {
        deviceSettings.volume++;
        volChange = true;
      }
    } else {
      if (deviceSettings.brightness < 255) deviceSettings.brightness++;
    }
  }
  if (dn.click() || dn.hold()) {
    if (!isBrtns) {
      if (deviceSettings.volume > 0) {
        deviceSettings.volume--;
        volChange = true;
      }
    } else {
      if (deviceSettings.brightness > 0) deviceSettings.brightness--;
    }
  }
  if (dn.release() || up.release()) {
    mlsE = millis();
    save = true;
  }
  if (!uart) buttonsTick();
}
