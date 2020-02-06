# RetroKallaxControl
A project for a Wemos D1 Mini to read 16 switch states wit a muxer, set 16 relais (also with a muxer) and send commands to a HDMI switch via serial. All controlled trough nodered over MQTT.

## Wemos D1 Mini setup
| Pin    | Comment |
|--------|---------|
| D1-D4  | adress relais mux |
| D0     | send relais data pin |
| D5-D8  | address switch mux |
| A0     | read switch data pin |
| Tx, Rx | Serial data to HDMI switch with level shifter |

## MQTT Topics

### /RetroKallaxControl/discovery/<MAC>
Do make it discoverable, it publishes its `VERSION` to this topic.

### /RetroKallaxControl/<MAC>/trigger

### /RetroKallaxControl/<MAC>/atenCommand
This topic is subscribed and forwards all recieved strings to the serial port for the Aten HDMI switch.

### /RetroKallaxControl/<MAC>/relais
