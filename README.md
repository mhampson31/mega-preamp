# mega-preamp
This program controls my Arduino Mega-based DIY audio receiver.

The Mega coordinates a few other hardware components: 
 * several relay pairs used to select input and output channels
 * an OLED display
 * a motorized volume control potentiometer
 * a hardware power button
 * an IR receiver for remote control
 * a real-time clock

History
  v1.0: First complete build (1/2015)
  v1.1: Fixed IR freezing; changed display formatting; added amp_on checks to many commands;
        blocked output change on mute; enabled first input and headphone output on power-on. (7/5/2015)
  
TODO
  1. Refactor defines to enum
  2. Add menu tree (but for what options?)
  3. Configurable + persistant options for input names
  4. Remote reboot, selective component reboots
