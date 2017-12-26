illucia dtr
a patchbay controller 
by [Chris Novello](http://chrisnovello.com) ([@paperkettle](http://www.twitter.com/paperkettle))   

It uses physical jacks and patch cables as an interface for making connections between computer programs. 

It is open source everything: hardware, software (microcontroller & computer side), and design.

For more info, visit [illucia.com](http://www.illucia.com) and read the [FAQ](http://www.illucia.com/faq/) and [user guide](http://www.illucia.com/guide/).

## Microcontroller Installation

Teensy++ 2.0
Requires installation of [Teensy extension](https://www.pjrc.com/teensy/td_download.html) for Arduino IDE. Choose correct board model from Tools menu. Click verify, press button on board, upload code to board.

* On my computer, the serial device shows up as `/dev/cu.usbmodem1421`
* Baud setting is 9600.

## Pin mappings

Pins are mapped into arrays. The index of this array is the numeric ID followed by the type of the controller.

## Firmware design 

What is a handshake?

There's a boolean, true by default that instructs the program that it "needs a handshake". Why would it not need one?

What is a Device State?

There is a class called ControlElement that has two setters. Pin number and symbolic ID. This is the base class for all other element types.

A DigitalElement inherits ControlElement with some attributes for byte value, and debouncing data. There defaults for attributes are to say debounce is false and set the byte value to something outside of the expected range. There are getters and setters for the byte data, in this case all expected byte values are boolean. The main class has a single function with no return value named updateState() with no arguments. It gets the current pin number from the parent class.

It seems like the pull up resistors for all the LED and jack connections indicate that a state change will "flip the bit". For example, if the pin read is HIGH, send a 0. From here the function allocates a 4 byte array like 

```
  byte dataToSend[MESSAGE_SIZE] = {
      DIGITAL_TYPE, lowByte(_id), valueToSend, 255
  }
```
Note last byte is FF. This is padding.

A ContinuousElement inherits ControlElement and adds a single attribute for the previous read. It has a single function named updateState() that returns an int. Why is this pattern different than the updateState() function for DigitalElement?

There is some software filtering for noise then it packs the 4 byte array like so

```
  byte bytesToSend[MESSAGE_SIZE] = {
      CONTINUOUS_TYPE, _id, highByte(reading), lowByte(reading)
  };
```

Simple!

The Jack class inherits from ControlElement but also creates instances of DigitalElement, which is strange. It looks like there is some iterative stuff going on here, which makes sense, but we really have a 4x4 matrix with each point having a streaming input or output. 

What is Debouncing? 

Debouncing appears to be a short delay window to not read a value but rather reread the value after this delay. Why this might be some kind of error correction to smooth out the electrical interference which could effect the corectness of the values coming from the controllers.

What is "first byte pertaining to the state of the grid (for now, just beaming the whole state of the grid over when something changes)"?

The serial protocol has the following structure, though there is logic within the array. Perhaps this logic could be better represented through data modeling?

[
int controllerType,
int controllerID,
byte controllerValue,
]

For example, the controllerType changes the meaning of the controllerID. If the type is a jack connection, the controllerID "beams the whole state of the grid over". I'm not sure what that means but it seems like it's larger than simply identifying the jack identifier which has changed.

Also, the controllerType changes the meaning of the controllerValue. If the type is 0, the last two bytes are the value of the controller for this sample. If the type is 1, the controllerValue is a simple boolean (1bit). If the type is 2 the controllerValue is the controllerID of the other jack connection and a boolean to signify if the connection is open or closed.
