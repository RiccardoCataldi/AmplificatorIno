#include <LiquidCrystal.h>

const int rs = 10, en = 11, d4 = 5, d5 = 4, d6 = 3, d7 = 2;
LiquidCrystal lcd(rs, en, d4, d5, d6, d7);

boolean clipping = 0;

// Data storage variables
byte newData = 0;
byte prevData = 0;
unsigned int time = 0;    // Keeps time and sends values to store in timer[] occasionally
int timer[10];            // Storage for timing of events
int slope[10];            // Storage for slope of events
unsigned int totalTimer;  // Used to calculate period
volatile unsigned int period;      // Storage for period of wave
byte index = 0;           // Current storage index
volatile float frequency;          // Storage for frequency calculations
int maxSlope = 0;         // Used to calculate max slope as trigger point
int newSlope;             // Storage for incoming slope data

// Variables for deciding whether you have a match
byte noMatch = 0;   // Counts how many non-matches you've received to reset variables if it's been too long
byte slopeTol = 3;  // Slope tolerance- adjust this if you need
int timerTol = 10;  // Timer tolerance- adjust this if you need

// Variables for amp detection
unsigned int ampTimer = 0;
byte maxAmp = 0;
volatile byte checkMaxAmp;
byte ampThreshold = 30;  // Raise if you have a very noisy signal

// Reference frequencies for guitar strings
const float referenceFrequencies[] = {82.40, 110.00, 146.80, 196.00, 246.90, 329.60};
const char* stringNames[] = {"E (Basso)", "A", "D", "G", "B", "E"};

void findClosestString(float freq, const char*& closestString, float& frequencyDifference) {
  float minDifference = abs(referenceFrequencies[0] - freq);
  closestString = stringNames[0];
  frequencyDifference = referenceFrequencies[0] - freq;

  for (int i = 1; i < 6; i++) {
    float difference = abs(referenceFrequencies[i] - freq);
    if (difference < minDifference) {
      minDifference = difference;
      closestString = stringNames[i];
      frequencyDifference = referenceFrequencies[i] - freq;
    }
  }
}

void setup() {
  lcd.begin(16, 2);
  lcd.print("TUNER");
  Serial.begin(9600);

  pinMode(13, OUTPUT);  // LED indicator pin
  pinMode(12, OUTPUT);  // Output pin

  cli();  // Disable interrupts

  // Set up continuous sampling of analog pin 0 at 38.5kHz

  // Clear ADCSRA and ADCSRB registers
  ADCSRA = 0;
  ADCSRB = 0;

  ADMUX |= (1 << REFS0);  // Set reference voltage
  ADMUX |= (1 << ADLAR);  // Left align the ADC value- so we can read highest 8 bits from ADCH register only

  ADCSRA |= (1 << ADPS2) | (1 << ADPS0);  // Set ADC clock with 32 prescaler- 16mHz/32=500kHz
  ADCSRA |= (1 << ADATE);                 // Enable auto trigger
  ADCSRA |= (1 << ADIE);                  // Enable interrupts when measurement complete
  ADCSRA |= (1 << ADEN);                  // Enable ADC
  ADCSRA |= (1 << ADSC);                  // Start ADC measurements

  sei();  // Enable interrupts
}

ISR(ADC_vect) {  // When new ADC value ready

  PORTB &= B11101111;                           // Set pin 12 low
  prevData = newData;                           // Store previous value
  newData = ADCH;                               // Get value from A0
  if (prevData < 127 && newData >= 127) {       // If increasing and crossing midpoint
    newSlope = newData - prevData;              // Calculate slope
    if (abs(newSlope - maxSlope) < slopeTol) {  // If slopes are ==
      // Record new data and reset time
      slope[index] = newSlope;
      timer[index] = time;
      time = 0;
      if (index == 0) {      // New max slope just reset
        PORTB |= B00010000;  // Set pin 12 high
        noMatch = 0;
        index++;  // Increment index
      } else if (abs(timer[0] - timer[index]) < timerTol && abs(slope[0] - newSlope) < slopeTol) {  // If timer duration and slopes match
        // Sum timer values
        totalTimer = 0;
        for (byte i = 0; i < index; i++) {
          totalTimer += timer[i];
        }
        period = totalTimer;  // Set period
        // Reset new zero index values to compare with
        timer[0] = timer[index];
        slope[0] = slope[index];
        index = 1;           // Set index to 1
        PORTB |= B00010000;  // Set pin 12 high
        noMatch = 0;
      } else {    // Crossing midpoint but not match
        index++;  // Increment index
        if (index > 9) {
          reset();
        }
      }
    } else if (newSlope > maxSlope) {  // If new slope is much larger than max slope
      maxSlope = newSlope;
      time = 0;  // Reset clock
      noMatch = 0;
      index = 0;  // Reset index
    } else {      // Slope not steep enough
      noMatch++;  // Increment no match counter
      if (noMatch > 9) {
        reset();
      }
    }
  }

  if (newData == 0 || newData == 1023) {  // If clipping
    PORTB |= B00100000;                   // Set pin 13 high- turn on clipping indicator led
    clipping = 1;                         // Currently clipping
  }

  time++;  // Increment timer at rate of 38.5kHz

  ampTimer++;  // Increment amplitude timer
  if (abs(127 - ADCH) > maxAmp) {
    maxAmp = abs(127 - ADCH);
  }
  if (ampTimer == 1000) {
    ampTimer = 0;
    checkMaxAmp = maxAmp;
    maxAmp = 0;
  }
}

void reset() {   // Clear out some variables
  index = 0;     // Reset index
  noMatch = 0;   // Reset match counter
  maxSlope = 0;  // Reset slope
}

void checkClipping() {   // Manage clipping indicator LED
  if (clipping) {        // If currently clipping
    PORTB &= B11011111;  // Turn off clipping indicator led
    clipping = 0;
  }
}

void loop() {

  checkClipping();

  if (checkMaxAmp > ampThreshold) {
    frequency = 38462.0 / float(period);  // Calculate frequency timer rate/period
    lcd.setCursor(0, 1);
    lcd.print(frequency);  // Print results to LCD
    Serial.print(frequency);
    Serial.println(" hz");

    // Find and print the closest string and its difference
    const char* closestString;
    float frequencyDifference;
    findClosestString(frequency, closestString, frequencyDifference);
    lcd.setCursor(0, 0);
    lcd.print(closestString);
    lcd.setCursor(0, 1);
    lcd.print("Diff: ");
    lcd.print(frequencyDifference, 2);
    lcd.print(" Hz");

    Serial.print("Closest string: ");
    Serial.println(closestString);
    Serial.print("Frequency difference: ");
    Serial.print(frequencyDifference, 2);
    

    delay(1000);
    lcd.clear();
  }

  delay(100);  // Delete this if you want

  // Do other stuff here
}
