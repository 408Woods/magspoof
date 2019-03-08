/*
* Salvador Mendoza (salmg.net)
* Manchester enconding to transmit a test track to MagSpoof with a simple SDR
* More info: https://salmg.net/2019/03/04/adding-sdr-wireless-support-to-magspoof/
*/


#include <Manchester.h>

#define TX_PIN  7  //pin where your transmitter is connected
#define LED_PIN 13 //pin for blinking LED
#define BUFFER_SIZE 40 // buffer for sdr - must be the same size as expected in the MagSpoof buffer

uint8_t led = 1; //last led status

//;5634567891234567=12345678123456781234? track example
uint8_t data[BUFFER_SIZE] = {11,';','5','6', '3', '4', '5', '6', '7', '8','9','1','2','3','4','5','6','7','=','1','2', '3', '4', '5', '6', '7', '8','1','2','3','4','5','6','7','8','1','2','3','4','?'};

void setup() {
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, led);
  //man.workAround1MhzTinyCore(); //add this in order for transmitter to work with 1Mhz Attiny85/84
  man.setupTransmit(TX_PIN, MAN_1200);
}

uint8_t datalength = 2;   //at least two data
void loop() {
  data[0] = datalength;
  man.transmitArray(datalength, data);
  led = ++led % 2;
  digitalWrite(LED_PIN, led);
  
  delay(200);
  datalength++;
  if(datalength>40)
    datalength=2;
}
