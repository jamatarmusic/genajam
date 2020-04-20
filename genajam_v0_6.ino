// GENajam v0.6 - JAMATAR 2020
// --------------------
// This is a front end for Litte-scale's GENMDM module for Mega Drive
// Currently for: Arduino MEGA 2640 or Leonardo
// SD card hooks up via the ICSP header pins

#include <MIDI.h>  // Midi Library
#include <SPI.h>
#include "SdFat.h"
#include "FreeStack.h"
#include <LiquidCrystal.h>

//-------------------------------------------
//LCD pin to Arduino
LiquidCrystal lcd(8, 9, 4, 5, 6, 7);

uint8_t lcd_key = 0;
#define btnRIGHT  0
#define btnUP     1
#define btnDOWN   2
#define btnLEFT   3
#define btnSELECT 4
#define btnNONE   5

byte emojichannel[] = {
  B01110,
  B01000,
  B01110,
  B00000,
  B01010,
  B01110,
  B01010,
  B00000
};

byte emojiprogram[] = {
  B11100,
  B10100,
  B11100,
  B10000,
  B00111,
  B00101,
  B00110,
  B00101
};


byte emojipoly[] = {
  B11100,
  B10100,
  B11100,
  B10000,
  B00101,
  B10101,
  B10010,
  B11010
};


//debouncing

unsigned long buttonpushed = 0;      // when a button is pushed, mark what millis it was pushed
const uint16_t debouncedelay = 200;   //the debounce time which user sets prior to run
unsigned long messagestart = 0; // when a message starts, mark what millis it displayed at
const uint16_t messagedelay = 1000; // how long to display messages for
uint8_t refreshscreen = 0; // trigger refresh of screen but only once

//-------------------------------------------

// SD card chip select pin.
const uint8_t SD_CS_PIN = 3;

SdFat sd;
SdFile tfifile;
SdFile dirFile;

const uint8_t MaxNumberOfChars = 17; // only show 16 characters coz LCD

// Number of files found.
uint16_t n = 0;

// How many files to handle
const uint16_t nMax = 64;

// Position of file's directory entry.
uint16_t dirIndex[nMax];

//----------------------------------------------

// Create an instance of the library with default name, serial port and settings
MIDI_CREATE_DEFAULT_INSTANCE();

// start position of each channel file cursor
char tfifilenumber[6] = {0, 0, 0, 0, 0, 0};

//set the initial midi channel
uint8_t tfichannel=1;

// switch between settings
uint8_t mode=1;
// 1) mono presets
// 2) mono fm settings
// 3) poly presets
// 4) poly fm settings

// polyphony settings
uint8_t polynote[6] = {0, 0, 0, 0, 0, 0}; // what note is in each channel
bool polyon[6] = {0, 0, 0, 0, 0, 0}; // what channels have voices playing
bool sustainon[6] = {0, 0, 0, 0, 0, 0}; // what channels are sustained
bool noteheld[6] = {0, 0, 0, 0, 0, 0}; // what notes are currently held down
uint8_t lowestnote = 0; // don't steal the lowest note
bool sustain = 0; // is the pedal on

// fm parameter screen navigation
uint8_t fmscreen=1;

// global variable for storing FM settings for each channel
uint8_t fmsettings[6][50];
uint8_t lfospeed=64;
uint8_t polypan=64;
uint8_t polyvoicenum=6;

uint8_t potPin1 = A1;
uint8_t potPin2 = A2;
uint8_t potPin3 = A3;
uint8_t potPin4 = A4;

uint8_t prevpotvalue[4]; // recorded last potentiometer values

void setup()
{
    
    //setup the input pots
    pinMode(potPin1, INPUT);
    pinMode(potPin2, INPUT);
    pinMode(potPin3, INPUT);
    pinMode(potPin4, INPUT);

    //find out where the pots are
    prevpotvalue[0] = analogRead(potPin1)>>3;
    prevpotvalue[1] = analogRead(potPin2)>>3;
    prevpotvalue[2] = analogRead(potPin3)>>3;
    prevpotvalue[3] = analogRead(potPin4)>>3;
    
    lcd.begin(16, 2);

    lcd.createChar(0, emojichannel);
    lcd.createChar(1, emojiprogram);
    lcd.createChar(2, emojipoly);


    lcd.clear();
    lcd.setCursor(0,0);
    lcd.print("JamaGEN START!");
    lcd.setCursor(0,1);
    lcd.print("version 0.6");

    delay(500);
  
  
  // Serial.begin(9600); // open the serial port at 9600 bps for debug
  // while (!Serial) {}
  // delay(1000);

    if (!sd.begin(SD_CS_PIN, SD_SCK_MHZ(50))) {
      lcd.clear();
      lcd.setCursor(0,0);
      lcd.print(F("ERROR:"));
      lcd.setCursor(0,1);
      lcd.print(F("CANNOT FIND SD"));
      sd.initErrorHalt();
    }

  MIDI.begin(MIDI_CHANNEL_OMNI); // read all channels
  MIDI.turnThruOff(); // turn off soft midi thru

  MIDI.setHandleNoteOn(MyHandleNoteOn);
  MIDI.setHandleNoteOff(MyHandleNoteOff);
  MIDI.setHandleControlChange(MyHandleCC);
  MIDI.setHandlePitchBend(MyHandlePitchbend);

  // ========================================================= SET REGION HERE

  MIDI.sendControlChange(83,1,1); // set GENMDM to PAL
  //MIDI.sendControlChange(83,75,1); // set GENMDM to NTSC

  // ========================================================= SET REGION HERE

 // List files in root directory and get max file number
  if (!dirFile.open("/", O_RDONLY)) {
      lcd.setCursor(0,0);
      lcd.print(F("ERROR:"));
      lcd.setCursor(0,1);
      lcd.print(F("NO ROOT DIR"));
      sd.errorHalt(F("open root failed"));
  }
  while (n < nMax && tfifile.openNext(&dirFile, O_RDONLY)) {

    // Skip directories and hidden files.
    if (!tfifile.isSubDir() && !tfifile.isHidden()) {

      // Save dirIndex of file in directory.
      dirIndex[n] = tfifile.dirIndex();

      // Count the files
      n++;
    }
    tfifile.close();
  }

  delay(700);

  //initialise all channels
  for (int i = 6; i > 0; i--)
  {
  tfichannel=i;
  tfiselect();
  }

  
} // void setup

void loop()
{

MIDI.read();

  lcd_key = read_LCD_buttons();

  modechangemessage(); // if the mode has just been changed, show message temporarily

  // set the mode depending on cycle of mode button
  
  switch(mode) {
  
  //======================================================= MODE 1
  case 1:
  {
      switch (lcd_key) // depending on which button was pushed, we perform an action
      {
      case btnRIGHT:
      {
      tfifilenumber[tfichannel-1] = tfifilenumber[tfichannel-1]+1;
      if(tfifilenumber[tfichannel-1]==(n)){ // if max files exceeded, loop back to start
      tfifilenumber[tfichannel-1]=0;
      }   
      tfiselect();
      break;
      }
      
      case btnLEFT:
      {
      tfifilenumber[tfichannel-1] = tfifilenumber[tfichannel-1]-1;
      if(tfifilenumber[tfichannel-1]==-1){ // if min files exceeded, loop back to end
      tfifilenumber[tfichannel-1]=n-1;
      }
      tfiselect();
      break;
      }
      
      case btnUP:
      {
      tfichannel=tfichannel-1;
      if(tfichannel==(0)){ // if max channels reached, loop around
      tfichannel=6;
      }
      channelselect();
      break;
      }
      
      case btnDOWN:
      {
      tfichannel=tfichannel+1;
      if(tfichannel==(7)){ // if max channels reached, loop around
      tfichannel=1;
      }
      channelselect();
      break;
      }
      
      case btnSELECT:
      {
      modechange();
      break;
      }
      
      } 
      
  break;  
  }
  
  
  //======================================================= MODE 2
  case 2:
  {
      
      switch (lcd_key) // depending on which button was pushed, we perform an action
      {
      case btnRIGHT:
      {
      fmscreen = fmscreen+1;
      if(fmscreen==14) fmscreen=1;   
      fmparamdisplay();
      break;
      }
      
      case btnLEFT:
      {
      fmscreen = fmscreen-1;
      if(fmscreen==0) fmscreen=13;   
      fmparamdisplay();
      break;
      }
      
      case btnUP:
      {
      tfichannel=tfichannel-1;
      if(tfichannel==(0)){ // if max channels reached, loop around
      tfichannel=6;
      }
      fmparamdisplay();
      break;
      }
      
      case btnDOWN:
      {
      tfichannel=tfichannel+1;
      if(tfichannel==(7)){ // if max channels reached, loop around
      tfichannel=1;
      }
      fmparamdisplay();
      break;
      }
      
      case btnSELECT:
      {
      modechange();
      break;
      }
      
      }

  operatorparamdisplay(); // check in with the pots to see if they've been moved, update the display and send CC
  
  break;  
  }


  //======================================================= MODE 3
  case 3:
  {
      switch (lcd_key) // depending on which button was pushed, we perform an action
      {
      case btnRIGHT:
      {
      tfichannel=1;
      tfifilenumber[tfichannel-1] = tfifilenumber[tfichannel-1]+1;
      if(tfifilenumber[tfichannel-1]==(n)){ // if max files exceeded, loop back to start
      tfifilenumber[tfichannel-1]=0;
      }
      for (int i = 1; i <= 5; i++) {
        tfifilenumber[i] = tfifilenumber[0];
      }
      for (int i = 1; i <= 6; i++) {
        tfichannel=i;
        tfiselect();
      }      
      break;
      }
      
      case btnLEFT:
      {
      tfichannel=1;
      tfifilenumber[tfichannel-1] = tfifilenumber[tfichannel-1]-1;
      if(tfifilenumber[tfichannel-1]==-1){ // if min files exceeded, loop back to end
      tfifilenumber[tfichannel-1]=n-1;
      }
      for (int i = 1; i <= 5; i++) {
        tfifilenumber[i] = tfifilenumber[0];
      }
      for (int i = 1; i <= 6; i++) {
        tfichannel=i;
        tfiselect();
      }    
      break;
      }
      
      case btnSELECT:
      {  
      modechange();
      break;
      }
      
      } 
      
  break;  
  }

  //======================================================= MODE 4
  case 4:
  {
      
      switch (lcd_key) // depending on which button was pushed, we perform an action
      {
      case btnRIGHT:
      {
      fmscreen = fmscreen+1;
      if(fmscreen==14) fmscreen=1;   
      fmparamdisplay();
      break;
      }
      
      case btnLEFT:
      {
      fmscreen = fmscreen-1;
      if(fmscreen==0) fmscreen=13;   
      fmparamdisplay();
      break;
      }
      
      case btnSELECT:
      {
      modechange();
      break;
      }
      
      }

  operatorparamdisplay(); // check in with the pots to see if they've been moved, update the display and send CC
  
  break;  
  }
  
  } // end mode check

  
} // void loop


int read_LCD_buttons() // function for reading the buttons
{
  uint16_t adc_key_in = 0;
  adc_key_in = analogRead(0);      // read the value from the sensor
  
  if (adc_key_in > 1000) {
  return btnNONE;
  }
  
    if ((millis() - buttonpushed) > debouncedelay) {    // only register a new button push if no button has been pushed in debouncedelay millis

      // you're going to want to customise this depending on the LCD sheild you have
      // check your button min / max analog read values by running and LCD sheild keypad test

      // 0 - 57 (0)
      if (adc_key_in > 0 && adc_key_in < 88) {
      buttonpushed = millis();  
      return btnRIGHT;
      }

      // 97 - 157 (99)
      if (adc_key_in > 98 && adc_key_in < 103) {
      buttonpushed = millis();  
      return btnUP;
      }

      // 254 - 271 (255)
      if (adc_key_in > 254 && adc_key_in < 258) {
      buttonpushed = millis();  
      return btnDOWN;
      }

      // 404 - 423 (408)
      if (adc_key_in > 404 && adc_key_in < 418) {
      buttonpushed = millis();  
      return btnLEFT;
      }

      // 629 - 647 (638)
      if (adc_key_in > 636 && adc_key_in < 642) {
      buttonpushed = millis();  
      return btnSELECT;
      }
    
    }

  return btnNONE;  // when all others fail, return this...

}

void modechange() // when the mode button is changed, cycle the modes
{
  mode++;
  if(mode > 4) mode = 1; // loop mode

  switch(mode){
    case 1:
    {
      messagestart = millis();
      lcd.clear();
      lcd.setCursor(0,0);
      lcd.print("1 MONO | Preset");
      refreshscreen=1;
      break;
    }
    
    case 2:
    {
      messagestart = millis();
      lcd.clear();
      lcd.setCursor(0,0);
      lcd.print("2 MONO | FM Edit");
      // find out where the pots are
      prevpotvalue[0] = analogRead(potPin1)>>3;
      prevpotvalue[1] = analogRead(potPin2)>>3;
      prevpotvalue[2] = analogRead(potPin3)>>3;
      prevpotvalue[3] = analogRead(potPin4)>>3;

      
      refreshscreen=1;
      break;
    }
    
    case 3:
    {
      messagestart = millis();
      lcd.clear();
      lcd.setCursor(0,0);
      lcd.print("3 POLY | Preset");
      refreshscreen=1;
      break;
    }

    case 4:
    {
      messagestart = millis();
      lcd.clear();
      lcd.setCursor(0,0);
      lcd.print("4 POLY | FM Edit");
      // find out where the pots are
      prevpotvalue[0] = analogRead(potPin1)>>3;
      prevpotvalue[1] = analogRead(potPin2)>>3;
      prevpotvalue[2] = analogRead(potPin3)>>3;
      prevpotvalue[3] = analogRead(potPin4)>>3;
      refreshscreen=1;
      break;
    }
    
  } // switch
}

void modechangemessage() // display temporary message
{
// if a mode switch message has been displayed for long enough, refresh the screen
  
if ((millis() - messagestart) > messagedelay && refreshscreen == 1) { 
  switch(mode){
    case 1:
    {
      channelselect(); // just reload the channel info for the current channel
      refreshscreen=0;
      break;
    }

    case 2:
    {
      fmparamdisplay();
      refreshscreen=0;
      break;
    }

    case 3:
    {
      channelselect(); // just reload the channel info for the current channel
      refreshscreen=0;
      break;
    }
    
    case 4:
    {
      fmparamdisplay();
      refreshscreen=0;
      break;
    }      
  } // end mode check
} // end message refresh
}

void tfiselect() //load a tfi , send the midi, update screen
{

    if (!tfifile.open(&dirFile, dirIndex[tfifilenumber[tfichannel-1]], O_RDONLY)) {
      lcd.clear();
      lcd.setCursor(0,0);
      lcd.print("ERROR:");
      lcd.setCursor(0,1);
      lcd.print("CANNOT READ TFI ");
      sd.errorHalt(F("open"));
    }

    // open TFI file and read/send contents =================

    // variables for pulling values from TFI files and putting them in an array
    int tfiarray[42];
    int filetoarray = 0; // read the file from the start
    // read from the file until there's nothing else in it:
    while (tfifile.available()) {
      tfiarray[filetoarray]=tfifile.read(), DEC; // read the byte data, convert it to DEC
      filetoarray++;
    } 

    
    tfisend(tfiarray, tfichannel);

     //get filename
    char tfifilename[MaxNumberOfChars + 1];
    tfifile.getName(tfifilename, MaxNumberOfChars);
    tfifilename[MaxNumberOfChars]=0; //ensure  termination
    
    //show filename on screen
    lcd.clear();
    lcd.setCursor(0,0);
    if (mode==3) {  
      lcd.write(byte(2));
      lcd.print(F(" "));     
    }
    else {
      lcd.write(byte(0));
      lcd.print(tfichannel);  
    }
    lcd.print(F(" "));
    lcd.write(byte(1));
    printzeros(tfifilenumber[tfichannel-1]+1);
    lcd.print(tfifilenumber[tfichannel-1]+1);
    lcd.print(F("/"));
    printzeros(n);
    lcd.print(n);
    lcd.setCursor(0,1);
    lcd.print(tfifilename);

   
    // TFI file closed ================
    
  tfifile.close();
  
}


void channelselect() //select a new channel, display current tfi on screen
{
    if (!tfifile.open(&dirFile, dirIndex[tfifilenumber[tfichannel-1]], O_RDONLY)) {
      lcd.setCursor(0,0);
      lcd.print("ERROR:");
      lcd.setCursor(0,1);
      lcd.print("CANNOT READ TFI");
      sd.errorHalt(F("open"));
  }

     //get filename
    char tfifilename[MaxNumberOfChars + 1];
    tfifile.getName(tfifilename, MaxNumberOfChars);
    tfifilename[MaxNumberOfChars]=0; //ensure  termination

    //show filename on screen
    lcd.clear();
    lcd.setCursor(0,0);
    if (mode==3) {  
      lcd.write(byte(2));
      lcd.print(F(" "));     
    }
    else {
      lcd.write(byte(0));
      lcd.print(tfichannel);  
    }
    lcd.print(F(" "));
    lcd.write(byte(1));
    printzeros(tfifilenumber[tfichannel-1]+1);
    lcd.print(tfifilenumber[tfichannel-1]+1);
    lcd.print(F("/"));
    printzeros(n);
    lcd.print(n);
    lcd.setCursor(0,1);
    lcd.print(tfifilename);
   
    // TFI file closed ================
    
  tfifile.close();
  
}

void tfisend(int opnarray[42], int sendchannel)
{
  
    //send all TFI data to appropriate CCs
      
    MIDI.sendControlChange(14,opnarray[0]*16,sendchannel); //Algorithm 
    MIDI.sendControlChange(15,opnarray[1]*16,sendchannel); //Feedback

    MIDI.sendControlChange(20,opnarray[2]*8,sendchannel);  //OP1 Multiplier
    MIDI.sendControlChange(21,opnarray[12]*8,sendchannel); //OP2 Multiplier
    MIDI.sendControlChange(22,opnarray[22]*8,sendchannel); //OP3 Multiplier
    MIDI.sendControlChange(23,opnarray[32]*8,sendchannel); //OP4 Multiplier

    MIDI.sendControlChange(24,opnarray[3]*32,sendchannel);  //OP1 Detune
    MIDI.sendControlChange(25,opnarray[13]*32,sendchannel); //OP2 Detune
    MIDI.sendControlChange(26,opnarray[23]*32,sendchannel); //OP3 Detune
    MIDI.sendControlChange(27,opnarray[33]*32,sendchannel); //OP4 Detune

    MIDI.sendControlChange(16,127-opnarray[4],sendchannel);  //OP1 Total Level
    MIDI.sendControlChange(17,127-opnarray[14],sendchannel); //OP2 Total Level
    MIDI.sendControlChange(18,127-opnarray[24],sendchannel); //OP3 Total Level
    MIDI.sendControlChange(19,127-opnarray[34],sendchannel); //OP4 Total Level

    MIDI.sendControlChange(39,opnarray[5]*32,sendchannel);  //OP1 Rate Scaling
    MIDI.sendControlChange(40,opnarray[15]*32,sendchannel); //OP2 Rate Scaling
    MIDI.sendControlChange(41,opnarray[25]*32,sendchannel); //OP3 Rate Scaling
    MIDI.sendControlChange(42,opnarray[35]*32,sendchannel); //OP4 Rate Scaling

    MIDI.sendControlChange(43,opnarray[6]*4,sendchannel);  //OP1 Attack Rate
    MIDI.sendControlChange(44,opnarray[16]*4,sendchannel); //OP2 Attack Rate
    MIDI.sendControlChange(45,opnarray[26]*4,sendchannel); //OP3 Attack Rate
    MIDI.sendControlChange(46,opnarray[36]*4,sendchannel); //OP4 Attack Rate

    MIDI.sendControlChange(47,opnarray[7]*4,sendchannel);  //OP1 1st Decay Rate
    MIDI.sendControlChange(48,opnarray[17]*4,sendchannel); //OP2 1st Decay Rate
    MIDI.sendControlChange(49,opnarray[27]*4,sendchannel); //OP3 1st Decay Rate
    MIDI.sendControlChange(50,opnarray[37]*4,sendchannel); //OP4 1st Decay Rate

    MIDI.sendControlChange(51,opnarray[8]*8,sendchannel);  //OP1 2nd Decay Rate
    MIDI.sendControlChange(52,opnarray[18]*8,sendchannel); //OP2 2nd Decay Rate
    MIDI.sendControlChange(53,opnarray[28]*8,sendchannel); //OP3 2nd Decay Rate
    MIDI.sendControlChange(54,opnarray[38]*8,sendchannel); //OP4 2nd Decay Rate

    MIDI.sendControlChange(59,opnarray[9]*8,sendchannel);  //OP1 Release Rate
    MIDI.sendControlChange(60,opnarray[19]*8,sendchannel); //OP2 Release Rate
    MIDI.sendControlChange(61,opnarray[29]*8,sendchannel); //OP3 Release Rate
    MIDI.sendControlChange(62,opnarray[39]*8,sendchannel); //OP4 Release Rate

    MIDI.sendControlChange(55,opnarray[10]*8,sendchannel); //OP1 2nd Total Level
    MIDI.sendControlChange(56,opnarray[20]*8,sendchannel); //OP2 2nd Total Level
    MIDI.sendControlChange(57,opnarray[30]*8,sendchannel); //OP3 2nd Total Level
    MIDI.sendControlChange(58,opnarray[40]*8,sendchannel); //OP4 2nd Total Level
    
    MIDI.sendControlChange(90,opnarray[11]*8,sendchannel); //OP1 SSG-EG
    MIDI.sendControlChange(91,opnarray[21]*8,sendchannel); //OP2 SSG-EG   
    MIDI.sendControlChange(92,opnarray[31]*8,sendchannel); //OP3 SSG-EG  
    MIDI.sendControlChange(93,opnarray[41]*8,sendchannel); //OP4 SSG-EG

    MIDI.sendControlChange(75,90,sendchannel); //FM Level // A good level of mod
    MIDI.sendControlChange(76,127,sendchannel); //AM Level // Maxed amp mod
    MIDI.sendControlChange(77,127,sendchannel); //Stereo (centered)

    MIDI.sendControlChange(70,0,sendchannel); //OP1 Amplitude Modulation (off)
    MIDI.sendControlChange(71,0,sendchannel); //OP2 Amplitude Modulation (off)
    MIDI.sendControlChange(72,0,sendchannel); //OP3 Amplitude Modulation (off)
    MIDI.sendControlChange(73,0,sendchannel); //OP4 Amplitude Modulation (off)

    // Dump TFI settings into the global settings array
    
    fmsettings[tfichannel-1][0] = opnarray[0]*16; //Algorithm 
    fmsettings[tfichannel-1][1] = opnarray[1]*16; //Feedback

    fmsettings[tfichannel-1][2] = opnarray[2]*8;   //OP1 Multiplier
    fmsettings[tfichannel-1][12] = opnarray[12]*8; //OP2 Multiplier
    fmsettings[tfichannel-1][22] = opnarray[22]*8; //OP3 Multiplier
    fmsettings[tfichannel-1][32] = opnarray[32]*8; //OP4 Multiplier

    fmsettings[tfichannel-1][3] = opnarray[3]*32;   //OP1 Detune
    fmsettings[tfichannel-1][13] = opnarray[13]*32; //OP2 Detune
    fmsettings[tfichannel-1][23] = opnarray[23]*32; //OP3 Detune
    fmsettings[tfichannel-1][33] = opnarray[33]*32; //OP4 Detune

    fmsettings[tfichannel-1][4] = 127-opnarray[4];   //OP1 Total Level
    fmsettings[tfichannel-1][14] = 127-opnarray[14]; //OP2 Total Level
    fmsettings[tfichannel-1][24] = 127-opnarray[24]; //OP3 Total Level
    fmsettings[tfichannel-1][34] = 127-opnarray[34]; //OP4 Total Level

    fmsettings[tfichannel-1][5] = opnarray[5]*32;   //OP1 Rate Scaling
    fmsettings[tfichannel-1][15] = opnarray[15]*32; //OP2 Rate Scaling
    fmsettings[tfichannel-1][25] = opnarray[25]*32; //OP3 Rate Scaling
    fmsettings[tfichannel-1][35] = opnarray[35]*32; //OP4 Rate Scaling

    fmsettings[tfichannel-1][6] = opnarray[6]*4;   //OP1 Attack Rate
    fmsettings[tfichannel-1][16] = opnarray[16]*4; //OP2 Attack Rate
    fmsettings[tfichannel-1][26] = opnarray[26]*4; //OP3 Attack Rate
    fmsettings[tfichannel-1][36] = opnarray[36]*4; //OP4 Attack Rate

    fmsettings[tfichannel-1][7] = opnarray[7]*4;   //OP1 1st Decay Rate
    fmsettings[tfichannel-1][17] = opnarray[17]*4; //OP2 1st Decay Rate
    fmsettings[tfichannel-1][27] = opnarray[27]*4; //OP3 1st Decay Rate
    fmsettings[tfichannel-1][37] = opnarray[37]*4; //OP4 1st Decay Rate

    fmsettings[tfichannel-1][8] = opnarray[8]*8;   //OP1 2nd Decay Rate
    fmsettings[tfichannel-1][18] = opnarray[18]*8; //OP2 2nd Decay Rate
    fmsettings[tfichannel-1][28] = opnarray[28]*8; //OP3 2nd Decay Rate
    fmsettings[tfichannel-1][38] = opnarray[38]*8; //OP4 2nd Decay Rate

    fmsettings[tfichannel-1][9] = opnarray[9]*8;   //OP1 Release Rate
    fmsettings[tfichannel-1][19] = opnarray[19]*8; //OP2 Release Rate
    fmsettings[tfichannel-1][29] = opnarray[29]*8; //OP3 Release Rate
    fmsettings[tfichannel-1][39] = opnarray[39]*8; //OP4 Release Rate

    fmsettings[tfichannel-1][10] = 127-(opnarray[10]*8); //OP1 2nd Total Level
    fmsettings[tfichannel-1][20] = 127-(opnarray[20]*8); //OP2 2nd Total Level
    fmsettings[tfichannel-1][30] = 127-(opnarray[30]*8); //OP3 2nd Total Level
    fmsettings[tfichannel-1][40] = 127-(opnarray[40]*8); //OP4 2nd Total Level

    fmsettings[tfichannel-1][11] = opnarray[11]*8; //OP1 SSG-EG
    fmsettings[tfichannel-1][21] = opnarray[21]*8; //OP2 SSG-EG
    fmsettings[tfichannel-1][31] = opnarray[31]*8; //OP3 SSG-EG
    fmsettings[tfichannel-1][41] = opnarray[41]*8; //OP4 SSG-EG
    
    fmsettings[tfichannel-1][42] = 90; //FM Level
    fmsettings[tfichannel-1][43] = 127; //AM Level
    fmsettings[tfichannel-1][44] = 127; //Stereo (centered)
    fmsettings[tfichannel-1][45] = 0; //OP1 Amplitude Modulation
    fmsettings[tfichannel-1][46] = 0; //OP2 Amplitude Modulation
    fmsettings[tfichannel-1][47] = 0; //OP3 Amplitude Modulation
    fmsettings[tfichannel-1][48] = 0; //OP4 Amplitude Modulation
    fmsettings[tfichannel-1][49] = 0; //Patch is unedited

    polypan = 0; // reset secret stereo mode
    
    /*
    // FOR TESTING CHANNEL
    MIDI.sendNoteOn(60, 127, sendchannel);
    delay(200);           
    MIDI.sendNoteOff(60, 0, sendchannel);  
    */
    
}

void fmparamdisplay()
{

  uint8_t i; // for holding array number

  lcd.clear();
  lcd.setCursor(0,0);

  if (mode==2) {
    lcd.write(byte(0));
    lcd.print(tfichannel);
    lcd.print(F(" ")); 
  }
  else
  {
    lcd.write(byte(2));
    lcd.print(F("  "));    
  }
  
  switch(fmscreen) {

    // Algorthm, Feedback, Pan
    case 1:
    {
      lcd.print(F("01:Alg FB Pan"));
      lcd.setCursor(5,1);
      i = fmsettings[tfichannel-1][0];
      printspaces(i);
      lcd.print(i);
      lcd.setCursor(9,1);
      i = fmsettings[tfichannel-1][1];
      printspaces(i);
      lcd.print(i);
      lcd.setCursor(13,1);
      i = fmsettings[tfichannel-1][44];
      printspaces(i);
      lcd.print(i);
      break;
    }

    // Total Level
    case 2:
    {
      lcd.print(F("02:TL"));
      lcd.setCursor(1,1);
      i = fmsettings[tfichannel-1][4];
      printspaces(i);
      lcd.print(i);
      lcd.setCursor(5,1);
      i = fmsettings[tfichannel-1][14];
      printspaces(i);
      lcd.print(i);
      lcd.setCursor(9,1);
      i = fmsettings[tfichannel-1][24];
      printspaces(i);
      lcd.print(i);
      lcd.setCursor(13,1);
      i = fmsettings[tfichannel-1][34];
      printspaces(i);
      lcd.print(i);
      break;
    }

    // Multiple
    case 3:
    {
      lcd.print(F("03:Multiple"));
      lcd.setCursor(1,1);
      i = fmsettings[tfichannel-1][2];
      printspaces(i);
      lcd.print(i);
      lcd.setCursor(5,1);
      i = fmsettings[tfichannel-1][12];
      printspaces(i);
      lcd.print(i);
      lcd.setCursor(9,1);
      i = fmsettings[tfichannel-1][22];
      printspaces(i);
      lcd.print(i);
      lcd.setCursor(13,1);
      i = fmsettings[tfichannel-1][32];
      printspaces(i);
      lcd.print(i);
      break;
    }

    // Rate Scaling
    case 4:
    {
      lcd.print(F("04:Rate Scale"));
      lcd.setCursor(1,1);
      i = fmsettings[tfichannel-1][5];
      printspaces(i);
      lcd.print(i);
      lcd.setCursor(5,1);
      i = fmsettings[tfichannel-1][15];
      printspaces(i);
      lcd.print(i);
      lcd.setCursor(9,1);
      i = fmsettings[tfichannel-1][25];
      printspaces(i);
      lcd.print(i);
      lcd.setCursor(13,1);
      i = fmsettings[tfichannel-1][35];
      printspaces(i);
      lcd.print(i);
      break;
    }

    // Detune
    case 5:
    {
      lcd.print(F("05:Detune"));
      lcd.setCursor(1,1);
      i = fmsettings[tfichannel-1][3];
      printspaces(i);
      lcd.print(i);
      lcd.setCursor(5,1);
      i = fmsettings[tfichannel-1][13];
      printspaces(i);
      lcd.print(i);
      lcd.setCursor(9,1);
      i = fmsettings[tfichannel-1][23];
      printspaces(i);
      lcd.print(i);
      lcd.setCursor(13,1);
      i = fmsettings[tfichannel-1][33];
      printspaces(i);
      lcd.print(i);
      break;
    }

    // Attack Rate
    case 6:
    {
      lcd.print(F("06:Attack"));
      lcd.setCursor(1,1);
      i = fmsettings[tfichannel-1][6];
      printspaces(i);
      lcd.print(i);
      lcd.setCursor(5,1);
      i = fmsettings[tfichannel-1][16];
      printspaces(i);
      lcd.print(i);
      lcd.setCursor(9,1);
      i = fmsettings[tfichannel-1][26];
      printspaces(i);
      lcd.print(i);
      lcd.setCursor(13,1);
      i = fmsettings[tfichannel-1][36];
      printspaces(i);
      lcd.print(i);
      break;
    }

    // Decay Rate 1
    case 7:
    {
      lcd.print(F("07:Decay 1"));
      lcd.setCursor(1,1);
      i = fmsettings[tfichannel-1][7];
      printspaces(i);
      lcd.print(i);
      lcd.setCursor(5,1);
      i = fmsettings[tfichannel-1][17];
      printspaces(i);
      lcd.print(i);
      lcd.setCursor(9,1);
      i = fmsettings[tfichannel-1][27];
      printspaces(i);
      lcd.print(i);
      lcd.setCursor(13,1);
      i = fmsettings[tfichannel-1][37];
      printspaces(i);
      lcd.print(i);
      break;
    }

    // Decay Rate 2
    case 8:
    {
      lcd.print(F("08:Decay 2"));
      lcd.setCursor(1,1);
      i = fmsettings[tfichannel-1][8];
      printspaces(i);
      lcd.print(i);
      lcd.setCursor(5,1);
      i = fmsettings[tfichannel-1][18];
      printspaces(i);
      lcd.print(i);
      lcd.setCursor(9,1);
      i = fmsettings[tfichannel-1][28];
      printspaces(i);
      lcd.print(i);
      lcd.setCursor(13,1);
      i = fmsettings[tfichannel-1][38];
      printspaces(i);
      lcd.print(i);
      break;
    }

    // Release Rate
    case 9:
    {
      lcd.print(F("09:Release"));
      lcd.setCursor(1,1);
      i = fmsettings[tfichannel-1][10];
      printspaces(i);
      lcd.print(i);
      lcd.setCursor(5,1);
      i = fmsettings[tfichannel-1][20];
      printspaces(i);
      lcd.print(i);
      lcd.setCursor(9,1);
      i = fmsettings[tfichannel-1][30];
      printspaces(i);
      lcd.print(i);
      lcd.setCursor(13,1);
      i = fmsettings[tfichannel-1][40];
      printspaces(i);
      lcd.print(i);
      break;
    }

    // Total Level 2
    case 10:
    {
      lcd.print(F("10:TL 2"));
      lcd.setCursor(1,1);
      i = fmsettings[tfichannel-1][9];
      printspaces(i);
      lcd.print(i);
      lcd.setCursor(5,1);
      i = fmsettings[tfichannel-1][19];
      printspaces(i);
      lcd.print(i);
      lcd.setCursor(9,1);
      i = fmsettings[tfichannel-1][29];
      printspaces(i);
      lcd.print(i);
      lcd.setCursor(13,1);
      i = fmsettings[tfichannel-1][39];
      printspaces(i);
      lcd.print(i);
      break;
    }

    // SSG-EG
    case 11:
    {
      lcd.print(F("11:SSG-EG"));
      lcd.setCursor(1,1);
      i = fmsettings[tfichannel-1][11];
      printspaces(i);
      lcd.print(i);
      lcd.setCursor(5,1);
      i = fmsettings[tfichannel-1][21];
      printspaces(i);
      lcd.print(i);
      lcd.setCursor(9,1);
      i = fmsettings[tfichannel-1][31];
      printspaces(i);
      lcd.print(i);
      lcd.setCursor(13,1);
      i = fmsettings[tfichannel-1][41];
      printspaces(i);
      lcd.print(i);
      break;
    }

    // Amp Mod
    case 12:
    {
      lcd.print(F("12:Amp Mod"));
      lcd.setCursor(1,1);
      i = fmsettings[tfichannel-1][45];
      printspaces(i);
      lcd.print(i);
      lcd.setCursor(5,1);
      i = fmsettings[tfichannel-1][46];
      printspaces(i);
      lcd.print(i);
      lcd.setCursor(9,1);
      i = fmsettings[tfichannel-1][47];
      printspaces(i);
      lcd.print(i);
      lcd.setCursor(13,1);
      i = fmsettings[tfichannel-1][48];
      printspaces(i);
      lcd.print(i);
      break;
    }

    // AM and FM Level
    case 13:
    {
      lcd.print(F("13:LFO/FM/AM"));
      lcd.setCursor(5,1);
      i = lfospeed;
      printspaces(i);
      lcd.print(i);
      lcd.setCursor(9,1);
      i = fmsettings[tfichannel-1][42];
      printspaces(i);
      lcd.print(i);
      lcd.setCursor(13,1);
      i = fmsettings[tfichannel-1][43];
      printspaces(i);
      lcd.print(i);
      break;
    }   
     
  } // end switch  
  
}

void operatorparamdisplay()
{

  MIDI.read();
  
  uint8_t currentpotvalue[4];
  char difference;
  bool flash = 0;
  
  // read the current pot values
  currentpotvalue[0] = analogRead(potPin1)>>3;
  currentpotvalue[1] = analogRead(potPin2)>>3;
  currentpotvalue[2] = analogRead(potPin3)>>3;
  currentpotvalue[3] = analogRead(potPin4)>>3;
  
  for (int i = 0; i <= 3; i++) {
    
    difference = prevpotvalue[i]-currentpotvalue[i];
    
    // only update the pots and values if the value has changed more than 'difference'
    if (difference > 1 || difference < -1)
    {
      MIDI.read();
      lcd.setCursor(((i+1)*4)-3,1);
      if (currentpotvalue[i]<100) lcd.print(F(" "));
      if (currentpotvalue[i]<10) lcd.print(F(" "));
      if (currentpotvalue[i]==1) currentpotvalue[i]=0; // flatten that shiz
      if (currentpotvalue[i]==126) currentpotvalue[i]=127; // max that shiz
      lcd.print(currentpotvalue[i]);
      prevpotvalue[i] = currentpotvalue[i];

      // send CC for either mono or poly mode
      if (mode == 2)
      {
        fmccsend(i, currentpotvalue[i]);  
      }
      else
      {
        for (int c = 1; c <=6; c++) {
          tfichannel=c;
          fmccsend(i, currentpotvalue[i]);
        }
      }
      
      
    }
    else
    {
      lcd.setCursor(((i+1)*4)-3,1); 
    }

  } // for loop

}

void fmccsend(byte potnumber, uint8_t potvalue)
{
    
    // grab the value from the pot that changes, update the array and send midi
    
    switch(fmscreen) {
      
    // Algorthm, Feedback, Pan
    case 1:
    {
      lcd.setCursor(1,1);
      lcd.print(F("   ")); // just blank out the unused pot display
      if (potnumber==1) {fmsettings[tfichannel-1][0] = potvalue; MIDI.sendControlChange(14,potvalue,tfichannel);} //Algorithm
      if (potnumber==2) {fmsettings[tfichannel-1][1] = potvalue; MIDI.sendControlChange(15,potvalue,tfichannel);} //Feedback
      if (potnumber==3) {fmsettings[tfichannel-1][44] = potvalue; MIDI.sendControlChange(77,potvalue,tfichannel);} //Pan
      break;
    }

    // Total Level
    case 2:
    {
      if (potnumber==0) {fmsettings[tfichannel-1][4] = potvalue; MIDI.sendControlChange(16,potvalue,tfichannel);}  //OP1 Total Level
      if (potnumber==1) {fmsettings[tfichannel-1][14] = potvalue; MIDI.sendControlChange(17,potvalue,tfichannel);} //OP2 Total Level
      if (potnumber==2) {fmsettings[tfichannel-1][24] = potvalue; MIDI.sendControlChange(18,potvalue,tfichannel);} //OP3 Total Level
      if (potnumber==3) {fmsettings[tfichannel-1][34] = potvalue; MIDI.sendControlChange(19,potvalue,tfichannel);} //OP4 Total Level
      break;
    }

    // Multiple
    case 3:
    {
      if (potnumber==0) {fmsettings[tfichannel-1][2] = potvalue; MIDI.sendControlChange(20,potvalue,tfichannel);}  //OP1 Multiplier
      if (potnumber==1) {fmsettings[tfichannel-1][12] = potvalue; MIDI.sendControlChange(21,potvalue,tfichannel);} //OP2 Multiplier
      if (potnumber==2) {fmsettings[tfichannel-1][22] = potvalue; MIDI.sendControlChange(22,potvalue,tfichannel);} //OP3 Multiplier
      if (potnumber==3) {fmsettings[tfichannel-1][32] = potvalue; MIDI.sendControlChange(23,potvalue,tfichannel);} //OP4 Multiplier
      break;
    }

    // Rate Scaling
    case 4:
    {
      if (potnumber==0) {fmsettings[tfichannel-1][5] = potvalue; MIDI.sendControlChange(39,potvalue,tfichannel);}  //OP1 Rate Scaling
      if (potnumber==1) {fmsettings[tfichannel-1][15] = potvalue; MIDI.sendControlChange(40,potvalue,tfichannel);} //OP2 Rate Scaling
      if (potnumber==2) {fmsettings[tfichannel-1][25] = potvalue; MIDI.sendControlChange(41,potvalue,tfichannel);} //OP3 Rate Scaling
      if (potnumber==3) {fmsettings[tfichannel-1][35] = potvalue; MIDI.sendControlChange(42,potvalue,tfichannel);} //OP4 Rate Scaling
      break;
    }

    // Detune
    case 5:
    {
      if (potnumber==0) {fmsettings[tfichannel-1][3] = potvalue; MIDI.sendControlChange(24,potvalue,tfichannel);}  //OP1 Detune
      if (potnumber==1) {fmsettings[tfichannel-1][13] = potvalue; MIDI.sendControlChange(25,potvalue,tfichannel);} //OP2 Detune
      if (potnumber==2) {fmsettings[tfichannel-1][23] = potvalue; MIDI.sendControlChange(26,potvalue,tfichannel);} //OP3 Detune
      if (potnumber==3) {fmsettings[tfichannel-1][33] = potvalue; MIDI.sendControlChange(27,potvalue,tfichannel);} //OP4 Detune
      break;
    }

    // Attack Rate
    case 6:
    {
      if (potnumber==0) {fmsettings[tfichannel-1][6] = potvalue; MIDI.sendControlChange(43,potvalue,tfichannel);}  //OP1 Attack Rate
      if (potnumber==1) {fmsettings[tfichannel-1][16] = potvalue; MIDI.sendControlChange(44,potvalue,tfichannel);} //OP2 Attack Rate
      if (potnumber==2) {fmsettings[tfichannel-1][26] = potvalue; MIDI.sendControlChange(45,potvalue,tfichannel);} //OP3 Attack Rate
      if (potnumber==3) {fmsettings[tfichannel-1][36] = potvalue; MIDI.sendControlChange(46,potvalue,tfichannel);} //OP4 Attack Rate
      break;
    }

    // Decay Rate 1
    case 7:
    {
      if (potnumber==0) {fmsettings[tfichannel-1][7] = potvalue; MIDI.sendControlChange(47,potvalue,tfichannel);}  //OP1 1st Decay Rate
      if (potnumber==1) {fmsettings[tfichannel-1][17] = potvalue; MIDI.sendControlChange(48,potvalue,tfichannel);} //OP2 1st Decay Rate
      if (potnumber==2) {fmsettings[tfichannel-1][27] = potvalue; MIDI.sendControlChange(49,potvalue,tfichannel);} //OP3 1st Decay Rate
      if (potnumber==3) {fmsettings[tfichannel-1][37] = potvalue; MIDI.sendControlChange(50,potvalue,tfichannel);} //OP4 1st Decay Rate
      break;
    }

    // Decay Rate 2
    case 8:
    {
      if (potnumber==0) {fmsettings[tfichannel-1][8] = potvalue; MIDI.sendControlChange(51,potvalue,tfichannel);}  //OP1 2nd Decay Rate
      if (potnumber==1) {fmsettings[tfichannel-1][18] = potvalue; MIDI.sendControlChange(52,potvalue,tfichannel);} //OP2 2nd Decay Rate
      if (potnumber==2) {fmsettings[tfichannel-1][28] = potvalue; MIDI.sendControlChange(53,potvalue,tfichannel);} //OP3 2nd Decay Rate
      if (potnumber==3) {fmsettings[tfichannel-1][38] = potvalue; MIDI.sendControlChange(54,potvalue,tfichannel);} //OP4 2nd Decay Rate
      break;
    }

    // Release Rate
    case 9:
    {
      if (potnumber==0) {fmsettings[tfichannel-1][10] = potvalue; MIDI.sendControlChange(59,potvalue,tfichannel);} //OP1 Release Rate
      if (potnumber==1) {fmsettings[tfichannel-1][20] = potvalue; MIDI.sendControlChange(60,potvalue,tfichannel);} //OP2 Release Rate
      if (potnumber==2) {fmsettings[tfichannel-1][30] = potvalue; MIDI.sendControlChange(61,potvalue,tfichannel);} //OP3 Release Rate
      if (potnumber==3) {fmsettings[tfichannel-1][40] = potvalue; MIDI.sendControlChange(62,potvalue,tfichannel);} //OP4 Release Rate
      break;
    }

    // Total Level 2
    case 10:
    {
      if (potnumber==0) {fmsettings[tfichannel-1][9] = potvalue; MIDI.sendControlChange(55,potvalue,tfichannel);} //OP1 2nd Total Level
      if (potnumber==1) {fmsettings[tfichannel-1][19] = potvalue; MIDI.sendControlChange(56,potvalue,tfichannel);} //OP2 2nd Total Level
      if (potnumber==2) {fmsettings[tfichannel-1][29] = potvalue; MIDI.sendControlChange(57,potvalue,tfichannel);} //OP3 2nd Total Level
      if (potnumber==3) {fmsettings[tfichannel-1][39] = potvalue; MIDI.sendControlChange(58,potvalue,tfichannel);} //OP4 2nd Total Level
      break;
    }

    // SSG-EG
    case 11:
    {
      if (potnumber==0) {fmsettings[tfichannel-1][11] = potvalue; MIDI.sendControlChange(90,potvalue,tfichannel);} //OP1 SSG-EG
      if (potnumber==1) {fmsettings[tfichannel-1][21] = potvalue; MIDI.sendControlChange(91,potvalue,tfichannel);} //OP2 SSG-EG
      if (potnumber==2) {fmsettings[tfichannel-1][31] = potvalue; MIDI.sendControlChange(92,potvalue,tfichannel);} //OP3 SSG-EG
      if (potnumber==3) {fmsettings[tfichannel-1][41] = potvalue; MIDI.sendControlChange(93,potvalue,tfichannel);} //OP4 SSG-EG
      break;
    }

    // Amp Mod
    case 12:
    {
      if (potnumber==0) {fmsettings[tfichannel-1][45] = potvalue; MIDI.sendControlChange(70,potvalue,tfichannel);} //OP1 Amplitude Modulation
      if (potnumber==1) {fmsettings[tfichannel-1][46] = potvalue; MIDI.sendControlChange(71,potvalue,tfichannel);} //OP2 Amplitude Modulation
      if (potnumber==2) {fmsettings[tfichannel-1][47] = potvalue; MIDI.sendControlChange(72,potvalue,tfichannel);} //OP3 Amplitude Modulation
      if (potnumber==3) {fmsettings[tfichannel-1][48] = potvalue; MIDI.sendControlChange(73,potvalue,tfichannel);} //OP4 Amplitude Modulation
      break;
    }

    // FM and AM Level
    case 13:
    {
      lcd.setCursor(1,1);
      
      if (polypan>64) // show pan mode enabled
      {
      lcd.print(F("PAN"));
      }
      else // otherwise just blank it
      {
      lcd.print(F("   "));
      fmsettings[tfichannel-1][44] = 127; MIDI.sendControlChange(77,127,tfichannel); // reset the panning to center
      }
      
      if (potnumber==0) {polypan = potvalue;} // enter secret pan mode 
      if (potnumber==1) {lfospeed = potvalue; MIDI.sendControlChange(1,potvalue,1);} //LFO Speed (GLOBAL)
      if (potnumber==2) {fmsettings[tfichannel-1][42] = potvalue; MIDI.sendControlChange(75,potvalue,tfichannel);} //FM Level
      if (potnumber==3) {fmsettings[tfichannel-1][43] = potvalue; MIDI.sendControlChange(76,potvalue,tfichannel);} //AM Level
      break;
    }   
     
  } // end switch  


}

void printzeros(int zeronum) // function for printing leading zeros to 3 digit numbers
{
  if (zeronum<100) lcd.print("0");
  if (zeronum<10) lcd.print("0");
}

void printspaces(int zeronum) // function for printing leading spaces to 3 digit numbers
{
  if (zeronum<100) lcd.print(" ");
  if (zeronum<10) lcd.print(" ");
}


// ============= MIDI FUNCTIONS =========================

void MyHandleNoteOn(byte channel, byte pitch, byte velocity) {

// thank impbox for this formula, a super nice velocity curve :D
velocity = (int)(pow((float)velocity / 127.0f, 0.17f) * 127.0f);

bool repeatnote=0;

if (mode==3 || mode==4) // if we're in poly mode
{

  // here's a fun feature of sustain, you can hit the same note twice
  // let's turn it off and on again here
  
  for (int i = 0; i <= 5; i++) // now scan the current note array for repeats
  {
    if (pitch==polynote[i]) // if the incoming note matches one in the array
    {

      if (polypan>64) // secret stereo mode
      {
      long randpan = random(33,127);
      MIDI.sendControlChange(77,randpan,i+1);
      }
      
      MIDI.sendNoteOff(pitch, velocity, i+1); // turn off that old note
      MIDI.sendNoteOn(pitch, velocity, i+1); // play the new note at that channel
      noteheld[i] = 1; // the note is still being held (it got turned off in note off)
      repeatnote=1; // to bypass the rest
      break;
    }
    MIDI.read();
  } 

  if (repeatnote==0)
  {
    lowestnote = polynote[0]; // don't steal the lowest note
    
    for (int i = 0; i <= 5; i++) // now scan the current note array for the lowest note
    {
      if (polynote[i]<lowestnote && polynote[i]!=0) lowestnote=polynote[i];
      MIDI.read();
    } 
  
    long randchannel = random(0,6); // pick a random channel to switch

    if (polynote[randchannel]==lowestnote) // if in your randomness, you chose the lowest note
    {
      randchannel++; // next channel
      if (randchannel==6) randchannel=0; // loop around
    }

    for (int i = 0; i <= 5; i++) // scan for empty voice slots
    {
      if (polynote[i]==0) {
      randchannel=i; // fill the empty channel
      break;
      }
      MIDI.read();     
    }

    if (polypan>64) // secret stereo mode
    {
      long randpan = random(33,127);
      MIDI.sendControlChange(77,randpan,randchannel+1);
    }
        
    MIDI.sendNoteOff(polynote[randchannel], velocity, randchannel+1); // turn off that old note
    MIDI.sendNoteOn(pitch, velocity, randchannel+1); // play the new note at that channel
    polynote[randchannel] = pitch; // save the new pitch of the note against the voice number
    polyon[randchannel]=1; // turn it on just in case
    noteheld[randchannel] = 1; // the key is currently held
  } // if repeatnote
} // if mode 3
else // otherwise, just revert to midi thru
{
  MIDI.sendNoteOn(pitch, velocity, channel);  
}
} // void note on


void MyHandleNoteOff(byte channel, byte pitch, byte velocity) {
// note: channel here is useless, as it's getting the channel from the keyboard 

if (mode==3 || mode==4) // if we're in poly mode
{    

  for (int i = 0; i <= 5; i++) // we know the note but not the channel
  {
    MIDI.read();
    if (pitch==polynote[i]) // if the pitch matches the note that was triggered off...
    {
      if (sustain==1) // if the sustain pedal is held
      {
        sustainon[i] = 1; // turn on sustain on that channel
        noteheld[i] = 0; // the key is no longer being held down
        break;
      }
      else
      {
        MIDI.sendNoteOff(pitch, velocity, i+1); // turn that voice off against it's channel
        polyon[i]=0; // turn voice off
        polynote[i] = 0; // clear the pitch on that channel
        noteheld[i] = 0; // the key is no longer being held down
        break;
      }
    }
  } 
} // if mode 3
else // otherwise, just revert to midi thru
{
  MIDI.sendNoteOff(pitch, velocity, channel);  
}
} // void note off


void MyHandleCC(byte channel, byte number, byte value) { 
  if (number==64) // if sustain pedal CC
  {
    if (value==0) // if sustain pedal is in off state
    {
      sustain=0;
      for (int i = 0; i <= 5; i++) // scan for sustained channels
      {
        MIDI.read();
        if (noteheld[i]==0) // if the key is not currently being pushed
        {
          MIDI.sendNoteOff(polynote[i], 0, i+1); // turn that voice off against it's channel
          sustainon[i]=0; // turn off sustain on that channel
          polyon[i]=0; // turn voice off
          polynote[i] = 0; // clear the pitch on that channel 
        }
      }
    }
    else
    {
      MIDI.read();
      sustain=1;
    }
  } // if sustain

  if (number==1) // if modulation wheel
  {
    if (value<=5) { MIDI.sendControlChange(74,0,1); } // mod wheel below 5 turns off LFO
    if (value>5) { MIDI.sendControlChange(74,70,1); } // mod wheel above 5 turns on LFO
  } // if modulation
  
} // void cc

void MyHandlePitchbend(byte channel, int bend)
{
  if (mode==3 || mode==4) // if we're in poly mode
  {    
    for (int i = 0; i <= 5; i++) // send for all channels
    {
      MIDI.sendPitchBend(bend, i+1);
      MIDI.read();
    }
  }
  else
  {
    MIDI.sendPitchBend(bend, channel); // just midi thru  
  }
} // void pitch bend
