/*
 Todo:
 -After first correspondence handshake, broadcast information about device
 Computer will use this to dynamically create a model of the device
 */

/*
 illucia dtr
 "deterritorializer"
 microcontroller code version 0.9
 arduino style code (with Teensy++ as the intended target at the moment)
 
 When there is a change in state, send a message to the computer specifiying the change. 
 The message is a 4 byte array with the makeup:
 
 //first byte: char representaiton of type of control element that this message pertains to
 0 = continuous (potentiometer, sensor, etc)
 1 = digital (button, switch, etc)
 2 = jack event (some jack connection or disconnection)
 3 = arbitrary data (currently unused)
 
 //second byte
 if continous or digital: ID number of control element (ie: which pot changed, or which button changed)
 if jack events: first byte pertaining to the state of the grid (for now, just beaming the whole state of the grid over when something changes)
 if arbitrary data: first byte of whatever data is being sent
 
 //remaining bytes:
 if continuous: send 2 bytes (the 10bit value from the analogRead) 
 if digital: send 1 byte (only 1 bit used). send 1 for pressed, 0 for released 
 if jack event: another jack is involved, so send its ID in the next byte, and another byte pertaining to the state (0 for a disconnection, 1 for a connection)
 */

//Constants:
byte const CONTINUOUS_TYPE = 0;
byte const DIGITAL_TYPE = 1;
byte const JACK_TYPE = 2;

int const MESSAGE_SIZE = 4; //4 bytes transmitted every serial message

int const NUMBER_OF_DIGITAL_ELEMENTS = 8; // how many buttons & switches?
int const NUMBER_OF_CONTINUOUS_ELEMENTS = 6; // how many continuous controls?
int const NUMBER_OF_JACKS = 16; //how many jacks are on this device?

//debouncing&smoothing
int const DEBOUNCE_DELAY = 20;//how many MS should you wait for a debounced value?
int const ANALOG_CHANGE_THRESHOLD = 6; //used for software smoothing of analogRead jitter

//For incoming data to microcontroller:
byte const RESET_TYPE = 0; //used to tell the device to go back to waiting for a handshake
byte const LED_TYPE = 1; //used to specify an LED's brightness
int const NUMBER_OF_LEDS = 4; //how many LEDs are on this device?


//Pin Setup
//these are lists of the microcontroller pins used. the order that they're listed will determine the control element's ID (as in, what number button or switch or potentiometer etc)
int digitalElementPins[] = { //pins hooked up to "digital" elements like buttons / switches. 
    18, 19, 20, 21, 22, 23, 44 ,45
}; 
int continuousElementPins[] = { //analog I/O (aka pots.... FRESH POTS!!!!)
    0, 1, 2, 3, 4, 5
};
int jackPins[] = {
    0,1,2,3,4,5,7,8,9,10,11,12,13,14,15,16
    //  17, 16, 15, 13, 12, 11, 10, 9, 32, 33, 34, 35, 8, 7, 5, 4
};
int ledPins[] = {
    24, 25, 26, 27
};



boolean needsHandshake = true; //used to determine if communication has been initiated with the device or not

//Base class for keeping track of the device's state
class ControlElement {
public:
    
    int _pinNumber; //the microcontroller pin associated with this element
    int _id; //the number of this element (aka, is it jack1, jack2, knob1, knob2, etc)
    
    void setPinNumber(int pinNumber) {
        _pinNumber = pinNumber; 
    }
    
    void setId(int id) {
        _id = id; 
    }
    
};


//Used to keep state of switches, buttons, jack connections.. anything that is on/off, needs debouncing, and is checked with a digital pin on the microcontroller
class DigitalElement : public ControlElement {
public:
    
    byte _value;
    
    boolean _debouncing;
    unsigned long _timeDebounceStarted;
    
    DigitalElement() {
        _debouncing = false;
        
        //initializing _value outside of the range of expected values (0 or 1) 
        //such that it will register the first read as a change in value, and thus broadcast the initial state of the device 
        _value = 3; 
    }
    
    void setValue(byte value) {
        _value = value; 
    }
    byte getValue() {
        return _value; 
    }
    
    void setTimeDebounceStarted(unsigned long timeDebounceStarted) {
        _timeDebounceStarted = timeDebounceStarted;
    }
    unsigned long getTimeDebounceStarted() {
        return _timeDebounceStarted;
    }
    
    void setDebouncing (boolean debouncing) {
        _debouncing = debouncing;
    }
    boolean isDebouncing() {
        return _debouncing;
    }
    
    void updateState() {
        int currentReading = digitalRead(_pinNumber);
        
        if (_value != currentReading) { //there is a state change, or this is the very first time checking state
            
            if (_debouncing ) {
                
                if (_timeDebounceStarted + DEBOUNCE_DELAY <= millis()) { //if the debounce delay has passed
                    
                    _debouncing = false; //done debouncing
                    _value = currentReading; //set the current value
                    
                    byte valueToSend; //send opposite of the read, given pull up resisitors
                    if (_value == HIGH) {
                        valueToSend = 0;
                    } 
                    else {
                        valueToSend = 1;
                    }
                    
                    //pack a byte array with data about the state change
                    byte dataToSend[MESSAGE_SIZE] = {
                        DIGITAL_TYPE, lowByte(_id), valueToSend, 255
                    }; //dummy byte at the end - easier to send everything as packets of the same size.. might change this down the road if there is some need to send arbitrary data.
                    Serial.write(dataToSend, MESSAGE_SIZE); //send it over serial.
                    
                }
            }
            else { //this is the first time we've read a state change, and thus should prepare a debounce, re-read after a delay
                _debouncing = true;
                _timeDebounceStarted = millis();
            }
        }
    }
};


//Used to keep state of knobs, sensors.. anything where a variable voltage range is read from an analog pin on the microcontroller
class ContinuousElement : public ControlElement {
public:
    
    int _previousRead; //used to keep track of readings in order to average / minimize noise on the analog readings
    
    int updateState() {
        
        int reading = analogRead(_pinNumber);
        
        //factor out noise
        int difference = _previousRead - reading;
        
        if (abs(difference) > ANALOG_CHANGE_THRESHOLD) { //simple software way of factoring out fluctations in reading due to noise/power  
            
            //check for edge cases, so pot doesn't miss full CW or CCW positions given software filtering
            if (reading <= ANALOG_CHANGE_THRESHOLD) {
             reading = 0; 
            } else if (reading >= 1023 - ANALOG_CHANGE_THRESHOLD) {
              reading = 1023;
            }
            
            
            //send update!
            byte bytesToSend[MESSAGE_SIZE] = {
                CONTINUOUS_TYPE, _id, highByte(reading), lowByte(reading)
            };
            Serial.write(bytesToSend, MESSAGE_SIZE);
            
            //update internal state  
            _previousRead = reading;
        }
    }
};


//Used to keep state of the patchbay's jacks. Wrapper around a digital element array 
class Jack : public ControlElement {
public:   
    
    //Each jack keeps track of which other jacks it is connected to.
    //uses "DigitalElements" because a jack connection behaves like a switch
    DigitalElement _connectionState[NUMBER_OF_JACKS]; //could be NUMBER_OF_JACKS-1 but might complicate index versus jack#.. so will leave it for now..
    
    Jack() {
        //When this jack is created, setup all of its possible connections 
        for (int i = 0; i < NUMBER_OF_JACKS; i++) {
            
            _connectionState[i].setPinNumber(jackPins[i]);
            _connectionState[i].setId(i);
            // _connectionState[i].setValue(HIGH); //initialize to HIGH (aka no connection, given pull up resistors)
        }
    }
    
    void updateState() {
        //turn this jack low, then check all the others to see what has changed. 
        
        //flip the jack you're checking to an output
        pinMode(_pinNumber, OUTPUT);
        digitalWrite(_pinNumber, LOW); // should bring all the other pins that it is connected to (inputs with pullups on) to LOW/Ground
        
        for (int i = 0; i < NUMBER_OF_JACKS; i++) { //look for changes
            
            int currentReading = digitalRead(_connectionState[i]._pinNumber); //find out if there is a connection with this jack or not. 
            
            if (i != _id) { //this just means "don't check this pin for connections with itself"
                
                if (_connectionState[i].getValue() != currentReading) { //"is this reading different from what is already recorded as the state?" or, is it the first re
                    
                    if (_connectionState[i].isDebouncing()) {//"and did we already record that this state change occured, but we're still debouncing?"
                        
                        if (_connectionState[i].getTimeDebounceStarted() + DEBOUNCE_DELAY <= millis()) { //"and if so, has the debounce delay passed such that we can now send the update?"
                            
                            _connectionState[i].setDebouncing(false); //done debouncing, so reset the flag
                            _connectionState[i].setValue(currentReading); //set the current value
                            
                            byte value; //send opposite of the read, given pull up resisitors
                            if (_connectionState[i].getValue() == HIGH) {
                                value = 0;
                            } 
                            else {
                                value = 1;
                            }
                            
                            byte dataToSend[MESSAGE_SIZE] = {
                                JACK_TYPE, lowByte(_id), lowByte(i), value
                            }; //make a byte array to send over serial. indicate the type of message, the two jacks, and their connection status
                            Serial.write(dataToSend, MESSAGE_SIZE); //send it over serial.
                        }
                    }
                    else { //this is the first time we've read a state change, and thus should prepare a debounce, re-read after a delay
                        _connectionState[i].setDebouncing(true);
                        _connectionState[i].setTimeDebounceStarted(millis());
                    }
                }
            }
        }
        
        pinMode(_pinNumber, INPUT);
        digitalWrite(_pinNumber, HIGH); // pull-up gets turned on again so that other jacks can check their connection to this one.
    }
};

//Arrays of elements - state needs to be maintained so that the device can send updates only upon changes (also, state is needed for debouncing, and analog signal smoothing)
DigitalElement digitalState[NUMBER_OF_DIGITAL_ELEMENTS];
ContinuousElement continuousState[NUMBER_OF_CONTINUOUS_ELEMENTS * 2]; //need 2 byts for every continous controller
Jack jacks[NUMBER_OF_JACKS]; //contains every jack. every jack then knows what other jacks it is patched into

void updateDigitalElements() {
    
    for (int i = 0; i < NUMBER_OF_DIGITAL_ELEMENTS; i++) {
        digitalState[i].updateState();
    }
}

void updateContinuousElements() {
    for (int i = 0; i < NUMBER_OF_CONTINUOUS_ELEMENTS; i++) {
        continuousState[i].updateState();
    }
}


void updateJacks() {
    for (int i = 0; i < NUMBER_OF_JACKS; i++) {
        jacks[i].updateState();
    }
}

void setup() {
    
    Serial.begin(9600);
    
    //setup pins, engage pull ups
    for (int i = 0; i < NUMBER_OF_DIGITAL_ELEMENTS; i++) {
        
        pinMode(digitalElementPins[i], INPUT);
        digitalWrite(digitalElementPins[i], HIGH);
        
        digitalState[i].setId(i); //keep track of the element number
        digitalState[i].setPinNumber(digitalElementPins[i]);
    }
    
    for (int i = 0; i < NUMBER_OF_CONTINUOUS_ELEMENTS; i++) {
        continuousState[i].setPinNumber(continuousElementPins[i]);
        continuousState[i].setId(i);
    }
    
    for (int i = 0; i < NUMBER_OF_JACKS; i++) {
        pinMode(jackPins[i], INPUT);
        digitalWrite(jackPins[i], HIGH);
        
        jacks[i].setId(i);
        jacks[i].setPinNumber(jackPins[i]);
    }
    
    for (int i = 0; i < NUMBER_OF_LEDS; i++) {
        pinMode(ledPins[i], OUTPUT);
    }
    
    for (int j = 0; j < 2; j++) {
        for (int i = 0; i < NUMBER_OF_LEDS; i++) {
            analogWrite(ledPins[i], random(20));
            delay(45); //init sequence
            analogWrite(ledPins[i], 0);
        }
        
        for (int i = NUMBER_OF_LEDS - 1; i >= 0; i--) {
            analogWrite(ledPins[i], random(255));
            delay(90); //init sequence
            analogWrite(ledPins[i], 0);
        }
    }
    
}


void loop() {

    digitalWrite(6, HIGH);
    if (needsHandshake) {
        
        //do serial handshake. 
        byte initMessage[MESSAGE_SIZE] = {
            'd', 'e', 't', 'r'
        };
        while (Serial.available () <= 0) {
            Serial.write(initMessage, MESSAGE_SIZE);
            delay(1000);
        }
        Serial.flush();
        
        needsHandshake = false;
        
        for (int j = 0; j < 2; j++) {
            for (int i = 0; i < NUMBER_OF_LEDS; i++) {
                analogWrite(ledPins[i], 255);
                delay(45); //init sequence
                analogWrite(ledPins[i], 0);
            }
        }
        
    } else {
        
        //update elements
        updateDigitalElements();
        updateContinuousElements();
        updateJacks();
        
        
        delay(1);
        //
        while (Serial.available () > 0) {
            
            //illucia also recieves serial messages            
            
            // if (Serial.available() % MESSAGE_SIZE == 0) {
            if (Serial.available() > 0) {
                
                byte type = Serial.read();
                
                if (type == LED_TYPE) {  
                    
                    while(Serial.available() < 2) {
                        //make sure you have received the two bytes specifying the LED value
                    }
                    
                    byte id = Serial.read();
                    byte value = Serial.read();
                    
                    analogWrite(ledPins[id], value);
                    
                }
                else if (type == RESET_TYPE) {
                    needsHandshake = true;
                } 
            }
            else {
                Serial.flush();
            }
        }
        
    }
}



//Note to self: 1st prototype had this pinout:

/*
int digitalElementPins[] = {
  18, 19, 20, 21, 28, 29, 30, 31
}; //pins hooked up to "digital" elements like buttons / switches. 
int continuousElementPins[] = {
  45, 44, 43, 42, 40, 39
};
int jackPins[] = {
  17, 16, 15, 13, 12, 11, 10, 9, 32, 33, 34, 35, 8, 7, 5, 4
};
int ledPins[] = {
  24, 25, 26, 27
};
*/
