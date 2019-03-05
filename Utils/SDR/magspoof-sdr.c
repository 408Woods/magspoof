/*
 * MagSpoof - "wireless" magnetic stripe/credit card emulator
 *
 * by Samy Kamkar (http://samy.pl/magspoof/)
 * 
 * modified by Salvador Mendoza(http://salmg.net)
 * + Adding SDR/Wireless support to MagSpoof (https://salmg.net/2019/03/04/adding-sdr-wireless-support-to-magspoof/)
 * 
 *
 * - Allows you to store all of your credit cards and magstripes in one device
 * - Works on traditional magstripe readers wirelessly (no NFC/RFID required)
 * - Can disable Chip-and-PIN (code not included)
 * - Correctly predicts Amex credit card numbers + expirations from previous card number (code not included)
 * - Supports all three magnetic stripe tracks, and even supports Track 1+2 simultaneously
 * - Easy to build using Arduino or ATtiny
 *
 */

#include <Manchester.h>
#include <avr/sleep.h>
#include <avr/interrupt.h>

#define PIN_A 0
#define PIN_B 1
#define ENABLE_PIN 3 // also green LED
#define SWAP_PIN 4 // used -> SDR
#define BUTTON_PIN 2
#define CLOCK_US 200

#define BETWEEN_ZERO 53 // 53 zeros between track1 & 2

#define TRACKS 2

#define BUFFER_SIZE 42 // buffer for sdr
uint8_t buffer[BUFFER_SIZE];
uint8_t receivedSize = 0; 

// keep track while the button is pressed
#define KEY_INTERVALS 25
byte pressButtonMax = 80;    // 80 * KEY_INTERVALS = 2000 ms
byte pressButtonCount = 0;
byte prevButtonState = HIGH;
unsigned long prevButtonMilli = 0;

// consts get stored in flash as we don't adjust them
const char* tracks[] = {
  "%B123456781234567^LASTNAME/FIRST^YYMMSSSDDDDDDDDDDDDDDDDDDDDDDDDD?\0", // Track 1
  ";123456781234567=YYMMSSSDDDDDDDDDDDDDD?\0" // Track 2
};

char revTrack[41];

const int sublen[] = {
  32, 48, 48 };
const int bitlen[] = {
  7, 5, 5 };

unsigned int curTrack = 0;
int dir;

void setup()
{
  man.setupReceive(SWAP_PIN, MAN_1200); // sdr set-up
  man.beginReceiveArray(BUFFER_SIZE, buffer);
  pinMode(PIN_A, OUTPUT);
  pinMode(PIN_B, OUTPUT);
  pinMode(ENABLE_PIN, OUTPUT);
  pinMode(BUTTON_PIN, INPUT_PULLUP);

  // blink to show we started up
  blink(ENABLE_PIN, 200, 3);
  // store reverse track 2 to play later
  storeRevTrack(2);
}

void blink(int pin, int msdelay, int times)
{
  for (int i = 0; i < times; i++)
  {
    digitalWrite(pin, HIGH);
    delay(msdelay);
    digitalWrite(pin, LOW);
    delay(msdelay);
  }
}

// send a single bit out
void playBit(int sendBit)
{
  dir ^= 1;
  digitalWrite(PIN_A, dir);
  digitalWrite(PIN_B, !dir);
  delayMicroseconds(CLOCK_US);

  if (sendBit)
  {
    dir ^= 1;
    digitalWrite(PIN_A, dir);
    digitalWrite(PIN_B, !dir);
  }
  delayMicroseconds(CLOCK_US);
}

// when reversing
void reverseTrack(int track)
{
  int i = 0;
  track--; // index 0
  dir = 0;

  while (revTrack[i++] != '\0');
  i--;
  while (i--)
    for (int j = bitlen[track]-1; j >= 0; j--)
      playBit((revTrack[i] >> j) & 1);
}

// plays out a full track, calculating CRCs and LRC
void playTrack(int track)
{
  int tmp, crc, lrc = 0;
  track--; // index 0
  dir = 0;

  // enable H-bridge and LED
  digitalWrite(ENABLE_PIN, HIGH);

  // First put out a bunch of leading zeros.
  for (int i = 0; i < 25; i++)
    playBit(0);

  //for (int i = 0; tracks[track][i] != '\0'; i++)
  for (int i = (!receivedSize) ? 0 : 1; (!receivedSize) ? tracks[track][i] != '\0' : buffer[i] != '!'; i++)
  {
    crc = 1;
    //tmp = tracks[track][i] - sublen[track];
    tmp = ((receivedSize) ? buffer[i] : tracks[track][i]) - sublen[track];
    for (int j = 0; j < bitlen[track]-1; j++)
    {
      crc ^= tmp & 1;
      lrc ^= (tmp & 1) << j;
      playBit(tmp & 1);
      tmp >>= 1;
    }
    playBit(crc);
  }

  // finish calculating and send last "byte" (LRC)
  tmp = lrc;
  crc = 1;
  for (int j = 0; j < bitlen[track]-1; j++)
  {
    crc ^= tmp & 1;
    playBit(tmp & 1);
    tmp >>= 1;
  }
  playBit(crc);

  // if track 1, play 2nd track in reverse (like swiping back?)
  if (track == 0)
  {
    // if track 1, also play track 2 in reverse
    // zeros in between
    for (int i = 0; i < BETWEEN_ZERO; i++)
      playBit(0);

    // send second track in reverse
    reverseTrack(2);
  }

  // finish with 0's
  for (int i = 0; i < 5 * 5; i++)
    playBit(0);

  digitalWrite(PIN_A, LOW);
  digitalWrite(PIN_B, LOW);
  digitalWrite(ENABLE_PIN, LOW);
}

// stores track for reverse usage later
void storeRevTrack(int track)
{
  int i, tmp, crc, lrc = 0;
  track--; // index 0
  dir = 0;

  for (i = 0; tracks[track][i] != '\0'; i++)
  {
    crc = 1;
    tmp = tracks[track][i] - sublen[track];

    for (int j = 0; j < bitlen[track]-1; j++)
    {
      crc ^= tmp & 1;
      lrc ^= (tmp & 1) << j;
      tmp & 1 ?
        (revTrack[i] |= 1 << j) :
        (revTrack[i] &= ~(1 << j));
      tmp >>= 1;
    }
    crc ?
      (revTrack[i] |= 1 << 4) :
      (revTrack[i] &= ~(1 << 4));
  }

  // finish calculating and send last "byte" (LRC)
  tmp = lrc;
  crc = 1;
  for (int j = 0; j < bitlen[track]-1; j++)
  {
    crc ^= tmp & 1;
    tmp & 1 ?
      (revTrack[i] |= 1 << j) :
      (revTrack[i] &= ~(1 << j));
    tmp >>= 1;
  }
  crc ?
    (revTrack[i] |= 1 << 4) :
    (revTrack[i] &= ~(1 << 4));

  i++;
  revTrack[i] = '\0';
}

// initialized the sdr receiver 
void receiveTrigger(){
  blink(ENABLE_PIN, 90, 3);
  man.beginReceiveArray(BUFFER_SIZE, buffer);
  unsigned long timer = millis();
  receivedSize = 0;
  
  while (1)
  {
    if (man.receiveComplete()) 
    {
      blink(ENABLE_PIN, 60, 1);
      receivedSize = buffer[0];
      if (buffer[receivedSize] == '!') // ! used it as \0
      {
        playTrack(2); // 2 as parameter to avoid the reverse function
        delay(400);
        blink(ENABLE_PIN, 90, 4);
        break;
      }
      man.beginReceiveArray(BUFFER_SIZE, buffer);
      timer = millis();
    }
    else if(millis() - timer >= 7000) // 7 seconds for time-out
    { 
        blink(ENABLE_PIN, 90, 2);
        prevButtonMilli = millis();
        break;
    }
  }
}

void sleep()
{
  GIMSK |= _BV(PCIE);                     // Enable Pin Change Interrupts
  PCMSK |= _BV(PCINT2);                   // Use PB3 as interrupt pin
  ADCSRA &= ~_BV(ADEN);                   // ADC off
  set_sleep_mode(SLEEP_MODE_PWR_DOWN);    // replaces above statement

  MCUCR &= ~_BV(ISC01);
  MCUCR &= ~_BV(ISC00);       // Interrupt on rising edge
  sleep_enable();                         // Sets the Sleep Enable bit in the MCUCR Register (SE BIT)
  sei();                                  // Enable interrupts
  sleep_cpu();                            // sleep

  cli();                                  // Disable interrupts
  PCMSK &= ~_BV(PCINT2);                  // Turn off PB3 as interrupt pin
  sleep_disable();                        // Clear SE bit
  ADCSRA |= _BV(ADEN);                    // ADC on

  sei();                                  // Enable interrupts
}

// XXX move playtrack in here?
ISR(PCINT0_vect) {
  /*  noInterrupts();
   while (digitalRead(BUTTON_PIN) == LOW);
   delay(50);
   while (digitalRead(BUTTON_PIN) == LOW);
   playTrack(1 + (curTrack++ % 2));
   delay(400);
   interrupts();*/
}

void loop() {
  if (millis() - prevButtonMilli >= KEY_INTERVALS) {
      prevButtonMilli = millis();
      byte button_state = digitalRead(BUTTON_PIN);
      if ((prevButtonState == HIGH) && (button_state == LOW)) 
        pressButtonCount = 0;
      else if ((prevButtonState == LOW) && (button_state == HIGH)) {
        if (pressButtonCount >= pressButtonMax) 
            receiveTrigger(); //Start communication
        else {
            noInterrupts();
            if (receivedSize == 0)
             playTrack(1 + (curTrack++ % 2));
            else
              playTrack(2);
            interrupts();
            delay(400);
            prevButtonMilli = millis();
        }
      }
      else if (button_state == LOW) 
          pressButtonCount++;
          
      prevButtonState = button_state;
  }
}
