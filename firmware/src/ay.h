#pragma once

#define USE_MICRO_WIRE
#define USART_BAUDRATE 57600
#define BAUD_PRESCALE ((((F_CPU/(USART_BAUDRATE*16UL)))-1)*2UL)
// AY timer = 100Hz (need for fast forward)
#define AY_TIMER F_CPU/100UL/256UL-1UL
#define DIVISOR (F_CPU/16000000UL)

#include "ayFreqTable.h"
#include <GyverOLED.h>
#include "icons_8x8.h"
#include "sprites.h"

#define EQ_SHIFT_V 27 //from TOP
#define EQ_SHIFT_H 15 //from LEFT
// EQ read from AY - 1, read from file/stram - 0
#define AY_EQ 1

int skipCnt = 0;
byte reg_num = 0;
bool reg = false;
bool writeFlag = true;
unsigned int playPos = 0;
unsigned int fillPos = 0;
unsigned long fileSize;
unsigned long sec = 0;
unsigned int timerB;
unsigned long sz;

const int bufSize = 128; // select a multiple of 16
static byte playBuf[bufSize]; // 31 bytes per frame max, 50*31 = 1550 per sec, 155 per 0.1 sec
static byte regBuf[14];
static byte bufEQ[96];
static bool bufEQcl[96];

uint8_t A, B, C;
uint8_t tA=0, tB=0, tC=0;
uint8_t bA=0, bB=0, bC=0;

GyverOLED<SSD1306_128x64, OLED_NO_BUFFER> oled;

void oledInit() {
  oled.init();
  Wire.setClock(400000L); // for fast draw for OLED
  oled.setContrast(deviceSettings.brightness);
  oled.rect(64,128,0,0,OLED_CLEAR);
  oled.drawBitmap(0,0,spLogo,128,64);
  oled.setCursor(110,7);
  oled.print(deviceSettings.version);
}

void setupAYclock(){
  // 1,777,777.8 Hz Frequency
  TCCR2A = 0x23;  // 0010 0011, Disable Timer
  TCCR2B = 0x08;  // 0000 1000, Disable Timer 
  OCR2A = 8*DIVISOR;  // Low PWM Resolution, 11.11% Duty step-size 
  OCR2B = OCR2A/2;
  TCNT2=0x0;
  DDRD |= 0x08;  //D3 pin as AY Clock
  TCCR2B |= 1;  // Prescale=1, Enable Timer 
}

void setupTimer(){
  cli(); 
  TCCR1A = 0;
  TCCR1B = 0;
  TCNT1  = 0;
  // (16000000/((1249+1)x256)) = 50 Hz, if 32MHz clock = 100 Hz
  OCR1A = AY_TIMER;
  TCCR1B |= (1 << WGM12);
  // prescalar 256
  TCCR1B |= (1 << CS12);
  TIMSK1 |= (1 << OCIE1A);  // Enable interrupt on coincidence A
  //TIMSK1 |= (1 << OCIE1B);  // Enable interrupt on coincidence B
  sei();
}

void setupSerial() {
  cli();
  UCSR0A = (1<<U2X0);
  UCSR0B |= (1<<RXEN0)|(1<<TXEN0);//|(1<<RXCIE0);
  UCSR0C = (1<<UCSZ01)|(1<<UCSZ00);
  UBRR0L = BAUD_PRESCALE;
  sei();
}

/*
 * inactive       BDIR 0, BC1 0 NACT
 * read from PSG  BDIR 0, BC1 1 DTB
 * write to PSG   BDIR 1, BC1 0 DWS
 * lach addr      BDIR 1, BC1 1 INTAK
 */

void ay_write(uint8_t reg, uint8_t data){
  //set register
  PORTC |= reg & 0x0F;
  PORTD |= reg & 0xF0;
  //lach address
  PORTB |= 0x03;
  asm("nop\nnop\nnop\nnop\nnop\nnop\nnop\nnop\nnop");
  //set inactive
  PORTB &= ~(0x03);
  //clear register
  PORTC &= ~(reg & 0x0F);
  PORTD &= ~(reg & 0xF0);
  //set data
  PORTC |= data & 0x0F;
  PORTD |= data & 0xF0;
  //write mode
  PORTB |= 0x02;
  asm("nop\nnop\nnop\nnop\nnop");
  //set BCDIR to 0 (inactive)
  PORTB &= ~(0x02);
  //clear data
  PORTC &= ~(data & 0x0F);
  PORTD &= ~(data & 0xF0);
}

uint8_t ay_read(uint8_t reg) {
  uint8_t res = 0;
  //set register
  PORTC |= reg & 0x0F;
  PORTD |= reg & 0xF0;
  //lach address
  PORTB |= 0x03;
  asm("nop\nnop\nnop\nnop\nnop\nnop\nnop\nnop\nnop");
  //set inactive
  PORTB &= ~(0x03);
  //clear register
  PORTC &= ~(reg & 0x0F);
  PORTD &= ~(reg & 0xF0);
  //read mode
  PORTB |= 0x01;
  //set 0 - (input) C0-3, D4-7
  asm("nop\nnop");
  DDRC &= ~(0x0F);
  DDRD &= ~(0xF0);
  asm("nop\nnop\nnop");
  //read value
  res = (PIND & 0xF0) | (PINC & 0x0F);
  //set 1 - (output) C0-3, D4-7
  DDRC |= 0x0F;
  DDRD |= 0xF0;
  //set BC1 to 0 (inactive)
  PORTB &= ~(0x01);
  return res;
}

void resetAY(){
  //set 1 - (output) C0-3(A0-A3 as D0-D3), D4-7(D4-D7 as D4-D7)
  DDRC |= 0x0F;
  DDRD |= 0xF0;
  //set 1 - (output) B0-1 B0(D8 as BC1), B1(D9 as BDIR)
  DDRB |= 0x03;
  //set inactive
  PORTB &= ~(0x03);
  //set 1 - (output) D2 (D2 as AY_RESET)
  DDRD |= 0x04;
  //set AY reset pin to LOW (reset AY);
  PORTD &= ~(0x04);
  delay(100);
  //set AY reset pin to LOW
  PORTD |= 0x04;
  delay(100);
  //clear all AY registers
  for (int i=0;i<16;i++) ay_write(i,0);
}

void oledEQ() {
  if (!pause) {
    for (uint8_t i=0;i<96;i++) {
      if(bufEQcl[i]) {
        oled.fastLineV(i+EQ_SHIFT_H, EQ_SHIFT_V, EQ_SHIFT_V-16, OLED_CLEAR);
        bufEQcl[i] = false;
      }
      if(bufEQ[i] != 0){
        oled.fastLineV(i+EQ_SHIFT_H, EQ_SHIFT_V, (bufEQ[i] > 16) ? EQ_SHIFT_V-16 : EQ_SHIFT_V-bufEQ[i], OLED_FILL);
        bufEQ[i]--;
        bufEQcl[i] = true;
      }
    }
  }
  if(!uart) oled.rect(EQ_SHIFT_H, EQ_SHIFT_V+7, map(sz, 0, fileSize, EQ_SHIFT_H, EQ_SHIFT_H+96), EQ_SHIFT_V+5);
}

void readEQ(uint8_t reg, uint8_t val, boolean ay = false) {
	uint16_t tmpA=0, tmpB=0, tmpC=0;
	uint16_t regEnv= 0, indxEnv= 0;
	uint8_t regNoise= 0, reg13= 0;
	uint8_t regMix= 0xFF;

  if(!ay) {
    switch (reg) {
      case 0:
        tmpA |= val;
      break;
      case 1:
        tmpA = (val & 0x0F)<<8;
      break;
      case 2:
        tmpB |= val;
      break;
      case 3:
        tmpB = (val & 0x0F)<<8;
      break;
      case 4:
        tmpC |= val;
      break;
      case 5:
        tmpC = (val & 0x0F)<<8;
      break;
      case 6:
        regNoise = val & 0x1F;
      break;
      case 7:
        regMix = val;
      break;
      case 8:
        tA = (val & 0x1F);
      break;
      case 9:
        tB = (val & 0x1F);
      break;
      case 10:
        tC= (val & 0x1F);
      break;
      case 11:
        regEnv |= val;
      break;
      case 12:
        regEnv= val<<8;
      break;
      case 13:
        reg13= val;
      break;
      default:
      break;
    }
  } else {
    tmpA= (ay_read(1)& 0x0F)<<8;
    tmpA|= ay_read(0);
    
    tmpB= (ay_read(3)& 0x0F)<<8;
    tmpB|= ay_read(2);
    
    tmpC= (ay_read(5)& 0x0F)<<8;
    tmpC|= ay_read(4);
    
    reg13= ay_read(13);
    
    regEnv= ay_read(12)<<8;
    regEnv|= ay_read(11);
    
    regNoise= ay_read(6) & 0x1F;
    regMix= ay_read(7);
  
    tA= (ay_read(8) & 0x1F);
    tB= (ay_read(9) & 0x1F);
    tC= (ay_read(10) & 0x1F);
  }
	
	for (uint8_t i = 0; i<96;i++) {
		if(FreqAY[i]>= tmpA && FreqAY[i+1]< tmpA) A= i;
		if(FreqAY[i]>= tmpB && FreqAY[i+1]< tmpB) B= i;
		if(FreqAY[i]>= tmpC && FreqAY[i+1]< tmpC) C= i;
		if(FreqAY[i]>= regEnv*reg13 && FreqAY[i+1]< regEnv*reg13) indxEnv= i;
	}
  
	if((tA & 16) == 0) bufEQ[A]|= tA;
  else bufEQ[indxEnv]|= tA & 0x0F;

	if((regMix & 8) == 0) bufEQ[A]|= regNoise/2;

	if((tB & 16) == 0) bufEQ[B]|= tB;
	else bufEQ[indxEnv]|= tB & 0x0F;
	
	if((regMix & 16) == 0) bufEQ[B]|= regNoise/2;

	if((tC & 16) == 0) bufEQ[C]|= tC;	
	else bufEQ[indxEnv]|= tC & 0x0F;
		
	if((regMix & 32) == 0) bufEQ[C]|= regNoise/2;
}

void pauseAY() {
  ay_write(8,0);
  ay_write(9,0);
  ay_write(10,0);
}

void secCount() {
  timerB++;
  if (timerB == 50) {
    sec++;
    timerB = 0;
    secChange = true;
  }
}

void playFinish() {
  playFinished = true;
  for (int i=0;i<16;i++) ay_write(i,0);
}

void playAYyrg() {
  // send diff registers from current frame
  for (byte reg = 0; reg < 14; reg++) {
    if (reg == 13 && playBuf[playPos*16+reg]==255) break;
    if (reg == 13 || regBuf[reg] != playBuf[playPos*16+reg]) {
      ay_write(reg, playBuf[playPos*16+reg]);
      readEQ(reg, playBuf[playPos*16+reg], AY_EQ);
    }
  }
  memcpy(regBuf,&playBuf[playPos*16],14);
  playPos++;
  secCount();
  if (!fp.available() && playPos >= fillPos) {
    playFinish();
  }
  if (playPos >= fillPos) {playPos = 0; fillPerm = true;}
}

void playAYrsf() {
  if (skipCnt>0){
    skipCnt--;
  } else {
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

    boolean ok = false;
    int p = playPos;
    while (fillSz>0){
      byte b = playBuf[p];
      p++; if (p==bufSize) p=0;
      fillSz--;
      
      if (b==0xFF){ ok = true; break; }
      if (b==0xFE){ 
        if (fillSz>0){
          skipCnt = playBuf[p]-1;
          p++; if (p==bufSize) p=0;
          fillSz--;

          ok = true; 
          break; 
        }
      }
      if (b==0xFD){ 
        ok = true; 
        playFinish();
        break; 
      }
      if (b<=252){
        byte mask1, mask2;
        if (fillSz>0){
          mask2 = b;
          mask1 = playBuf[p];
          p++; if (p==bufSize) p=0;
          fillSz--;
          byte regg = 0;
          while ( mask1 != 0) {
            if (mask1 & 1) {
              ay_write(regg,playBuf[p]);
              readEQ(regg,playBuf[p], AY_EQ);
              p++; if (p==bufSize) p=0;
              fillSz--;
            }
            mask1 >>= 1;
            regg++;
          }
          regg = 8;
          while ( mask2 != 0) {
            if (mask2 & 1) {
              ay_write(regg,playBuf[p]);
              readEQ(regg,playBuf[p], AY_EQ);
              p++; if (p==bufSize) p=0;
              fillSz--;
            }
            mask2 >>= 1;
            regg++;
          }
        }
        ok = true;
        break;
      }
    } // while (fillSz>0)
    
    if (ok){
      playPos = p;
    }
  }
  secCount();
}

void playAYpsg() {
  if (skipCnt>0){
    skipCnt--;
  } else {
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

    boolean ok = false;
    int p = playPos;
    while (fillSz>0){
      byte b = playBuf[p];
      p++; if (p==bufSize) p=0;
      fillSz--;
      
      if (b==0xFF){ ok = true; break; }
      if (b==0xFD){ 
        ok = true; 
        playFinish();
        break; 
      }
      if (b==0xFE){ 
        if (fillSz>0){
          skipCnt = playBuf[p]*4-1; // one tact is 20ms, it is necessary to skip times 80 ms minus this tact (20ms)
          p++; if (p==bufSize) p=0;
          fillSz--;
          
          //skipCnt = 4*skipCnt;
          ok = true; 
          break; 
        }
      }
      if (b<=252){
        if (fillSz>0){
          byte v = playBuf[p];
          p++; if (p==bufSize) p=0;
          fillSz--;
          
          if (b<16) {
            ay_write(b,v);
            readEQ(b,v, AY_EQ);
          }
        } 
      }
    } // while (fillSz>0)
    
    if (ok){
      playPos = p;
    }
  }
  secCount();
}

// AY TIMER INTERRUPT HANDLER
ISR(TIMER1_COMPA_vect) {
  if(writeFlag && !fForward && !pause && play) {
    switch(file_type) {
      case 1:
        playAYyrg();
      break;
      case 2:
        playAYrsf();
      break;
      case 3:
        playAYpsg();
      break;
    }
  }
  if(pause || !play) pauseAY();
  if(fForward && play && !pause) {
    switch(file_type) {
      case 1:
        playAYyrg();
      break;
      case 2:
        playAYrsf();
      break;
      case 3:
        playAYpsg();
      break;
    }
  }
  writeFlag = !writeFlag;
}

// SERIAL INTERRUPT HANDLER
ISR(USART_RX_vect) {
  byte r = UDR0;
  if (bit_is_clear(UCSR0A,FE0)) {
    if(reg == false) {
      if(r <= 15) {
        reg_num = r;
        reg = true;
      }
    } else {
      ay_write(reg_num, r);
      readEQ(reg_num,r, AY_EQ);
      reg = false;
    }
  }
}
