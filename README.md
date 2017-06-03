illucia dtr
a patchbay controller 
by [Chris Novello](http://chrisnovello.com) ([@paperkettle](http://www.twitter.com/paperkettle))   

It uses physical jacks and patch cables as an interface for making connections between computer programs. 

It is open source everything: hardware, software (microcontroller & computer side), and design.

For more info, visit [illucia.com](http://www.illucia.com) and read the [FAQ](http://www.illucia.com/faq/) and [user guide](http://www.illucia.com/guide/).

## Microcontroller Installation

Teensy++ 2.0
Requires installation of [Teensy extension](https://www.pjrc.com/teensy/td_download.html) for Arduino IDE. Choose correct board model from Tools menu. Click verify, press button on board, upload code to board.

On my computer, the serial device shows up as `/dev/cu.usbmodem1421`
Client software cannot connect to serial device...how to connect manually?
It's very important to solider the header pins on. Simply resting them in the holes doesn't make an electrical connection.
