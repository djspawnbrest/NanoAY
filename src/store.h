#pragma once

#include <EEPROM.h>
// Dont change this
#define CONFIG_START 0

// Change this if the settings structure changes
#define CONFIG_VERSION "Ay1"
/*------------------------------DEFAULT VALUES----------------------------------*/
#define BRIGHTNESS 255                 // OLED default brightness (0-255)
#define VOLUME     32                  // Amp default value (0-63)
/*-----------------------------END DEFAULT VALUES-------------------------------*/

struct StoreStruct {
  // StoreStruct version
  char version[4];
  // Device settings:
  uint8_t brightness;
  uint8_t volume;
} deviceSettings = {
  CONFIG_VERSION,
  // ---------------- The default values ------------------
  BRIGHTNESS,                        // Brightness
  VOLUME                            // Volume
};

void(* resetFunc) (void) = 0; //declare reset function @ address 0

void eepromSave() {
  for (uint16_t t = 0; t < sizeof(deviceSettings); t++)
    EEPROM.write(CONFIG_START + t, *((char*)&deviceSettings + t));
 // EEPROM.commit();
}

void eepromLoad() {
  // Store defaults for if we need them
  StoreStruct tmpStore;
  tmpStore = deviceSettings;
  // To make sure there are settings, and they are YOURS!
  // If nothing is found it will use the default settings.
  if (EEPROM.read(CONFIG_START + 0) == CONFIG_VERSION[0] &&
      EEPROM.read(CONFIG_START + 1) == CONFIG_VERSION[1] &&
      EEPROM.read(CONFIG_START + 2) == CONFIG_VERSION[2]) {
    // Copy data to deviceSettings structure
    for (uint16_t t = 0; t < sizeof(deviceSettings); t++)
      *((char*)&deviceSettings + t) = EEPROM.read(CONFIG_START + t);

  // If config files dont match, save defaults then erase the ESP config to clear away any residue
  } else {
    deviceSettings = tmpStore;
    eepromSave();
    delay(500);
    resetFunc();  //call reset
    // while(1);
  }
}
