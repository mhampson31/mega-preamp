#include "Arduino.h"
#include <Wire.h>
#include <Adafruit_CharacterOLED.h>
#include <IRremote.h>
#include <DS1307RTC.h>
#include <Time.h>

#define USE_LCD 1
#define USE_SERIAL 1

/* Pin counts 
 Volume: 1 in, read level from THAT2180
 Power button: 1 in, read. 2 power.
 OLED: 2 power, 7 data
 In select: 8 data
 Out select: 4 data
 IR: 1 in
 RTC: 5 in a row
 Total: 3 in, 4 power, 24 data
 */


/* IO Pins 
 No pin numbers can be defined twice in this section!
 */

// --L293D volume control
// --currently the CW and CCW control wires are both green
#define VOL_CW A4
#define VOL_CCW A5
#define VOL_PIN A2 //what is this?

// --IR
#define IR_PIN 3
#define IR_5V 4
#define IR_GROUND 5

// --OLED pins with pin #s
#define OLED_5V 13
// -- OLED Ground goes to a ground pin
#define OLED_RS 6   //4
#define OLED_RW 7   //5
#define OLED_E 8    //6
#define OLED_DB4 9  //11
#define OLED_DB5 10  //12
#define OLED_DB6 11 //13
#define OLED_DB7 12 //14

// --SSR
#define SSR_5V 53 //white wire
#define SSR_GROUND 52 //blue wire

// --RTC pins. Not much choice, we need SCA/SCL
#define RTC_GROUND 18
#define RTC_5V 19

// --power button
// --the button signal goes to any interrupt pin
#define BUTTON_5V A0
#define BUTTON_GROUND A1
#define BUTTON_IN 2

// --the ULN2803A darlington arrays
#define ULN_GROUND 22
#define ULN_5V 23


// --I/O relays
// --The power pins are technically to the darlington arrays, not the relays
#define IO_5V 50
#define IO_GROUND 52

/* end IO pin assignments */

/* *** Relay pin assignments *** */

#define IN_RELAY_1 38
#define IN_RELAY_2 40
#define IN_RELAY_3 44
#define IN_RELAY_4 46

#define OUT_RELAY_1 34
#define OUT_RELAY_2 36

// --define these ports to keep the ULN from drawing too much current
// --(may actually not be needed?)
#define UNUSED_RELAY_1 42
#define UNUSED_RELAY_2 44 

/* *** end relay pin assignments *** */

/* Audio IO */
// --Don't forget to define the relays
#define NUM_INPUTS 4
#define NUM_OUTPUTS 2
#define NUM_DUMMIES 2

/* IR - NEC Command Codes */
#define IR_REPEAT 0xFFFFFFFF
#define IR_NONE 0
#define IR_ENTER 0x1CE3B04F
#define IR_MUTE 0x1CE320DF
#define IR_POWER 0x1CE3807F
#define IR_RIGHT 0x1CE352AD
#define IR_LEFT 0x1CE3926D
#define IR_UP 0x1CE3D02F
#define IR_DOWN 0x1CE3F00F
#define IR_MENU 0x1CE350AF
#define IR_BACK 0x1CE322DD
#define IR_EXIT 0x1CE3E01F
#define IR_VOL_DOWN 0x1CE308F7
#define IR_VOL_UP 0x1CE330CF
#define IR_CHANNEL_UP 0x1CE318E7
#define IR_CHANNEL_DOWN 0x1CE338C7

// -- power button input
#define BUTTON_POWER 100

// -- no command
#define NO_COMMAND 0

#define DIR_DOWN -1
#define DIR_UP 1
#define DIR_NONE 0

#define MAX_VOLUME 255
// --how long does the volume pot activate for? in milliseconds
#define VOL_CHANGE_DELAY 250

#define R_SELECT 0
#define R_DESELECT 1

#define M_MUTE 0
#define M_UNMUTE 1
#define M_TOGGLE 2

/* *** CLASS DEFINITIONS *** */

class Relay {
  /* All relays should be wired with consecutive ports.
   The right pin is the reset pin, and should be wired to the first port.
   So: drop the first pin to reset a relay, drop the second pin to set it.
   
   Note that we don't bother to code the set pin, since it's always reset + 1.
   
   Physically, the input relays are selected when they're in their reset condition.
   */

public:
  byte pin;       
  void toggle(byte dir);
  void mute(byte dir);
  boolean is_mute;
  Relay(byte p);
};

Relay::Relay(byte p) {    
  pin = p;
  is_mute = false;
};

void Relay::toggle(byte dir) { 
  Serial.print("  * Toggle relay "); 
  Serial.print(pin);
  if (dir == R_SELECT) {
    Serial.println(", select.");
    digitalWrite(pin, HIGH);
    delay(3);
    digitalWrite(pin, LOW);
  }
  else {
    Serial.println(", deselect.");
    digitalWrite(pin+1, HIGH);
    delay(3);
    digitalWrite(pin+1, LOW);
  }
};

void Relay::mute(byte dir) {
  byte new_mute;
  switch(dir) {
  case M_TOGGLE:
    new_mute = !is_mute;
    break;
  case M_MUTE:
    new_mute = true;
    break;
  case M_UNMUTE:
    new_mute = false;
    break;
  }
  if (new_mute) {
        Serial.println("Muting.");
        toggle(R_DESELECT);
        is_mute = true;
  }
  else {
        Serial.println("Unmuting.");
        toggle(R_SELECT);
        is_mute = false;
  }

}


/* *** Global objects *** */

IRrecv ir_receiver(IR_PIN);
decode_results ir_command;

Adafruit_CharacterOLED lcd(OLED_V2, OLED_RS, OLED_RW, OLED_E, OLED_DB4, OLED_DB5, OLED_DB6, OLED_DB7);

byte input = 0;
byte output = 0;
byte volume;
long last_command = 0;
boolean amp_on = true;
volatile boolean button_command = false;

// --This is global so we can change it in the volume function or the loop.
byte volume_pin;
boolean alps_is_on = false;
boolean turn_alps_off = true;

// --Note that the relays are all initialized when they're defined
Relay inputs[NUM_INPUTS] = {
  Relay(IN_RELAY_1),
  Relay(IN_RELAY_2),
  Relay(IN_RELAY_3),
  Relay(IN_RELAY_4)
  };

Relay outputs[NUM_OUTPUTS] = {
  Relay(OUT_RELAY_1),
  Relay(OUT_RELAY_2)
  };

Relay dummy_relays[NUM_DUMMIES] = {
  Relay(UNUSED_RELAY_1),
  Relay(UNUSED_RELAY_2)
  };

void setup() {
    Wire.begin();
    Serial.begin(9600);
    Serial.println("Serial started.");

    pinMode(OLED_5V, OUTPUT);
    digitalWrite(OLED_5V, HIGH);
    lcd.begin(16, 2);
    //lcd.print("Boot time.");
    Serial.println("Started LCD.");

    pinMode(BUTTON_GROUND, OUTPUT);
    digitalWrite(BUTTON_GROUND, LOW);
    pinMode(BUTTON_5V, OUTPUT);
    pinMode(BUTTON_IN, INPUT);
    digitalWrite(BUTTON_5V, HIGH);
    Serial.println("Started power button.");

    pinMode(RTC_GROUND, OUTPUT);
    digitalWrite(RTC_GROUND, LOW);
    pinMode(RTC_5V, OUTPUT);
    digitalWrite(RTC_5V, HIGH);

    //rtc.begin();
    setSyncProvider(RTC.get); 
    Serial.println("Started RTC.");

    pinMode(IR_GROUND, OUTPUT);
    digitalWrite(IR_GROUND, LOW);
    pinMode(IR_5V, OUTPUT);
    digitalWrite(IR_5V, HIGH);
    ir_receiver.enableIRIn();
    Serial.println("Started IR.");

    pinMode(VOL_CW, OUTPUT);
    pinMode(VOL_CCW, OUTPUT);
    digitalWrite(VOL_CW, HIGH);
    digitalWrite(VOL_CCW, HIGH);
    Serial.println("L293D initialized.");

    pinMode(ULN_GROUND, OUTPUT);
    digitalWrite(ULN_GROUND, LOW);  

    for (byte r = 0; r < NUM_DUMMIES; r++) {
      Serial.print("Initializing dummy relay ");
      Serial.println(r);
      pinMode(dummy_relays[r].pin, OUTPUT);
      pinMode(dummy_relays[r].pin + 1, OUTPUT);
      //digitalWrite(dummy_relays[r].pin, HIGH);
      //digitalWrite(dummy_relays[r].pin+1, HIGH);
    }

    for (byte r = 0; r < NUM_INPUTS; r++) { 
      Serial.print("Initializing input ");
      Serial.println(r);
      pinMode(inputs[r].pin, OUTPUT);
      pinMode(inputs[r].pin + 1, OUTPUT);
      //digitalWrite(inputs[r].pin, HIGH);
      //digitalWrite(inputs[r].pin+1, HIGH);
      inputs[r].toggle(R_DESELECT);
    }
    inputs[0].toggle(R_SELECT);
    Serial.println("Inputs initialized.");

    for (byte r = 0; r < NUM_OUTPUTS; r++) {
      Serial.print("Initializing output ");
      Serial.println(r);
      pinMode(outputs[r].pin, OUTPUT);
      pinMode(outputs[r].pin + 1, OUTPUT);
      //digitalWrite(outputs[r].pin, HIGH);
      //digitalWrite(outputs[r].pin+1, HIGH);
      outputs[r].toggle(R_DESELECT);
    }
    outputs[0].toggle(R_SELECT);
    Serial.println("Outputs initialized.");

    pinMode(ULN_5V, OUTPUT);
    digitalWrite(ULN_5V, HIGH);

    pinMode(SSR_5V, OUTPUT);
    pinMode(SSR_GROUND, OUTPUT);
    digitalWrite(SSR_GROUND, LOW);
    Serial.println("Started SSR.");

    // --initial SSR turn-on, bypassing toggle_amp()
    digitalWrite(SSR_5V, HIGH);

    attachInterrupt(0, button_pushed, RISING);
    Serial.println("Interrupts attached.");
    
    Serial.println("Startup finished.");
    update_display();

  }

void loop() {
  long next_command = NO_COMMAND;
  if (button_command) {
    next_command = BUTTON_POWER;
    button_command = false;
  }
  else if (ir_receiver.decode(&ir_command)) {
    if (ir_command.decode_type == 1) {
      next_command = ir_command.value;
      Serial.println(ir_command.decode_type);
      Serial.println("");
    }
    ir_receiver.resume();
  }

  dispatch(next_command);
  next_command = NO_COMMAND;

  if (alps_is_on && turn_alps_off) {
    // If we're done changing volume, turn the motor off
    digitalWrite(volume_pin, HIGH);
    alps_is_on = false;
  }
  update_display();

  //pause for a beat, keeps the IR from getting confused
  //might be able to tune this down to something between 100 and 50.
  delay(100);

}

// -- button interrupt
void button_pushed () {
  button_command = true;
}


void update_display() {
    time_t t_now = now();
    char line1[16];
    char line2[16];
    char out_status[10];

    sprintf(line1, "    %2d:%02d %cM    ", hourFormat12(t_now), minute(t_now), (isAM(t_now)) ? ('A'):( 'P')); 
    lcd.setCursor(0, 0);
    lcd.print(line1);
  
    if (outputs[output].is_mute) {
      sprintf(out_status, "Mute (%c)", (output == 0) ? 'H':'S');
    }
    else {
      sprintf(out_status, "%s", (output == 0) ? "Headphones":"Speakers");
    }
  
    if (amp_on) {
        sprintf(line2, "%d --> %-10s", input+1, out_status);
    }
    else {
        sprintf(line2, "%16c", ' ');
    }
    
    lcd.setCursor(0, 1);
    lcd.print(line2);
}

void send_info() {
    Serial.println("Put diagnostic stuff here.");
}


void dispatch(unsigned long code)
{
  turn_alps_off = true;

  switch (code) {
  case NO_COMMAND:
    // --nothing happening
    break;
  case IR_ENTER:
    break;
  case IR_MUTE:
    if (amp_on) {
        outputs[output].mute(M_TOGGLE);
    }
    break;
  case BUTTON_POWER:
  case IR_POWER:
    toggle_amp();
    break;
  case IR_RIGHT: 
    break;
  case IR_LEFT: 
    break;
  case IR_UP:
    if (amp_on) {
        change_output(DIR_UP);
    }
    Serial.println("Output up");
    break;
  case IR_DOWN:
    if (amp_on) {
        change_output(DIR_DOWN);
    }
    Serial.println("Output down");
    break;
  case IR_MENU:
    send_info();
    break;
  case IR_BACK: 
    break;
  case IR_EXIT: 
    break;
  case IR_VOL_DOWN:
    if (amp_on) {
        change_volume(DIR_DOWN);
    }
    Serial.println("Vol down");
    break;
  case IR_VOL_UP:
    if (amp_on) {
        change_volume(DIR_UP);
    }
    Serial.println("Vol up");
    break;
  case IR_CHANNEL_UP:
    if (amp_on) {
        change_input(DIR_UP);
    }
    Serial.println("Channel up");
    break;
  case IR_CHANNEL_DOWN:
    if (amp_on) {
        change_input(DIR_DOWN);
    }
    Serial.println("Channel down");
    break;
  case IR_REPEAT:
    /* We don't do anything, because the only thing that repeats is volume.
     We just need to get to this point in order to keep the volume changing
     if the button is still being pressed.
     */
    turn_alps_off = false;
    break;
  }
  last_command = code;
}


void toggle_amp() {
  // --make sure the output is muted when the power changes
  
  if (amp_on) {
    // turn off the SSR, change the world state
    amp_on = false;
    outputs[output].mute(M_MUTE);
    delay(200);
    digitalWrite(SSR_5V, LOW);
    Serial.println("Power off");
  }
  else {
    // turn the SSR on
    amp_on = true;
    digitalWrite(SSR_5V, HIGH);
    delay(200);
    outputs[output].mute(M_UNMUTE);
    Serial.println("Power on");
  }

  // --pause half a second so we can't toggle back and forth too quickly
  delay(500);
}


void change_volume(byte dir) {
  /* Toggle the volume pot motor on.
   We let it go until no command is received by the loop.
   Then we turn the motor off.
   
   Side note: Right now the THAT2180s are wired backwards so that counter-clockwise
   on the pot means higher volume, not lower.
   */
        (dir == DIR_UP) ? (volume_pin = VOL_CCW):(volume_pin = VOL_CW);
        alps_is_on = true;
        turn_alps_off = false;
        digitalWrite(volume_pin, LOW);
}


void change_output(byte dir) {
    if (!outputs[output].is_mute) {
        byte new_output;
        if (dir == DIR_UP) {
            new_output = (output + 1) % NUM_OUTPUTS;
        }
        else  {
            if (output == 0) {
                new_output = NUM_OUTPUTS - 1;
            }
            else {
                new_output = output - 1;
            }
        }
        for (byte r = 0; r < NUM_OUTPUTS; r++) {
            if (r != new_output) {
                outputs[r].toggle(R_DESELECT);
            }
        }
        outputs[new_output].toggle(R_SELECT);
        output = new_output;
    }
}


void change_input(byte dir) {
    Serial.println("Channel change.");
        byte new_input;
        if (dir == DIR_UP) {
            new_input = (input + 1) % NUM_INPUTS;
        }
        else  {
            new_input = (input == 0) ? NUM_INPUTS - 1 : input - 1;
        }
        
        for (byte r = 0; r < NUM_INPUTS; r++) {
            if (r != new_input) {
                inputs[r].toggle(R_DESELECT);
            }
        }
        inputs[new_input].toggle(R_SELECT);
        input = new_input;
        Serial.println("Done");
}
