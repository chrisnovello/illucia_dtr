//Change these values if you'd like to change illuciaConnect's ports!
final int OSC_INCOMING_PORT = 12000;
final int OSC_OUTGOING_PORT = 12001;

//Todo: 

//automatically get device info at boot.
//make model of device OO, support multiple devices plugged in at once...
//make it so one can connect illucia devices after launch
//make it so user selects device from serial list..? and has connect/disconnect buttons in the utility?
//create a keepalive signal on the illucia unit, then automatically close the serial port if a timepassedSinceLastPacket > keepalive

import processing.serial.*;
import oscP5.*;
import netP5.*;

Serial serialConnection;
OscP5 oscP5;
NetAddress myRemoteLocation;

//Data ranges//////////////////////////////////////////////// 
//The microcontrollers I'm using have 10bit A->D converters...
final float microcontrollerLow = 0;
final float microcontrollerHigh = 1023;
//...which get mapped to this standardized range for the sake of seamless/universal communication with other software
final float OSCLow = 0.0;
final float OSCHigh = 1.0;

////Serial protocol////////////////////////////////////////////////
//incoming
//The hardware device will send a set of bytes upon state change (aka, a button pressed, jacks patched together, knob turned, etc) 
final int MESSAGE_SIZE = 4; //this is number of bytes in each serial message sent by the hardware 
//The first byte will denote the type of change being communicated. 
final byte CONTINUOUS_TYPE = 0; //Knobs
final byte DIGITAL_TYPE = 1; //Buttons & Switches
final byte JACK_TYPE = 2; //Jack connections & disconnections 

//outgoing
final byte RESET_TYPE = 0;
final byte LED_TYPE = 1;
///
boolean serialConnected = false; // has illucaConnect initialized a serial connection 
boolean handShakeCompleted = false; //has the device successfully completed a handshake with this program? 

//TODO: automatically get this from device at boot.
//Device details... 
String deviceName = "dtr";
final int NUMBER_OF_DIGITAL_ELEMENTS = 8;
final int NUMBER_OF_CONTINUOUS_ELEMENTS = 6;
final int NUMBER_OF_JACKS = 16;
final int NUMBER_OF_LEDS = 4;
//int versionNumber;

//Create a place to store illucia device state
float[] digitalElements;
float[] continuousElements;
Jack[] jacks; 

//Images to show connected/disconnected status
PImage[] connectionStatusImages;

void setup() {
  size(300, 300);  
  background(255);

  connectionStatusImages = new PImage[2];

  //try to load images
  connectionStatusImages[0] = loadImage("illuciaNotConnected.png");
  connectionStatusImages[1] = loadImage("illuciaConnected.png");
  //call the helper that renders text and images
  setConnectionStatus(false);

  noLoop(); //disable the draw() loop, as this program is event driven for now
  //low frame rates = less CPU usage. 
  frameRate(1); //useless.. at the moment, drawing is disabled in this sketch. Might get readded later though

  //initialize OSC 
  oscP5 = new OscP5(this, OSC_INCOMING_PORT);
  myRemoteLocation = new NetAddress("127.0.0.1", OSC_OUTGOING_PORT);

  //initialize serial
  connectOverSerial();

  //Setup the model of the device
  //buttons&switches
  digitalElements = new float[NUMBER_OF_DIGITAL_ELEMENTS];
  //knobs
  continuousElements = new float[NUMBER_OF_CONTINUOUS_ELEMENTS];
  //jacks...
  jacks = new Jack[NUMBER_OF_JACKS];
  for (int i = 0; i < jacks.length; i++) {
    jacks[i] = new Jack(i);
  }

  prepareExitHandler(); //sets up an event handled that is called upon program's closing. Tells device to reset to a new state and await a new handshake
}


void draw() {
}

//This gets called by the serial library when something on the device changes
void serialEvent(Serial port) {

  byte[] incoming = new byte[MESSAGE_SIZE];

  if (!handShakeCompleted && serialConnection.available () >= incoming.length && serialConnection.available() % incoming.length == 0) { //has this device connected before? if not, check for a initial handshake 

    println("Starting handshake..");

    /* if this is the first serialEvent, 
     make sure the connection has enough time to get setup before reading
     */
    delay(750); 

    serialConnection.readBytes(incoming);

    if (incoming[0] == 'd') {

      serialConnection.write('P');
      println("Handshake Complete");
      println("Bytes available after handshake and clear: "+serialConnection.available());

      //after handshake, update the display
      setConnectionStatus(true);
      handShakeCompleted = true;

      serialConnection.clear();
    }
  }

  else { //Initial handshake has already been established

    //println("Available: " + serialConnection.available());

    while (serialConnection.available () >= incoming.length && serialConnection.available() % incoming.length == 0) {

      serialConnection.readBytes(incoming);


      /*
      println("reading");
       for (int i = 0; i < incoming.length; i++) {
       println(i+"'s value: "+incoming[i]);
       } 
       */

      OscMessage messageToSend;

      switch(incoming[0]) { //Switch the serial bytes based on the byte that specifies message type

      case CONTINUOUS_TYPE:


        //what number element(knob) is this?
        int continuousElementIndex = incoming[1];

        float contValue = int(incoming[2] << 8) + int(incoming[3]);
        contValue = map(contValue, microcontrollerLow, microcontrollerHigh, OSCLow, OSCHigh);

        //update model of device
        continuousElements[continuousElementIndex] = contValue;

        //Now, broadcast the value change over OSC

        //control elements are indexed at 0 on the hardware, but 1 in software, so add 1 when sending this value out
        messageToSend = new OscMessage("/"+deviceName+"/Continuous/"+(continuousElementIndex + 1));  
        messageToSend.add(contValue);
        oscP5.send(messageToSend, myRemoteLocation);

        //println("ID: "+ incoming[1] + " value: "  +contValue);
        break;


      case DIGITAL_TYPE:

        //what number element(button or switch) is this?
        int digitalElementIndex = incoming[1];

        float value = 0.0;

        if (incoming[2] == 1) { //switch or button is activated
          value = 1.0;
        } 
        else if (incoming[2] == 0) { //switch or button is off
          value = 0.0;
        }

        //update model of device
        digitalElements[digitalElementIndex] = value;

        //now, broadcast the value change over OSC

        //control element numbers are indexed at 0 on the hardware, but 1 in software, so add 1
        messageToSend = new OscMessage("/"+deviceName+"/Digital/"+(digitalElementIndex+1));  
        messageToSend.add(value);
        oscP5.send(messageToSend, myRemoteLocation);

        //println("Button ID: "+ incoming[1] + " value: "  +incoming[2]);    
        break;


      case JACK_TYPE:
        //There was a jack connection or disconnection - 
        //update this utility's model of the patchbay accordingly, so that it can forward messages based on the state of patch bay & cables.

        Jack jack1 = jacks[incoming[1]]; //main jack being checked for updates
        Jack jack2 = jacks[incoming[2]]; //the jack that jack1 is connected to.

        float isJackPatched = 1.0; //defaulting to 1.0, which assumes most events will involve connection

        if (incoming[3] == 1) { //there is a new connection
          jack1.connections.add(jack2);
        } 
        else {  //a connection was broken
          jack1.connections.remove(jack2);

          if (jack1.connections.isEmpty()) { // if this jack isn't connected to anything else..
            isJackPatched = 0.0;
          }
        }

        //broadcast the connection or disconnection over OSC. (indicate jack index in the OSC message address)
        //the argument is 1.0 if the jack has connections, and 0.0 if it doesn't
        //todo: consider adding a second argument: an index list of all jacks connected? (if you do, dont forget to change it in the PollDeviceState too! refactor all this..)
        messageToSend = new OscMessage("/"+deviceName+"/JackIsPatched/"+(jack1.getID()+1)); //the hardware indexes from 0, so add 1 because software indexes from 0
        messageToSend.add(isJackPatched);    
        oscP5.send(messageToSend, myRemoteLocation);

        //     println("JACK ID: "+ jack1.getID() + "Other JACK ID: "+ jack2.getID() +" connection: "  +incoming[3]);
        //     println("connections:"+jack1.connections.size() );
        break;
      }
    }
  }
}

//handles patchbay forwarding, LED brightness, and  
void oscEvent(OscMessage messageReceived) {

  String[] msgSplit = messageReceived.addrPattern().split("/"); // splits OSC msg

  //If the OSC msg matches the name of the game
  if (msgSplit[1].equals(deviceName)) {

    if (msgSplit[2].equals("LED")) { //set an LED

      //println(Integer.parseInt(msgSplit[3]) + " " +mappedLEDBrightness);

      int mappedLEDBrightness = int(constrain(map(messageReceived.get(0).floatValue(), OSCLow, OSCHigh, 0.0, 255), 0, 255));
      int LEDNumber = Integer.parseInt(msgSplit[3]) - 1;

      setLED(LEDNumber, mappedLEDBrightness);
    }

    else if (msgSplit[2].equals("OutputJack")) { //receives messages to be sent out of a designated output jack

      //SHOULD ADD A CHECK HERE TO MAKE SURE THE JACK IS ACTUALLY WITHIN THE RANGE AVAILABLE ON THE INSTRUMENT? or go nuts and just mod everything, for good times..
      int jackNumber = Integer.parseInt(msgSplit[3]) - 1;

      ArrayList<Jack> connections = jacks[jackNumber].connections;

      for (int i = 0; i < connections.size(); i++) {
        int idToForwardTo = connections.get(i).getID() + 1;

        OscMessage messageBeingSent = new OscMessage("/"+deviceName+"/InputJack/"+ idToForwardTo);  
        messageBeingSent.setArguments(messageReceived.arguments());
        oscP5.send(messageBeingSent, myRemoteLocation);
      }
    }

    //this device automatically sends messages when values on the hardware change, but it also allows the user to poll the device. 
    //one might send a message to this address when opening a new patch, which will rebroadcast the state of switches, jack connections, etc
    else if (msgSplit[2].equals("PollDeviceState")) {

      OscMessage messageToSend;

      //buttons&switches
      for (int i = 0; i < digitalElements.length; i++) {
        messageToSend = new OscMessage("/"+deviceName+"/Digital/"+(i+1));  
        messageToSend.add(digitalElements[i]);
        oscP5.send(messageToSend, myRemoteLocation);
      }

      //knobs
      for (int i = 0; i < continuousElements.length; i++) {
        messageToSend = new OscMessage("/"+deviceName+"/Continuous/"+(i + 1));  
        messageToSend.add(continuousElements[i]);
        oscP5.send(messageToSend, myRemoteLocation);
      }

      //TODO: ADD list of jacks as an argument, if you decide to go that way!
      //for every jack, broadcast JackIsPatched state
      for (int i = 0; i < jacks.length; i++) {

        float isJackPatched = 1.0; 
        if (jacks[i].connections.isEmpty()) { //no connections? broadcast 0.
          isJackPatched = 0.0;
        }
        messageToSend = new OscMessage("/"+deviceName+"/JackIsPatched/"+(jacks[i].getID()+1)); //the hardware indexes from 0, so add 1 because software indexes from 0
        messageToSend.add(isJackPatched);    
        oscP5.send(messageToSend, myRemoteLocation);
      }
    }
  }
}



//Class used to keep track of connections
class Jack {

  int _id;
  ArrayList<Jack> connections; //stores the other jacks that this one is connected to

  Jack(int id) {
    connections = new ArrayList<Jack>();
    _id = id;
  }

  int getID() {
    return _id;
  }
}



//Helper - sends a brightness value to an LED.  
void setLED(int LEDNumber, int brightnessValue) {

  byte[] bytesToSend = new byte[3]; 

  //first byte is the type (LED), second is the LED number, next is the value
  bytesToSend[0] = LED_TYPE;
  bytesToSend[1] = byte(LEDNumber); 
  bytesToSend[2] = byte(brightnessValue);
  serialConnection.write(bytesToSend); //send the LED value over serial
}



//Helper method for drawing status updates, telling the user if an illucia unit connected

void setConnectionStatus(boolean isConnected) {
  background(255);
  textAlign(CENTER, CENTER);
  fill(0);
  if (isConnected) {
    if (connectionStatusImages[1] != null) {  //the image loaded successfully..
      image(connectionStatusImages[1], width/2 - connectionStatusImages[0].width/2, height/2 - connectionStatusImages[0].height/2);
    }
    else { //the image didn't load, just use text
      text("Connected!", width/2, height/2);
    }
  } 
  else {    //still waiting for connection
    //Processing doesn't throw errors on file not found, so make sure the image was loaded
    if (connectionStatusImages[0] != null) {  
      image(connectionStatusImages[0], width/2 - connectionStatusImages[0].width/2, height/2 - connectionStatusImages[0].height/2);
    } 
    else //the image didn't load, just use text
    text("Connecting...", width/2, height/2);
  }
  redraw(); //need to tell the sketch to redraw() because of the noLoop() call made earlier
}


//Calls code upon program's closing. Tells device to await a new handshake
private void prepareExitHandler() {

  Runtime.getRuntime().addShutdownHook(new Thread(new Runnable() {
    public void run () {
      println("Exiting.");

      //turn all the LEDs off
      for (int i = 0; i < NUMBER_OF_LEDS; i++) {
        setLED(i, 0);
      }

      serialConnection.write(RESET_TYPE); //send the LED value over serial

      try {
        stop();
      } 
      catch (Exception ex) {
        ex.printStackTrace(); // not much else to do at this point
      }
    }
  }
  ));
}   

void connectOverSerial() {

  String[] availableSerialPorts = Serial.list();

  for (int i = 0; i < availableSerialPorts.length; i++) {

    if (!serialConnected) {

      boolean connectionFailed = false;

      try {
        serialConnection = new Serial(this, availableSerialPorts[i], 9600);
        if (serialConnection.available() >= MESSAGE_SIZE) {
          serialConnected = true;
          serialConnection.buffer(MESSAGE_SIZE); //let the serial port know the number of bytes to check for with each mesage
        }
      } 
      catch (RuntimeException e) {
        //println("Couldn't connect on " + availableSerialPorts[i] + " - Trying another port");
      }
    }
  }
}
