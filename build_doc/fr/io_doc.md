
# AVReceiver
AVReceiver object to control network amplifier

## Parameters of AVReceiver
Name | Type | Mandatory | Description
---- | ---- | --------- | -----------
model | string | yes | AVReceiver model. Supported: pioneer, denon, onkyo, marantz, yamaha
zone | int | no | Zone of the amplifier (if supported)
host | string | yes | IP address of the device
logged | bool | no | If enabled, and if influxdb is enabled in local_config send the value to influxdb for this IO
io_type | string | yes | IO type, can be "input", "output", "inout"
gui_type | string | no | Internal graphical type for all calaos objects. Set automatically, read-only parameter.
enabled | bool | no | Enable the Input/Output. The default value is true. This parameter is added if it's not found in the configuration.
visible | bool | no | Display the Input/Output on all user interfaces if set. Default to true
port | int | no | Port to use for connection
log_history | bool | no | If enabled, write an entry in the history event log for this IO
name | string | yes | Name of Input/Output.
id | string | yes | Unique ID identifying the Input/Output in calaos-server

## Actions of AVReceiver
Name | Description
---- | -----------
custom XXXXXX | Send a custom command to receiver (if you know the protocol) 
 source X | Change current input source 
 volume 50 | Set current volume 
 power off | Switch receiver off 
 power on | Switch receiver on 
 
# Axis - UNDOCUMENTED IO
SPANK SPANK SPANK : naughty programmer ! You did not add documentation for this IO, that's BAD :'(
Go document it in your code or you will burn in hell!



Axis IP Camera/Encoder. Camera can be viewed directly inside calaos and used in rules.

## Parameters of Axis
Name | Type | Mandatory | Description
---- | ---- | --------- | -----------
resolution | string | no | Resolution to use
name | string | yes | Name of Input/Output.
tilt_framesize | string | no | 
log_history | bool | no | If enabled, write an entry in the history event log for this IO
visible | bool | no | Display the Input/Output on all user interfaces if set. Default to true
id | string | yes | Unique ID identifying the Input/Output in calaos-server
gui_type | string | no | Internal graphical type for all calaos objects. Set automatically, read-only parameter.
io_type | string | yes | IO type, can be "input", "output", "inout"
logged | bool | no | If enabled, and if influxdb is enabled in local_config send the value to influxdb for this IO
rotate | int | no | Rotate the image. Set a value between. The value is in degrees. Example : -90 for  Counter Clock Wise rotation, 90 for Clock Wise rotationCW.
pan_framesize | string | no | 
model | string | yes | Camera model/chanel to use
width | int | no | Width of the image, if this parameter is set, video will be resized to fit the given width. Let parameter empty to keep the original size.
ptz | bool | no | Set to true if camera has PTZ support
enabled | bool | no | Enable the Input/Output. The default value is true. This parameter is added if it's not found in the configuration.
zoom_step | string | no | 


# BlinkstickOutputLightRGB
RGB Light dimmer using a Blinkstick


RGB light. Choose a color to be set for this light.

## Parameters of BlinkstickOutputLightRGB
Name | Type | Mandatory | Description
---- | ---- | --------- | -----------
nb_leds | int | yes | Number of LEDs to control with the blinkstick
logged | bool | no | If enabled, and if influxdb is enabled in local_config send the value to influxdb for this IO
io_type | string | yes | IO type, can be "input", "output", "inout"
gui_type | string | no | Internal graphical type for all calaos objects. Set automatically, read-only parameter.
enabled | bool | no | Enable the Input/Output. The default value is true. This parameter is added if it's not found in the configuration.
visible | bool | no | Display the Input/Output on all user interfaces if set. Default to true
log_history | bool | no | If enabled, write an entry in the history event log for this IO
name | string | yes | Name of Input/Output.
serial | string | yes | Blinkstick serial to control
id | string | yes | Unique ID identifying the Input/Output in calaos-server

## Conditions of BlinkstickOutputLightRGB
Name | Description
---- | -----------
value | Event when light is at this value 
 changed | Event on any change of value 
 
## Actions of BlinkstickOutputLightRGB
Name | Description
---- | -----------
down_red 5 | Decrease intensity by X percent of red channel 
 true | Switch the light on 
 false | Switch the light off 
 set_state false | Update internal light state without starting real action. This is useful when having updating the light state from an external source. 
 down_green 5 | Decrease intensity by X percent of green channel 
 toggle | Invert the light state (ON/OFF) 
 set_state true | Update internal light state without starting real action. This is useful when having updating the light state from an external source. 
 down_blue 5 | Decrease intensity by X percent of blue channel 
 set_state #AA1294 | Update internal light state without starting real action. This is useful when having updating the light state from an external source. 
 up_red 5 | Increase intensity by X percent of red channel 
 set_red 50 | Set red channel to X percent 
 set_blue 50 | Set blue channel to X percent 
 up_green 5 | Increase intensity by X percent of green channel 
 set_green 50 | Set green channel to X percent 
 set #AA1294 | Set color. Color can be represented by using HTML notation: #AABBCC, rgb(50, 10, 30), hsl(11, 22, 33) 
 up_blue 5 | Increase intensity by X percent of blue channel 
 
## More Infos
* OLA: http://www.blinkstick.com

# Foscam - UNDOCUMENTED IO
SPANK SPANK SPANK : naughty programmer ! You did not add documentation for this IO, that's BAD :'(
Go document it in your code or you will burn in hell!



Foscam IP Camera/Encoder. Camera can be viewed directly inside calaos and used in rules.

## Parameters of Foscam
Name | Type | Mandatory | Description
---- | ---- | --------- | -----------
name | string | yes | Name of Input/Output.
log_history | bool | no | If enabled, write an entry in the history event log for this IO
password | string | yes | Password for user
visible | bool | no | Display the Input/Output on all user interfaces if set. Default to true
id | string | yes | Unique ID identifying the Input/Output in calaos-server
gui_type | string | no | Internal graphical type for all calaos objects. Set automatically, read-only parameter.
io_type | string | yes | IO type, can be "input", "output", "inout"
logged | bool | no | If enabled, and if influxdb is enabled in local_config send the value to influxdb for this IO
host | string | yes | IP Address
rotate | int | no | Rotate the image. Set a value between. The value is in degrees. Example : -90 for  Counter Clock Wise rotation, 90 for Clock Wise rotationCW.
port | string | yes | Port number
width | int | no | Width of the image, if this parameter is set, video will be resized to fit the given width. Let parameter empty to keep the original size.
ptz | bool | no | Set to true if camera has PTZ support
enabled | bool | no | Enable the Input/Output. The default value is true. This parameter is added if it's not found in the configuration.
zoom_step | string | no | 
username | string | yes | Username for accessing the camera

# Gadspot - UNDOCUMENTED IO
SPANK SPANK SPANK : naughty programmer ! You did not add documentation for this IO, that's BAD :'(
Go document it in your code or you will burn in hell!



Gadspot IP Camera. Camera can be viewed directly inside calaos and used in rules.

## Parameters of Gadspot
Name | Type | Mandatory | Description
---- | ---- | --------- | -----------
rotate | int | no | Rotate the image. Set a value between. The value is in degrees. Example : -90 for  Counter Clock Wise rotation, 90 for Clock Wise rotationCW.
logged | bool | no | If enabled, and if influxdb is enabled in local_config send the value to influxdb for this IO
width | int | no | Width of the image, if this parameter is set, video will be resized to fit the given width. Let parameter empty to keep the original size.
io_type | string | yes | IO type, can be "input", "output", "inout"
gui_type | string | no | Internal graphical type for all calaos objects. Set automatically, read-only parameter.
enabled | bool | no | Enable the Input/Output. The default value is true. This parameter is added if it's not found in the configuration.
visible | bool | no | Display the Input/Output on all user interfaces if set. Default to true
log_history | bool | no | If enabled, write an entry in the history event log for this IO
name | string | yes | Name of Input/Output.
id | string | yes | Unique ID identifying the Input/Output in calaos-server


# GpioInputSwitch
Input switch with a GPIO


Basic switch with press/release states.

## Parameters of GpioInputSwitch
Name | Type | Mandatory | Description
---- | ---- | --------- | -----------
gpio | int | yes | GPIO ID on your hardware
logged | bool | no | If enabled, and if influxdb is enabled in local_config send the value to influxdb for this IO
io_type | string | yes | IO type, can be "input", "output", "inout"
gui_type | string | no | Internal graphical type for all calaos objects. Set automatically, read-only parameter.
enabled | bool | no | Enable the Input/Output. The default value is true. This parameter is added if it's not found in the configuration.
visible | bool | no | A switch can't be visible. Always false.
log_history | bool | no | If enabled, write an entry in the history event log for this IO
name | string | yes | Name of Input/Output.
active_low | bool | no | Set this if your GPIO has an inverted level
id | string | yes | Unique ID identifying the Input/Output in calaos-server

## Conditions of GpioInputSwitch
Name | Description
---- | -----------
changed | Event on any change of state 
 false | Event triggered when switch is released 
 true | Event triggered when switch is pressed 
 

# GpioInputSwitchLongPress
Input switch with a GPIO


Long press switch. This switch supports single press and long press. User has 500ms to perform the long press.

## Parameters of GpioInputSwitchLongPress
Name | Type | Mandatory | Description
---- | ---- | --------- | -----------
gpio | int | yes | GPIO ID on your hardware
logged | bool | no | If enabled, and if influxdb is enabled in local_config send the value to influxdb for this IO
io_type | string | yes | IO type, can be "input", "output", "inout"
gui_type | string | no | Internal graphical type for all calaos objects. Set automatically, read-only parameter.
enabled | bool | no | Enable the Input/Output. The default value is true. This parameter is added if it's not found in the configuration.
visible | bool | no | A switch can't be visible. Always false.
log_history | bool | no | If enabled, write an entry in the history event log for this IO
name | string | yes | Name of Input/Output.
active_low | bool | no | Set this is your GPIO has an inverted level
id | string | yes | Unique ID identifying the Input/Output in calaos-server

## Conditions of GpioInputSwitchLongPress
Name | Description
---- | -----------
changed | Event on any change of state 
 2 | Event triggered when switch is pressed at least for 500ms (long press) 
 1 | Event triggered when switch is pressed quickly 
 

# GpioInputSwitchTriple
Input switch with a GPIO


Triple click switch. This switch can start 3 kind of actions. User has 500ms to do a multiple click.

## Parameters of GpioInputSwitchTriple
Name | Type | Mandatory | Description
---- | ---- | --------- | -----------
gpio | int | yes | GPIO ID on your hardware
logged | bool | no | If enabled, and if influxdb is enabled in local_config send the value to influxdb for this IO
io_type | string | yes | IO type, can be "input", "output", "inout"
gui_type | string | no | Internal graphical type for all calaos objects. Set automatically, read-only parameter.
enabled | bool | no | Enable the Input/Output. The default value is true. This parameter is added if it's not found in the configuration.
visible | bool | no | A switch can't be visible. Always false.
log_history | bool | no | If enabled, write an entry in the history event log for this IO
name | string | yes | Name of Input/Output.
active_low | bool | no | Set this is your GPIO has an inverted level
id | string | yes | Unique ID identifying the Input/Output in calaos-server

## Conditions of GpioInputSwitchTriple
Name | Description
---- | -----------
changed | Event on any change of state 
 2 | Event triggered when switch is double clicked 
 3 | Event triggered when switch is triple clicked 
 1 | Event triggered when switch is single clicked 
 

# GpioOutputShutter
Shutter with 2 GPIOs


Simple shutter. This shutter supports open/close states, as well as impulse shutters.

## Parameters of GpioOutputShutter
Name | Type | Mandatory | Description
---- | ---- | --------- | -----------
active_low_up | bool | no | Set this is your GPIO has an inverted level
active_low_down | bool | no | Set this is your GPIO has an inverted level
name | string | yes | Name of Input/Output.
log_history | bool | no | If enabled, write an entry in the history event log for this IO
visible | bool | no | Display the Input/Output on all user interfaces if set. Default to true
enabled | bool | no | Enable the Input/Output. The default value is true. This parameter is added if it's not found in the configuration.
id | string | yes | Unique ID identifying the Input/Output in calaos-server
gui_type | string | no | Internal graphical type for all calaos objects. Set automatically, read-only parameter.
gpio_down | int | yes | GPIO ID for closing on your hardware
io_type | string | yes | IO type, can be "input", "output", "inout"
logged | bool | no | If enabled, and if influxdb is enabled in local_config send the value to influxdb for this IO
stop_both | bool | no | If in impulse mode, some shutters needs to activate both up dans down relays when stopping the shutter
time | int | yes | Time in sec for shutter to open or close
impulse_time | int | no | Impulse time for shutter that needs impulse instead of holding up/down relays. If set to 0 impulse shutter is disabled. Time is in ms. Default to 0
gpio_up | int | yes | GPIO ID for opening on your hardware

## Conditions of GpioOutputShutter
Name | Description
---- | -----------
false | Event when shutter is closed 
 true | Event when shutter is open 
 changed | Event on any change of shutter state 
 
## Actions of GpioOutputShutter
Name | Description
---- | -----------
impulse up 200 | Open shutter for X ms 
 set_state true | Update internal shutter state without starting real action. This is useful when having updating the shutter state from an external source. 
 impulse down 200 | Close shutter for X ms 
 toggle | Invert shutter state 
 set_state false | Update internal shutter state without starting real action. This is useful when having updating the shutter state from an external source. 
 stop | Stop the shutter 
 down | Close the shutter 
 up | Open the shutter 
 

# GpioOutputShutterSmart
Shutter with 2 GPIOs


Smart shutter. This shutter calculates the position of the shutter based on the time it takes to open and close. It then allows to set directly the shutter at a specified position.

## Parameters of GpioOutputShutterSmart
Name | Type | Mandatory | Description
---- | ---- | --------- | -----------
active_low_up | bool | no | Set this is your GPIO has an inverted level
gpio_down | int | yes | GPIO ID for closing on your hardware
active_low_down | bool | no | Set this is your GPIO has an inverted level
name | string | yes | Name of Input/Output.
log_history | bool | no | If enabled, write an entry in the history event log for this IO
visible | bool | no | Display the Input/Output on all user interfaces if set. Default to true
enabled | bool | no | Enable the Input/Output. The default value is true. This parameter is added if it's not found in the configuration.
id | string | yes | Unique ID identifying the Input/Output in calaos-server
gui_type | string | no | Internal graphical type for all calaos objects. Set automatically, read-only parameter.
time_up | int | yes | Time in sec for shutter to be fully open. The more accurate, the better it will work
io_type | string | yes | IO type, can be "input", "output", "inout"
logged | bool | no | If enabled, and if influxdb is enabled in local_config send the value to influxdb for this IO
time_down | int | yes | Time in sec for shutter to fully closed. The more accurate, the better it will work
stop_both | bool | no | If in impulse mode, some shutters needs to activate both up dans down relays when stopping the shutter
impulse_time | int | no | Impulse time for shutter that needs impulse instead of holding up/down relays. If set to 0 impulse shutter is disabled. Time is in ms. Default to 0
gpio_up | int | yes | GPIO ID for opening on your hardware

## Conditions of GpioOutputShutterSmart
Name | Description
---- | -----------
false | Event when shutter is closed 
 true | Event when shutter is open 
 changed | Event on any change of shutter state 
 
## Actions of GpioOutputShutterSmart
Name | Description
---- | -----------
up 5 | Open the shutter by X percent 
 set 50 | Set shutter at position X in percent 
 impulse up 200 | Open shutter for X ms 
 set_state true | Update internal shutter state without starting real action. This is useful when having updating the shutter state from an external source. 
 down 5 | Close the shutter by X percent 
 impulse down 200 | Close shutter for X ms 
 toggle | Invert shutter state 
 set_state false | Update internal shutter state without starting real action. This is useful when having updating the shutter state from an external source. 
 calibrate | Start calibration on shutter. This opens fully the shutter and resets all internal position values. Use this if shutter sync is lost. 
 stop | Stop the shutter 
 down | Close the shutter 
 up | Open the shutter 
 

# GpioOutputSwitch
Light with a GPIO


Basic light. This light have only 2 states, ON or OFF. Can also be used to control simple relays output

## Parameters of GpioOutputSwitch
Name | Type | Mandatory | Description
---- | ---- | --------- | -----------
gpio | int | yes | GPIO ID on your hardware
logged | bool | no | If enabled, and if influxdb is enabled in local_config send the value to influxdb for this IO
io_type | string | yes | IO type, can be "input", "output", "inout"
gui_type | string | no | Internal graphical type for all calaos objects. Set automatically, read-only parameter.
enabled | bool | no | Enable the Input/Output. The default value is true. This parameter is added if it's not found in the configuration.
visible | bool | no | Display the Input/Output on all user interfaces if set. Default to true
log_history | bool | no | If enabled, write an entry in the history event log for this IO
name | string | yes | Name of Input/Output.
active_low | bool | no | Set this is your GPIO has an inverted level
io_style | list | yes | GUI style display. This will control the icon displayed on the UI
id | string | yes | Unique ID identifying the Input/Output in calaos-server

## Conditions of GpioOutputSwitch
Name | Description
---- | -----------
false | Event when light is off 
 true | Event when light is on 
 changed | Event on any change of value 
 
## Actions of GpioOutputSwitch
Name | Description
---- | -----------
impulse 200 | Do an impulse on light state. Set to true for X ms then reset to false 
 set_state true | Update internal light state without starting real action. This is useful when having updating the light state from an external source. 
 impulse 500 200 500 200 | Do an impulse on light state with a pattern.<br>Ex: 500 200 500 200 means: TRUE for 500ms, FALSE for 200ms, TRUE for 500ms, FALSE for 200ms<br>Ex: 500 loop 200 300 means: TRUE for 500ms, then loop the next steps for infinite, FALSE for 200ms, TRUE for 300ms<br>Ex: 100 100 200 old means: blinks and then set to the old start state (before impulse starts) 
 toggle | Invert light state 
 set_state false | Update internal light state without starting real action. This is useful when having updating the light state from an external source. 
 false | Switch the light off 
 true | Switch the light on 
 

# HueOutputLightRGB
RGB Light dimmer using a Philips Hue


RGB light. Choose a color to be set for this light.

## Parameters of HueOutputLightRGB
Name | Type | Mandatory | Description
---- | ---- | --------- | -----------
api | string | yes | API key return by Hue bridge when assciation has been made. Use Hue Wizard in calaos_installer to get this value automatically.
host | string | yes | Hue bridge IP address
logged | bool | no | If enabled, and if influxdb is enabled in local_config send the value to influxdb for this IO
io_type | string | yes | IO type, can be "input", "output", "inout"
id_hue | string | yes | Unique ID describing the Hue Light. This value is returned by the Hue Wizard.
gui_type | string | no | Internal graphical type for all calaos objects. Set automatically, read-only parameter.
enabled | bool | no | Enable the Input/Output. The default value is true. This parameter is added if it's not found in the configuration.
visible | bool | no | Display the Input/Output on all user interfaces if set. Default to true
log_history | bool | no | If enabled, write an entry in the history event log for this IO
name | string | yes | Name of Input/Output.
id | string | yes | Unique ID identifying the Input/Output in calaos-server

## Conditions of HueOutputLightRGB
Name | Description
---- | -----------
value | Event when light is at this value 
 changed | Event on any change of value 
 
## Actions of HueOutputLightRGB
Name | Description
---- | -----------
down_red 5 | Decrease intensity by X percent of red channel 
 true | Switch the light on 
 false | Switch the light off 
 set_state false | Update internal light state without starting real action. This is useful when having updating the light state from an external source. 
 down_green 5 | Decrease intensity by X percent of green channel 
 toggle | Invert the light state (ON/OFF) 
 set_state true | Update internal light state without starting real action. This is useful when having updating the light state from an external source. 
 down_blue 5 | Decrease intensity by X percent of blue channel 
 set_state #AA1294 | Update internal light state without starting real action. This is useful when having updating the light state from an external source. 
 up_red 5 | Increase intensity by X percent of red channel 
 set_red 50 | Set red channel to X percent 
 set_blue 50 | Set blue channel to X percent 
 up_green 5 | Increase intensity by X percent of green channel 
 set_green 50 | Set green channel to X percent 
 set #AA1294 | Set color. Color can be represented by using HTML notation: #AABBCC, rgb(50, 10, 30), hsl(11, 22, 33) 
 up_blue 5 | Increase intensity by X percent of blue channel 
 
## More Infos
* Meet Hue: http://www.meethue.com


# InputTime
Basic time input. An event is triggered when the current time equals the configured time. A specific date can also be set.

## Parameters of InputTime
Name | Type | Mandatory | Description
---- | ---- | --------- | -----------
log_history | bool | no | If enabled, write an entry in the history event log for this IO
visible | bool | no | A time object can't be visible. Always false.
year | int | no | Year for this time input
enabled | bool | no | Enable the Input/Output. The default value is true. This parameter is added if it's not found in the configuration.
id | string | yes | Unique ID identifying the Input/Output in calaos-server
gui_type | string | no | Internal graphical type for all calaos objects. Set automatically, read-only parameter.
min | int | yes | Minutes for this time input
io_type | string | yes | IO type, can be "input", "output", "inout"
logged | bool | no | If enabled, and if influxdb is enabled in local_config send the value to influxdb for this IO
day | int | no | Day for this time input
hour | int | yes | Hour for this time input
sec | int | yes | Seconds for this time input
name | string | yes | Name of Input/Output.
month | int | no | Month for this time input

## Conditions of InputTime
Name | Description
---- | -----------
changed | Event on any change of time 
 false | Event triggered when current time is not equal 
 true | Event triggered when current time equals 
 

# InputTimer
Timer object. trigger an event after the configured time has expired.

## Parameters of InputTimer
Name | Type | Mandatory | Description
---- | ---- | --------- | -----------
autorestart | bool | yes | Auto restart the timer when time expires
name | string | yes | Name of Input/Output.
log_history | bool | no | If enabled, write an entry in the history event log for this IO
enabled | bool | no | Enable the Input/Output. The default value is true. This parameter is added if it's not found in the configuration.
visible | bool | no | A timer object can't be visible. Always false.
msec | int | yes | Miliseconds for the timer interval
id | string | yes | Unique ID identifying the Input/Output in calaos-server
gui_type | string | no | Internal graphical type for all calaos objects. Set automatically, read-only parameter.
min | int | yes | Minutes for the timer interval
io_type | string | yes | IO type, can be "input", "output", "inout"
logged | bool | no | If enabled, and if influxdb is enabled in local_config send the value to influxdb for this IO
hour | int | yes | Hour for the timer interval
autostart | bool | yes | Auto start the timer when calaos starts
sec | int | yes | Seconds for the timer interval

## Conditions of InputTimer
Name | Description
---- | -----------
change | Event triggered on any change 
 false | Event triggered when timer starts 
 true | Event triggered when timer expires 
 
## Actions of InputTimer
Name | Description
---- | -----------
00:00:00:200 | Reset the configured time to a value. Format is h:m:s:ms 
 stop | Stop the timer 
 start | Start the timer 
 

# InternalBool
Internal boolean object. This object is useful for doing internal programing in rules, like keeping boolean states, or displaying boolean values

## Parameters of InternalBool
Name | Type | Mandatory | Description
---- | ---- | --------- | -----------
rw | bool | no | Enable edit mode for this object. It allows user to modify the value on interfaces. Default to false
logged | bool | no | If enabled, and if influxdb is enabled in local_config send the value to influxdb for this IO
io_type | string | yes | IO type, can be "input", "output", "inout"
gui_type | string | no | Internal graphical type for all calaos objects. Set automatically, read-only parameter.
enabled | bool | no | Enable the Input/Output. The default value is true. This parameter is added if it's not found in the configuration.
save | bool | no | Automatically save the value in cache. The value will be reloaded when restarting calaos is true. Default to false
visible | bool | no | Display the Input/Output on all user interfaces if set. Default to true
log_history | bool | no | If enabled, write an entry in the history event log for this IO
name | string | yes | Name of Input/Output.
id | string | yes | Unique ID identifying the Input/Output in calaos-server

## Conditions of InternalBool
Name | Description
---- | -----------
changed | Event on any change of value 
 false | Event when value is false 
 true | Event when value is true 
 
## Actions of InternalBool
Name | Description
---- | -----------
impulse 200 | Do an impulse on boolean value. Set to true for X ms then reset to false 
 impulse 500 200 500 200 | Do an impulse on boolean value with a pattern.<br>Ex: 500 200 500 200 means: TRUE for 500ms, FALSE for 200ms, TRUE for 500ms, FALSE for 200ms<br>Ex: 500 loop 200 300 means: TRUE for 500ms, then loop the next steps for infinite, FALSE for 200ms, TRUE for 300ms<br>Ex: 100 100 200 old means: blinks and then set to the old start state (before impulse starts) 
 toggle | Invert boolean value 
 false | Set a value to false 
 true | Set a value to true 
 

# InternalInt
Internal number object. This object is useful for doing internal programing in rules, like counters, of displaying values.

## Parameters of InternalInt
Name | Type | Mandatory | Description
---- | ---- | --------- | -----------
rw | bool | no | Enable edit mode for this object. It allows user to modify the value on interfaces. Default to false
unit | string | no | Unit which will be displayed on the UI as a suffix.
logged | bool | no | If enabled, and if influxdb is enabled in local_config send the value to influxdb for this IO
io_type | string | yes | IO type, can be "input", "output", "inout"
gui_type | string | no | Internal graphical type for all calaos objects. Set automatically, read-only parameter.
enabled | bool | no | Enable the Input/Output. The default value is true. This parameter is added if it's not found in the configuration.
save | bool | no | Automatically save the value in cache. The value will be reloaded when restarting calaos is true. Default to false
step | float | no | Set a step for increment/decrement value. Default is 1.0
visible | bool | no | Display the Input/Output on all user interfaces if set. Default to true
log_history | bool | no | If enabled, write an entry in the history event log for this IO
name | string | yes | Name of Input/Output.
id | string | yes | Unique ID identifying the Input/Output in calaos-server

## Conditions of InternalInt
Name | Description
---- | -----------
changed | Event on any change of value 
 0 | Event on a specific number value 
 
## Actions of InternalInt
Name | Description
---- | -----------
dec 1 | Decrement value by value 
 inc 1 | Increment value by value 
 dec | Decrement value with configured step 
 inc | Increment value with configured step 
 0 | Set a specific number value 
 

# InternalString
Internal string object. This object is useful for doing internal programing in rules or displaying text values on user interfaces.

## Parameters of InternalString
Name | Type | Mandatory | Description
---- | ---- | --------- | -----------
rw | bool | no | Enable edit mode for this object. It allows user to modify the value on interfaces. Default to false
logged | bool | no | If enabled, and if influxdb is enabled in local_config send the value to influxdb for this IO
io_type | string | yes | IO type, can be "input", "output", "inout"
gui_type | string | no | Internal graphical type for all calaos objects. Set automatically, read-only parameter.
enabled | bool | no | Enable the Input/Output. The default value is true. This parameter is added if it's not found in the configuration.
save | bool | no | Automatically save the value in cache. The value will be reloaded when restarting calaos is true. Default to false
visible | bool | no | Display the Input/Output on all user interfaces if set. Default to true
log_history | bool | no | If enabled, write an entry in the history event log for this IO
name | string | yes | Name of Input/Output.
id | string | yes | Unique ID identifying the Input/Output in calaos-server

## Conditions of InternalString
Name | Description
---- | -----------
changed | Event on any change of value 
 value | Event on a specific string value 
 
## Actions of InternalString
Name | Description
---- | -----------
value | Set a specific string value 
 

# KNXInputAnalog
Input analog with KNX and eibnetmux


An analog input can be used to read analog values to display them and use them in rules.

## Parameters of KNXInputAnalog
Name | Type | Mandatory | Description
---- | ---- | --------- | -----------
read_at_start | bool | yes | Send a read request at start to get the current value. Default is false
eis | int | no | KNX EIS (Data type)
listen_knx_group | string | no | KNX Group address for listening status, Ex: x/y/z
precision | int | no | Precision of the returned value. The value represents the number of decimal after the dot. The value is rounded like this : value = 19.275 => returned value 19.28 when preicision = 2, 19.3 when precision = 1, 19 when precision = 0
name | string | yes | Name of Input/Output.
io_style | list | yes | GUI style display. This will control the icon displayed on the UI
log_history | bool | no | If enabled, write an entry in the history event log for this IO
visible | bool | no | Display the Input/Output on all user interfaces if set. Default to true
display_warning | bool | no | Display a warning if value has not been updated for a long time. Default to true
enabled | bool | no | Enable the Input/Output. The default value is true. This parameter is added if it's not found in the configuration.
unit | string | no | Unit which will be displayed on the UI as a suffix.
coeff_a | float | no | use in conjunction of coeff_b to apply equation of the form `value_displayed = coeff_a * raw_value + coeff_b`. Default value is 1.0.
knx_group | string | yes | KNX Group address, Ex: x/y/z
io_type | string | yes | IO type, can be "input", "output", "inout"
logged | bool | no | If enabled, and if influxdb is enabled in local_config send the value to influxdb for this IO
coeff_b | float | no | use in conjunction of coeff_a to apply equation of the form `value_displayed = coeff_a * raw_value + coeff_b`. Default value is 0.0
host | string | yes | Hostname of knxd, default to localhost
offset | float | no | same as coeff_b, can be used alone. Default value is 0.0
id | string | yes | Unique ID identifying the Input/Output in calaos-server
gui_type | string | no | Internal graphical type for all calaos objects. Set automatically, read-only parameter.
period | float | no | Sampling time in microsecond. The value is read at this frequency. If this value is not set, calaos tries to read the interval parameter
interval | float | no | Sampling time in seconds. The value is read at this frequency. If this value is not set, the default value is 15s

## Conditions of KNXInputAnalog
Name | Description
---- | -----------
changed | Event on any change of value 
 value | Event on a specific value 
 
## More Infos
* knxd: https://github.com/knxd/knxd/g


# KNXInputSwitch
Input switch with KNX and eibnetmux


Basic switch with press/release states.

## Parameters of KNXInputSwitch
Name | Type | Mandatory | Description
---- | ---- | --------- | -----------
host | string | yes | Hostname of knxd, default to localhost
read_at_start | bool | yes | Send a read request at start to get the current value. Default is false
listen_knx_group | string | no | KNX Group address for listening status, Ex: x/y/z
knx_group | string | yes | KNX Group address, Ex: x/y/z
logged | bool | no | If enabled, and if influxdb is enabled in local_config send the value to influxdb for this IO
io_type | string | yes | IO type, can be "input", "output", "inout"
gui_type | string | no | Internal graphical type for all calaos objects. Set automatically, read-only parameter.
enabled | bool | no | Enable the Input/Output. The default value is true. This parameter is added if it's not found in the configuration.
eis | int | no | KNX EIS (Data type)
visible | bool | no | A switch can't be visible. Always false.
log_history | bool | no | If enabled, write an entry in the history event log for this IO
name | string | yes | Name of Input/Output.
id | string | yes | Unique ID identifying the Input/Output in calaos-server

## Conditions of KNXInputSwitch
Name | Description
---- | -----------
changed | Event on any change of state 
 false | Event triggered when switch is released 
 true | Event triggered when switch is pressed 
 
## More Infos
* knxd: https://github.com/knxd/knxd/g


# KNXInputSwitchLongPress
Input switch long press with KNX and eibnetmux


Long press switch. This switch supports single press and long press. User has 500ms to perform the long press.

## Parameters of KNXInputSwitchLongPress
Name | Type | Mandatory | Description
---- | ---- | --------- | -----------
host | string | yes | Hostname of knxd, default to localhost
read_at_start | bool | yes | Send a read request at start to get the current value. Default is false
listen_knx_group | string | no | KNX Group address for listening status, Ex: x/y/z
knx_group | string | yes | KNX Group address, Ex: x/y/z
logged | bool | no | If enabled, and if influxdb is enabled in local_config send the value to influxdb for this IO
io_type | string | yes | IO type, can be "input", "output", "inout"
gui_type | string | no | Internal graphical type for all calaos objects. Set automatically, read-only parameter.
enabled | bool | no | Enable the Input/Output. The default value is true. This parameter is added if it's not found in the configuration.
eis | int | no | KNX EIS (Data type)
visible | bool | no | A switch can't be visible. Always false.
log_history | bool | no | If enabled, write an entry in the history event log for this IO
name | string | yes | Name of Input/Output.
id | string | yes | Unique ID identifying the Input/Output in calaos-server

## Conditions of KNXInputSwitchLongPress
Name | Description
---- | -----------
changed | Event on any change of state 
 2 | Event triggered when switch is pressed at least for 500ms (long press) 
 1 | Event triggered when switch is pressed quickly 
 
## More Infos
* knxd: https://github.com/knxd/knxd/g


# KNXInputSwitchTriple
Input switch triple with KNX and eibnetmux


Triple click switch. This switch can start 3 kind of actions. User has 500ms to do a multiple click.

## Parameters of KNXInputSwitchTriple
Name | Type | Mandatory | Description
---- | ---- | --------- | -----------
host | string | yes | Hostname of knxd, default to localhost
read_at_start | bool | yes | Send a read request at start to get the current value. Default is false
listen_knx_group | string | no | KNX Group address for listening status, Ex: x/y/z
knx_group | string | yes | KNX Group address, Ex: x/y/z
logged | bool | no | If enabled, and if influxdb is enabled in local_config send the value to influxdb for this IO
io_type | string | yes | IO type, can be "input", "output", "inout"
gui_type | string | no | Internal graphical type for all calaos objects. Set automatically, read-only parameter.
enabled | bool | no | Enable the Input/Output. The default value is true. This parameter is added if it's not found in the configuration.
eis | int | no | KNX EIS (Data type)
visible | bool | no | A switch can't be visible. Always false.
log_history | bool | no | If enabled, write an entry in the history event log for this IO
name | string | yes | Name of Input/Output.
id | string | yes | Unique ID identifying the Input/Output in calaos-server

## Conditions of KNXInputSwitchTriple
Name | Description
---- | -----------
changed | Event on any change of state 
 2 | Event triggered when switch is double clicked 
 3 | Event triggered when switch is triple clicked 
 1 | Event triggered when switch is single clicked 
 
## More Infos
* knxd: https://github.com/knxd/knxd/g


# KNXInputTemp
Input temperature with KNX and eibnetmux


Temperature sensor input. Use for displaying temperature and to control heating devices with rules based on temperature value

## Parameters of KNXInputTemp
Name | Type | Mandatory | Description
---- | ---- | --------- | -----------
read_at_start | bool | yes | Send a read request at start to get the current value. Default is false
eis | int | no | KNX EIS (Data type)
listen_knx_group | string | no | KNX Group address for listening status, Ex: x/y/z
precision | int | no | Precision of the returned value. The value represents the number of decimal after the dot. The value is rounded like this : value = 19.275 => returned value 19.28 when preicision = 2, 19.3 when precision = 1, 19 when precision = 0
name | string | yes | Name of Input/Output.
log_history | bool | no | If enabled, write an entry in the history event log for this IO
visible | bool | no | Display the Input/Output on all user interfaces if set. Default to true
display_warning | bool | no | Display a warning if value has not been updated for a long time. Default to true
enabled | bool | no | Enable the Input/Output. The default value is true. This parameter is added if it's not found in the configuration.
coeff_a | float | no | use in conjunction of coeff_b to apply equation of the form `value_displayed = coeff_a * raw_value + coeff_b`. Default value is 1.0.
knx_group | string | yes | KNX Group address, Ex: x/y/z
io_type | string | yes | IO type, can be "input", "output", "inout"
logged | bool | no | If enabled, and if influxdb is enabled in local_config send the value to influxdb for this IO
coeff_b | float | no | use in conjunction of coeff_a to apply equation of the form `value_displayed = coeff_a * raw_value + coeff_b`. Default value is 0.0
host | string | yes | Hostname of knxd, default to localhost
offset | float | no | same as coeff_b, can be used alone. Default value is 0.0
id | string | yes | Unique ID identifying the Input/Output in calaos-server
gui_type | string | no | Internal graphical type for all calaos objects. Set automatically, read-only parameter.
period | float | no | Sampling time in microsecond. The value is read at this frequency. If this value is not set, calaos tries to read the interval parameter
interval | float | no | Sampling time in seconds. The value is read at this frequency. If this value is not set, the default value is 15s

## Conditions of KNXInputTemp
Name | Description
---- | -----------
changed | Event on any change of temperature value 
 value | Event on a temperature value in degree Celsius 
 
## More Infos
* knxd: https://github.com/knxd/knxd/g


# KNXOutputAnalog
Analog output with KNX and eibnetmux


Analog output. Useful to control analog output devices connected to calaos.

## Parameters of KNXOutputAnalog
Name | Type | Mandatory | Description
---- | ---- | --------- | -----------
host | string | yes | Hostname of knxd, default to localhost
read_at_start | bool | yes | Send a read request at start to get the current value. Default is false
eis | int | no | KNX EIS (Data type)
listen_knx_group | string | no | KNX Group address for listening status, Ex: x/y/z
name | string | yes | Name of Input/Output.
io_style | list | yes | GUI style display. This will control the icon displayed on the UI
log_history | bool | no | If enabled, write an entry in the history event log for this IO
visible | bool | no | Display the Input/Output on all user interfaces if set. Default to true
step | float | no | Set a step for increment/decrement value. Default is 1.0
enabled | bool | no | Enable the Input/Output. The default value is true. This parameter is added if it's not found in the configuration.
id | string | yes | Unique ID identifying the Input/Output in calaos-server
gui_type | string | no | Internal graphical type for all calaos objects. Set automatically, read-only parameter.
knx_group | string | yes | KNX Group address, Ex: x/y/z
io_type | string | yes | IO type, can be "input", "output", "inout"
logged | bool | no | If enabled, and if influxdb is enabled in local_config send the value to influxdb for this IO
coeff_b | float | no | use in conjunction of coeff_a to apply equation of the form `value_sent = coeff_a * raw_value + coeff_b`. Default value is 0.0
coeff_a | float | no | use in conjunction of coeff_b to apply equation of the form `value_sent = coeff_a * raw_value + coeff_b`. Default value is 1.0.
unit | string | no | Unit which will be displayed on the UI as a suffix.

## Conditions of KNXOutputAnalog
Name | Description
---- | -----------
0 | Event on a specific number value 
 changed | Event on any change of value 
 value | Event on a specific value 
 
## Actions of KNXOutputAnalog
Name | Description
---- | -----------
dec 1 | Decrement value by value 
 inc 1 | Increment value by value 
 dec | Decrement value with configured step 
 inc | Increment value with configured step 
 0 | Set a specific number value 
 
## More Infos
* knxd: https://github.com/knxd/knxd/g


# KNXOutputLight
Light output with KNX and eibnetmux


Basic light. This light have only 2 states, ON or OFF. Can also be used to control simple relays output

## Parameters of KNXOutputLight
Name | Type | Mandatory | Description
---- | ---- | --------- | -----------
host | string | yes | Hostname of knxd, default to localhost
name | string | yes | Name of Input/Output.
io_style | list | yes | GUI style display. This will control the icon displayed on the UI
log_history | bool | no | If enabled, write an entry in the history event log for this IO
visible | bool | no | Display the Input/Output on all user interfaces if set. Default to true
eis | int | no | KNX EIS (Data type)
enabled | bool | no | Enable the Input/Output. The default value is true. This parameter is added if it's not found in the configuration.
id | string | yes | Unique ID identifying the Input/Output in calaos-server
gui_type | string | no | Internal graphical type for all calaos objects. Set automatically, read-only parameter.
logged | bool | no | If enabled, and if influxdb is enabled in local_config send the value to influxdb for this IO
io_type | string | yes | IO type, can be "input", "output", "inout"
knx_group | string | yes | KNX Group address, Ex: x/y/z
listen_knx_group | string | no | KNX Group address for listening status, Ex: x/y/z
read_at_start | bool | yes | Send a read request at start to get the current value. Default is false

## Conditions of KNXOutputLight
Name | Description
---- | -----------
false | Event when light is off 
 true | Event when light is on 
 changed | Event on any change of value 
 
## Actions of KNXOutputLight
Name | Description
---- | -----------
impulse 200 | Do an impulse on light state. Set to true for X ms then reset to false 
 set_state true | Update internal light state without starting real action. This is useful when having updating the light state from an external source. 
 impulse 500 200 500 200 | Do an impulse on light state with a pattern.<br>Ex: 500 200 500 200 means: TRUE for 500ms, FALSE for 200ms, TRUE for 500ms, FALSE for 200ms<br>Ex: 500 loop 200 300 means: TRUE for 500ms, then loop the next steps for infinite, FALSE for 200ms, TRUE for 300ms<br>Ex: 100 100 200 old means: blinks and then set to the old start state (before impulse starts) 
 toggle | Invert light state 
 set_state false | Update internal light state without starting real action. This is useful when having updating the light state from an external source. 
 false | Switch the light off 
 true | Switch the light on 
 
## More Infos
* knxd: https://github.com/knxd/knxd/g


# KNXOutputLightDimmer
Light dimmer with KNX and eibnetmux


Light with dimming control. Light intensity can be changed for this light.

## Parameters of KNXOutputLightDimmer
Name | Type | Mandatory | Description
---- | ---- | --------- | -----------
host | string | yes | Hostname of knxd, default to localhost
read_at_start | bool | yes | Send a read request at start to get the current value. Default is false
listen_knx_group | string | no | KNX Group address for listening status, Ex: x/y/z
knx_group | string | yes | KNX Group address, Ex: x/y/z
logged | bool | no | If enabled, and if influxdb is enabled in local_config send the value to influxdb for this IO
io_type | string | yes | IO type, can be "input", "output", "inout"
gui_type | string | no | Internal graphical type for all calaos objects. Set automatically, read-only parameter.
enabled | bool | no | Enable the Input/Output. The default value is true. This parameter is added if it's not found in the configuration.
eis | int | no | KNX EIS (Data type)
visible | bool | no | Display the Input/Output on all user interfaces if set. Default to true
log_history | bool | no | If enabled, write an entry in the history event log for this IO
name | string | yes | Name of Input/Output.
id | string | yes | Unique ID identifying the Input/Output in calaos-server

## Conditions of KNXOutputLightDimmer
Name | Description
---- | -----------
value | Event when light is at this value 
 changed | Event on any change of value 
 
## Actions of KNXOutputLightDimmer
Name | Description
---- | -----------
hold stop | Dynamically change light intensity when holding a switch (stop action) 
 true | Switch the light on 
 false | Switch the light off 
 set off 50 | Set light value without switching on. This will be the light intensity for the next ON 
 set_state false | Update internal light state without starting real action. This is useful when having updating the light state from an external source. 
 toggle | Invert the light state 
 down 5 | Decrease intensity by X percent 
 impulse 500 200 500 200 | Do an impulse on light state with a pattern.<br>Ex: 500 200 500 200 means: TRUE for 500ms, FALSE for 200ms, TRUE for 500ms, FALSE for 200ms<br>Ex: 500 loop 200 300 means: TRUE for 500ms, then loop the next steps for infinite, FALSE for 200ms, TRUE for 300ms<br>Ex: 100 100 200 old means: blinks and then set to the old start state (before impulse starts) 
 impulse 200 | Do an impulse on light state. Set to true for X ms then reset to false 
 set_state true | Update internal light state without starting real action. This is useful when having updating the light state from an external source. 
 hold press | Dynamically change light intensity when holding a switch (press action) 
 set_state 50 | Update internal light state without starting real action. This is useful when having updating the light state from an external source. 
 set 50 | Set light intensity and swith on if light is off 
 up 5 | Increase intensity by X percent 
 
## More Infos
* knxd: https://github.com/knxd/knxd/g


# KNXOutputLightRGB
Light RGB with KNX and eibnetmux


RGB light. Choose a color to be set for this light.

## Parameters of KNXOutputLightRGB
Name | Type | Mandatory | Description
---- | ---- | --------- | -----------
host | string | yes | Hostname of knxd, default to localhost
read_at_start | bool | yes | Send a read request at start to get the current value. Default is false
eis | int | no | KNX EIS (Data type)
listen_knx_group_blue | string | no | Blue Group address for listening status, Ex: x/y/z
name | string | yes | Name of Input/Output.
log_history | bool | no | If enabled, write an entry in the history event log for this IO
enabled | bool | no | Enable the Input/Output. The default value is true. This parameter is added if it's not found in the configuration.
id | string | yes | Unique ID identifying the Input/Output in calaos-server
gui_type | string | no | Internal graphical type for all calaos objects. Set automatically, read-only parameter.
io_type | string | yes | IO type, can be "input", "output", "inout"
visible | bool | no | Display the Input/Output on all user interfaces if set. Default to true
listen_knx_group_green | string | no | Green Group address for listening status, Ex: x/y/z
knx_group_red | string | yes | Red channel KNX Group address, Ex: x/y/z
logged | bool | no | If enabled, and if influxdb is enabled in local_config send the value to influxdb for this IO
knx_group_green | string | yes | Green channel KNX Group address, Ex: x/y/z
listen_knx_group_red | string | no | Red Group address for listening status, Ex: x/y/z
knx_group_blue | string | yes | Blue channel KNX Group address, Ex: x/y/z

## Conditions of KNXOutputLightRGB
Name | Description
---- | -----------
value | Event when light is at this value 
 changed | Event on any change of value 
 
## Actions of KNXOutputLightRGB
Name | Description
---- | -----------
down_red 5 | Decrease intensity by X percent of red channel 
 true | Switch the light on 
 false | Switch the light off 
 set_state false | Update internal light state without starting real action. This is useful when having updating the light state from an external source. 
 down_green 5 | Decrease intensity by X percent of green channel 
 toggle | Invert the light state (ON/OFF) 
 set_state true | Update internal light state without starting real action. This is useful when having updating the light state from an external source. 
 down_blue 5 | Decrease intensity by X percent of blue channel 
 set_state #AA1294 | Update internal light state without starting real action. This is useful when having updating the light state from an external source. 
 up_red 5 | Increase intensity by X percent of red channel 
 set_red 50 | Set red channel to X percent 
 set_blue 50 | Set blue channel to X percent 
 up_green 5 | Increase intensity by X percent of green channel 
 set_green 50 | Set green channel to X percent 
 set #AA1294 | Set color. Color can be represented by using HTML notation: #AABBCC, rgb(50, 10, 30), hsl(11, 22, 33) 
 up_blue 5 | Increase intensity by X percent of blue channel 
 
## More Infos
* knxd: https://github.com/knxd/knxd/g


# KNXOutputShutter
Shutter with with KNX and eibnetmux


Simple shutter. This shutter supports open/close states, as well as impulse shutters.

## Parameters of KNXOutputShutter
Name | Type | Mandatory | Description
---- | ---- | --------- | -----------
host | string | yes | Hostname of knxd, default to localhost
eis | int | no | KNX EIS (Data type)
name | string | yes | Name of Input/Output.
log_history | bool | no | If enabled, write an entry in the history event log for this IO
visible | bool | no | Display the Input/Output on all user interfaces if set. Default to true
enabled | bool | no | Enable the Input/Output. The default value is true. This parameter is added if it's not found in the configuration.
id | string | yes | Unique ID identifying the Input/Output in calaos-server
gui_type | string | no | Internal graphical type for all calaos objects. Set automatically, read-only parameter.
io_type | string | yes | IO type, can be "input", "output", "inout"
logged | bool | no | If enabled, and if influxdb is enabled in local_config send the value to influxdb for this IO
stop_both | bool | no | If in impulse mode, some shutters needs to activate both up dans down relays when stopping the shutter
read_at_start | bool | yes | Send a read request at start to get the current value. Default is false
time | int | yes | Time in sec for shutter to open or close
knx_group_down | string | yes | Down KNX Group address, Ex: x/y/z
impulse_time | int | no | Impulse time for shutter that needs impulse instead of holding up/down relays. If set to 0 impulse shutter is disabled. Time is in ms. Default to 0
knx_group_up | string | yes | Up KNX Group address, Ex: x/y/z

## Conditions of KNXOutputShutter
Name | Description
---- | -----------
false | Event when shutter is closed 
 true | Event when shutter is open 
 changed | Event on any change of shutter state 
 
## Actions of KNXOutputShutter
Name | Description
---- | -----------
impulse up 200 | Open shutter for X ms 
 set_state true | Update internal shutter state without starting real action. This is useful when having updating the shutter state from an external source. 
 impulse down 200 | Close shutter for X ms 
 toggle | Invert shutter state 
 set_state false | Update internal shutter state without starting real action. This is useful when having updating the shutter state from an external source. 
 stop | Stop the shutter 
 down | Close the shutter 
 up | Open the shutter 
 
## More Infos
* knxd: https://github.com/knxd/knxd/g


# KNXOutputShutterSmart
Shutter with with KNX and eibnetmux


Smart shutter. This shutter calculates the position of the shutter based on the time it takes to open and close. It then allows to set directly the shutter at a specified position.

## Parameters of KNXOutputShutterSmart
Name | Type | Mandatory | Description
---- | ---- | --------- | -----------
host | string | yes | Hostname of knxd, default to localhost
read_at_start | bool | yes | Send a read request at start to get the current value. Default is false
eis | int | no | KNX EIS (Data type)
listen_knx_group | string | no | KNX Group address for listening status, Ex: x/y/z
knx_group_down | string | yes | Down KNX Group address, Ex: x/y/z
name | string | yes | Name of Input/Output.
log_history | bool | no | If enabled, write an entry in the history event log for this IO
visible | bool | no | Display the Input/Output on all user interfaces if set. Default to true
enabled | bool | no | Enable the Input/Output. The default value is true. This parameter is added if it's not found in the configuration.
id | string | yes | Unique ID identifying the Input/Output in calaos-server
gui_type | string | no | Internal graphical type for all calaos objects. Set automatically, read-only parameter.
knx_group | string | yes | KNX Group address, Ex: x/y/z
time_up | int | yes | Time in sec for shutter to be fully open. The more accurate, the better it will work
io_type | string | yes | IO type, can be "input", "output", "inout"
logged | bool | no | If enabled, and if influxdb is enabled in local_config send the value to influxdb for this IO
time_down | int | yes | Time in sec for shutter to fully closed. The more accurate, the better it will work
stop_both | bool | no | If in impulse mode, some shutters needs to activate both up dans down relays when stopping the shutter
impulse_time | int | no | Impulse time for shutter that needs impulse instead of holding up/down relays. If set to 0 impulse shutter is disabled. Time is in ms. Default to 0
knx_group_up | string | yes | Up KNX Group address, Ex: x/y/z

## Conditions of KNXOutputShutterSmart
Name | Description
---- | -----------
false | Event when shutter is closed 
 true | Event when shutter is open 
 changed | Event on any change of shutter state 
 
## Actions of KNXOutputShutterSmart
Name | Description
---- | -----------
up 5 | Open the shutter by X percent 
 set 50 | Set shutter at position X in percent 
 impulse up 200 | Open shutter for X ms 
 set_state true | Update internal shutter state without starting real action. This is useful when having updating the shutter state from an external source. 
 down 5 | Close the shutter by X percent 
 impulse down 200 | Close shutter for X ms 
 toggle | Invert shutter state 
 set_state false | Update internal shutter state without starting real action. This is useful when having updating the shutter state from an external source. 
 calibrate | Start calibration on shutter. This opens fully the shutter and resets all internal position values. Use this if shutter sync is lost. 
 stop | Stop the shutter 
 down | Close the shutter 
 up | Open the shutter 
 
## More Infos
* knxd: https://github.com/knxd/knxd/g


# MilightOutputLightRGB
RGB light support for Limitless/Milight RGB bulbs.


RGB light. Choose a color to be set for this light.

## Parameters of MilightOutputLightRGB
Name | Type | Mandatory | Description
---- | ---- | --------- | -----------
zone | int | yes | Zone to control. Each gateway supports 4 zones.
host | string | yes | Milight wifi gateway IP address
logged | bool | no | If enabled, and if influxdb is enabled in local_config send the value to influxdb for this IO
io_type | string | yes | IO type, can be "input", "output", "inout"
gui_type | string | no | Internal graphical type for all calaos objects. Set automatically, read-only parameter.
enabled | bool | no | Enable the Input/Output. The default value is true. This parameter is added if it's not found in the configuration.
visible | bool | no | Display the Input/Output on all user interfaces if set. Default to true
port | int | no | Gateway port, default to 8899
log_history | bool | no | If enabled, write an entry in the history event log for this IO
name | string | yes | Name of Input/Output.
id | string | yes | Unique ID identifying the Input/Output in calaos-server

## Conditions of MilightOutputLightRGB
Name | Description
---- | -----------
value | Event when light is at this value 
 changed | Event on any change of value 
 
## Actions of MilightOutputLightRGB
Name | Description
---- | -----------
down_red 5 | Decrease intensity by X percent of red channel 
 true | Switch the light on 
 false | Switch the light off 
 set_state false | Update internal light state without starting real action. This is useful when having updating the light state from an external source. 
 down_green 5 | Decrease intensity by X percent of green channel 
 toggle | Invert the light state (ON/OFF) 
 set_state true | Update internal light state without starting real action. This is useful when having updating the light state from an external source. 
 down_blue 5 | Decrease intensity by X percent of blue channel 
 set_state #AA1294 | Update internal light state without starting real action. This is useful when having updating the light state from an external source. 
 up_red 5 | Increase intensity by X percent of red channel 
 set_red 50 | Set red channel to X percent 
 set_blue 50 | Set blue channel to X percent 
 up_green 5 | Increase intensity by X percent of green channel 
 set_green 50 | Set green channel to X percent 
 set #AA1294 | Set color. Color can be represented by using HTML notation: #AABBCC, rgb(50, 10, 30), hsl(11, 22, 33) 
 up_blue 5 | Increase intensity by X percent of blue channel 
 
## More Infos
* LimitlessLED: http://www.limitlessled.com


# MqttInputAnalog
Analog value read from a mqtt broker


An analog input can be used to read analog values to display them and use them in rules.

## Parameters of MqttInputAnalog
Name | Type | Mandatory | Description
---- | ---- | --------- | -----------
topic_sub | string | yes | Topic on witch to subscribe.
topic_pub | string | yes | Topic on witch to publish.
user | string | no | User to use for authentication with mqtt broker. Password must be defined in that case.
keepalive | int | no | keepalive timeout in seconds. Time between two mqtt PING.
port | int | no | TCP port of the mqtt broker. Default value is 1883
precision | int | no | Precision of the returned value. The value represents the number of decimal after the dot. The value is rounded like this : value = 19.275 => returned value 19.28 when preicision = 2, 19.3 when precision = 1, 19 when precision = 0
name | string | yes | Name of Input/Output.
path | string | yes | The path where to found the value in the mqtt payload. If payload if JSON, informations will be extracted depending on the path. for example weather[0]/description, try to read the description value of the 1 element of the array of the weather object. if payload is somple json, just try to use the key of the value you want to read, for example : {"temperature":14.23} use "temperature" as path

io_style | list | yes | GUI style display. This will control the icon displayed on the UI
log_history | bool | no | If enabled, write an entry in the history event log for this IO
password | string | no | Password to use for authentication with mqtt broker. User must be defined in that case.
visible | bool | no | Display the Input/Output on all user interfaces if set. Default to true
display_warning | bool | no | Display a warning if value has not been updated for a long time. Default to true
enabled | bool | no | Enable the Input/Output. The default value is true. This parameter is added if it's not found in the configuration.
unit | string | no | Unit which will be displayed on the UI as a suffix.
coeff_a | float | no | use in conjunction of coeff_b to apply equation of the form `value_displayed = coeff_a * raw_value + coeff_b`. Default value is 1.0.
io_type | string | yes | IO type, can be "input", "output", "inout"
logged | bool | no | If enabled, and if influxdb is enabled in local_config send the value to influxdb for this IO
coeff_b | float | no | use in conjunction of coeff_a to apply equation of the form `value_displayed = coeff_a * raw_value + coeff_b`. Default value is 0.0
host | string | no | IP address of the mqtt broker to connect to. Default value is 127.0.0.1.
offset | float | no | same as coeff_b, can be used alone. Default value is 0.0
id | string | yes | Unique ID identifying the Input/Output in calaos-server
gui_type | string | no | Internal graphical type for all calaos objects. Set automatically, read-only parameter.
period | float | no | Sampling time in microsecond. The value is read at this frequency. If this value is not set, calaos tries to read the interval parameter
interval | float | no | Sampling time in seconds. The value is read at this frequency. If this value is not set, the default value is 15s

## Conditions of MqttInputAnalog
Name | Description
---- | -----------
changed | Event on any change of value 
 value | Event on a specific value 
 

# MqttInputString
String value read from a mqtt broker

## Parameters of MqttInputString
Name | Type | Mandatory | Description
---- | ---- | --------- | -----------
topic_sub | string | yes | Topic on witch to subscribe.
topic_pub | string | yes | Topic on witch to publish.
keepalive | int | no | keepalive timeout in seconds. Time between two mqtt PING.
name | string | yes | Name of Input/Output.
path | string | yes | The path where to found the value in the mqtt payload. If payload if JSON, informations will be extracted depending on the path. for example weather[0]/description, try to read the description value of the 1 element of the array of the weather object. if payload is somple json, just try to use the key of the value you want to read, for example : {"temperature":14.23} use "temperature" as path

log_history | bool | no | If enabled, write an entry in the history event log for this IO
port | int | no | TCP port of the mqtt broker. Default value is 1883
enabled | bool | no | Enable the Input/Output. The default value is true. This parameter is added if it's not found in the configuration.
id | string | yes | Unique ID identifying the Input/Output in calaos-server
gui_type | string | no | Internal graphical type for all calaos objects. Set automatically, read-only parameter.
io_type | string | yes | IO type, can be "input", "output", "inout"
user | string | no | User to use for authentication with mqtt broker. Password must be defined in that case.
logged | bool | no | If enabled, and if influxdb is enabled in local_config send the value to influxdb for this IO
host | string | no | IP address of the mqtt broker to connect to. Default value is 127.0.0.1.
visible | bool | no | Display the Input/Output on all user interfaces if set. Default to true
password | string | no | Password to use for authentication with mqtt broker. User must be defined in that case.


# MqttInputSwitch
Switch value read from a mqtt broker


Basic switch with press/release states.

## Parameters of MqttInputSwitch
Name | Type | Mandatory | Description
---- | ---- | --------- | -----------
on_value | string | yes | Value to interpret as ON value
topic_sub | string | yes | Topic on witch to subscribe.
topic_pub | string | yes | Topic on witch to publish.
keepalive | int | no | keepalive timeout in seconds. Time between two mqtt PING.
name | string | yes | Name of Input/Output.
path | string | yes | The path where to found the value in the mqtt payload. If payload if JSON, informations will be extracted depending on the path. for example weather[0]/description, try to read the description value of the 1 element of the array of the weather object. if payload is somple json, just try to use the key of the value you want to read, for example : {"temperature":14.23} use "temperature" as path

log_history | bool | no | If enabled, write an entry in the history event log for this IO
port | int | no | TCP port of the mqtt broker. Default value is 1883
enabled | bool | no | Enable the Input/Output. The default value is true. This parameter is added if it's not found in the configuration.
id | string | yes | Unique ID identifying the Input/Output in calaos-server
gui_type | string | no | Internal graphical type for all calaos objects. Set automatically, read-only parameter.
io_type | string | yes | IO type, can be "input", "output", "inout"
off_value | string | yes | Value to interpret as OFF value
user | string | no | User to use for authentication with mqtt broker. Password must be defined in that case.
logged | bool | no | If enabled, and if influxdb is enabled in local_config send the value to influxdb for this IO
host | string | no | IP address of the mqtt broker to connect to. Default value is 127.0.0.1.
visible | bool | no | A switch can't be visible. Always false.
password | string | no | Password to use for authentication with mqtt broker. User must be defined in that case.

## Conditions of MqttInputSwitch
Name | Description
---- | -----------
changed | Event on any change of state 
 false | Event triggered when switch is released 
 true | Event triggered when switch is pressed 
 

# MqttInputTemp
Temperature read from a mqtt broker


Temperature sensor input. Use for displaying temperature and to control heating devices with rules based on temperature value

## Parameters of MqttInputTemp
Name | Type | Mandatory | Description
---- | ---- | --------- | -----------
topic_sub | string | yes | Topic on witch to subscribe.
topic_pub | string | yes | Topic on witch to publish.
user | string | no | User to use for authentication with mqtt broker. Password must be defined in that case.
keepalive | int | no | keepalive timeout in seconds. Time between two mqtt PING.
port | int | no | TCP port of the mqtt broker. Default value is 1883
precision | int | no | Precision of the returned value. The value represents the number of decimal after the dot. The value is rounded like this : value = 19.275 => returned value 19.28 when preicision = 2, 19.3 when precision = 1, 19 when precision = 0
name | string | yes | Name of Input/Output.
path | string | yes | The path where to found the value in the mqtt payload. If payload if JSON, informations will be extracted depending on the path. for example weather[0]/description, try to read the description value of the 1 element of the array of the weather object. if payload is somple json, just try to use the key of the value you want to read, for example : {"temperature":14.23} use "temperature" as path

log_history | bool | no | If enabled, write an entry in the history event log for this IO
password | string | no | Password to use for authentication with mqtt broker. User must be defined in that case.
visible | bool | no | Display the Input/Output on all user interfaces if set. Default to true
display_warning | bool | no | Display a warning if value has not been updated for a long time. Default to true
enabled | bool | no | Enable the Input/Output. The default value is true. This parameter is added if it's not found in the configuration.
coeff_a | float | no | use in conjunction of coeff_b to apply equation of the form `value_displayed = coeff_a * raw_value + coeff_b`. Default value is 1.0.
io_type | string | yes | IO type, can be "input", "output", "inout"
logged | bool | no | If enabled, and if influxdb is enabled in local_config send the value to influxdb for this IO
coeff_b | float | no | use in conjunction of coeff_a to apply equation of the form `value_displayed = coeff_a * raw_value + coeff_b`. Default value is 0.0
host | string | no | IP address of the mqtt broker to connect to. Default value is 127.0.0.1.
offset | float | no | same as coeff_b, can be used alone. Default value is 0.0
id | string | yes | Unique ID identifying the Input/Output in calaos-server
gui_type | string | no | Internal graphical type for all calaos objects. Set automatically, read-only parameter.
period | float | no | Sampling time in microsecond. The value is read at this frequency. If this value is not set, calaos tries to read the interval parameter
interval | float | no | Sampling time in seconds. The value is read at this frequency. If this value is not set, the default value is 15s

## Conditions of MqttInputTemp
Name | Description
---- | -----------
changed | Event on any change of temperature value 
 value | Event on a temperature value in degree Celsius 
 

# MqttOutputAnalog
Control analog output through mqtt broker


Analog output. Useful to control analog output devices connected to calaos.

## Parameters of MqttOutputAnalog
Name | Type | Mandatory | Description
---- | ---- | --------- | -----------
data | string | yes | The data sent when publishing to topic. The __##VALUE##__ contained in data is substituted with the state (float value) to be sent.
topic_sub | string | yes | Topic on witch to subscribe.
topic_pub | string | yes | Topic on witch to publish.
user | string | no | User to use for authentication with mqtt broker. Password must be defined in that case.
keepalive | int | no | keepalive timeout in seconds. Time between two mqtt PING.
port | int | no | TCP port of the mqtt broker. Default value is 1883
host | string | no | IP address of the mqtt broker to connect to. Default value is 127.0.0.1.
name | string | yes | Name of Input/Output.
path | string | yes | The path where to found the value in the mqtt payload. If payload if JSON, informations will be extracted depending on the path. for example weather[0]/description, try to read the description value of the 1 element of the array of the weather object. if payload is somple json, just try to use the key of the value you want to read, for example : {"temperature":14.23} use "temperature" as path

io_style | list | yes | GUI style display. This will control the icon displayed on the UI
log_history | bool | no | If enabled, write an entry in the history event log for this IO
password | string | no | Password to use for authentication with mqtt broker. User must be defined in that case.
visible | bool | no | Display the Input/Output on all user interfaces if set. Default to true
step | float | no | Set a step for increment/decrement value. Default is 1.0
enabled | bool | no | Enable the Input/Output. The default value is true. This parameter is added if it's not found in the configuration.
id | string | yes | Unique ID identifying the Input/Output in calaos-server
gui_type | string | no | Internal graphical type for all calaos objects. Set automatically, read-only parameter.
io_type | string | yes | IO type, can be "input", "output", "inout"
logged | bool | no | If enabled, and if influxdb is enabled in local_config send the value to influxdb for this IO
coeff_b | float | no | use in conjunction of coeff_a to apply equation of the form `value_sent = coeff_a * raw_value + coeff_b`. Default value is 0.0
coeff_a | float | no | use in conjunction of coeff_b to apply equation of the form `value_sent = coeff_a * raw_value + coeff_b`. Default value is 1.0.
unit | string | no | Unit which will be displayed on the UI as a suffix.

## Conditions of MqttOutputAnalog
Name | Description
---- | -----------
0 | Event on a specific number value 
 changed | Event on any change of value 
 value | Event on a specific value 
 
## Actions of MqttOutputAnalog
Name | Description
---- | -----------
dec 1 | Decrement value by value 
 inc 1 | Increment value by value 
 dec | Decrement value with configured step 
 inc | Increment value with configured step 
 0 | Set a specific number value 
 

# MqttOutputLight
Control lights through mqtt broker


Basic light. This light have only 2 states, ON or OFF. Can also be used to control simple relays output

## Parameters of MqttOutputLight
Name | Type | Mandatory | Description
---- | ---- | --------- | -----------
data | string | yes | The data sent when publishing to topic. The __##VALUE##__ contained in data is substituted with with the state (on_value, off_value) to be sent.
on_value | string | yes | Value to interpret as ON value
topic_sub | string | yes | Topic on witch to subscribe.
topic_pub | string | yes | Topic on witch to publish.
off_value | string | yes | Value to interpret as OFF value
user | string | no | User to use for authentication with mqtt broker. Password must be defined in that case.
keepalive | int | no | keepalive timeout in seconds. Time between two mqtt PING.
name | string | yes | Name of Input/Output.
path | string | yes | The path where to found the value in the mqtt payload. If payload if JSON, informations will be extracted depending on the path. for example weather[0]/description, try to read the description value of the 1 element of the array of the weather object. if payload is somple json, just try to use the key of the value you want to read, for example : {"temperature":14.23} use "temperature" as path

io_style | list | yes | GUI style display. This will control the icon displayed on the UI
log_history | bool | no | If enabled, write an entry in the history event log for this IO
port | int | no | TCP port of the mqtt broker. Default value is 1883
enabled | bool | no | Enable the Input/Output. The default value is true. This parameter is added if it's not found in the configuration.
id | string | yes | Unique ID identifying the Input/Output in calaos-server
gui_type | string | no | Internal graphical type for all calaos objects. Set automatically, read-only parameter.
io_type | string | yes | IO type, can be "input", "output", "inout"
logged | bool | no | If enabled, and if influxdb is enabled in local_config send the value to influxdb for this IO
host | string | no | IP address of the mqtt broker to connect to. Default value is 127.0.0.1.
visible | bool | no | Display the Input/Output on all user interfaces if set. Default to true
password | string | no | Password to use for authentication with mqtt broker. User must be defined in that case.

## Conditions of MqttOutputLight
Name | Description
---- | -----------
false | Event when light is off 
 true | Event when light is on 
 changed | Event on any change of value 
 
## Actions of MqttOutputLight
Name | Description
---- | -----------
impulse 200 | Do an impulse on light state. Set to true for X ms then reset to false 
 set_state true | Update internal light state without starting real action. This is useful when having updating the light state from an external source. 
 impulse 500 200 500 200 | Do an impulse on light state with a pattern.<br>Ex: 500 200 500 200 means: TRUE for 500ms, FALSE for 200ms, TRUE for 500ms, FALSE for 200ms<br>Ex: 500 loop 200 300 means: TRUE for 500ms, then loop the next steps for infinite, FALSE for 200ms, TRUE for 300ms<br>Ex: 100 100 200 old means: blinks and then set to the old start state (before impulse starts) 
 toggle | Invert light state 
 set_state false | Update internal light state without starting real action. This is useful when having updating the light state from an external source. 
 false | Switch the light off 
 true | Switch the light on 
 

# MqttOutputLightDimmer
Control lights through mqtt broker


Light with dimming control. Light intensity can be changed for this light.

## Parameters of MqttOutputLightDimmer
Name | Type | Mandatory | Description
---- | ---- | --------- | -----------
data | string | yes | The data sent when publishing to topic. The __##VALUE##__ contained in data is substituted with the state (integer value) to be sent.
topic_sub | string | yes | Topic on witch to subscribe.
topic_pub | string | yes | Topic on witch to publish.
keepalive | int | no | keepalive timeout in seconds. Time between two mqtt PING.
name | string | yes | Name of Input/Output.
path | string | yes | The path where to found the value in the mqtt payload. If payload if JSON, informations will be extracted depending on the path. for example weather[0]/description, try to read the description value of the 1 element of the array of the weather object. if payload is somple json, just try to use the key of the value you want to read, for example : {"temperature":14.23} use "temperature" as path

log_history | bool | no | If enabled, write an entry in the history event log for this IO
port | int | no | TCP port of the mqtt broker. Default value is 1883
enabled | bool | no | Enable the Input/Output. The default value is true. This parameter is added if it's not found in the configuration.
id | string | yes | Unique ID identifying the Input/Output in calaos-server
gui_type | string | no | Internal graphical type for all calaos objects. Set automatically, read-only parameter.
io_type | string | yes | IO type, can be "input", "output", "inout"
user | string | no | User to use for authentication with mqtt broker. Password must be defined in that case.
logged | bool | no | If enabled, and if influxdb is enabled in local_config send the value to influxdb for this IO
host | string | no | IP address of the mqtt broker to connect to. Default value is 127.0.0.1.
visible | bool | no | Display the Input/Output on all user interfaces if set. Default to true
password | string | no | Password to use for authentication with mqtt broker. User must be defined in that case.

## Conditions of MqttOutputLightDimmer
Name | Description
---- | -----------
value | Event when light is at this value 
 changed | Event on any change of value 
 
## Actions of MqttOutputLightDimmer
Name | Description
---- | -----------
hold stop | Dynamically change light intensity when holding a switch (stop action) 
 true | Switch the light on 
 false | Switch the light off 
 set off 50 | Set light value without switching on. This will be the light intensity for the next ON 
 set_state false | Update internal light state without starting real action. This is useful when having updating the light state from an external source. 
 toggle | Invert the light state 
 down 5 | Decrease intensity by X percent 
 impulse 500 200 500 200 | Do an impulse on light state with a pattern.<br>Ex: 500 200 500 200 means: TRUE for 500ms, FALSE for 200ms, TRUE for 500ms, FALSE for 200ms<br>Ex: 500 loop 200 300 means: TRUE for 500ms, then loop the next steps for infinite, FALSE for 200ms, TRUE for 300ms<br>Ex: 100 100 200 old means: blinks and then set to the old start state (before impulse starts) 
 impulse 200 | Do an impulse on light state. Set to true for X ms then reset to false 
 set_state true | Update internal light state without starting real action. This is useful when having updating the light state from an external source. 
 hold press | Dynamically change light intensity when holding a switch (press action) 
 set_state 50 | Update internal light state without starting real action. This is useful when having updating the light state from an external source. 
 set 50 | Set light intensity and swith on if light is off 
 up 5 | Increase intensity by X percent 
 

# MqttOutputLightRGB
Control RGB lights through mqtt broker


RGB light. Choose a color to be set for this light.

## Parameters of MqttOutputLightRGB
Name | Type | Mandatory | Description
---- | ---- | --------- | -----------
path_brightness | string | yes | The path where to found the brightness value in the mqtt payload. If payload if JSON, informations will be extracted depending on the path. for example 'brightness'
path_y | string | yes | The path where to found the Y (X/Y Color space) value in the mqtt payload. If payload if JSON, informations will be extracted depending on the path. for example color.y, try to read the x value from the color object.
path_x | string | yes | The path where to found the X (X/Y Color space) value in the mqtt payload. If payload if JSON, informations will be extracted depending on the path. for example color.x, try to read the x value from the color object.
data | string | yes | The data sent when publishing color to topic. The __##VALUE_R##__  __##VALUE_G##__  __##VALUE_B##__ or __##VALUE_HEX##__ or __##VALUE_X##__ __##VALUE_Y##__ __##VALUE_BRIGHTNESS##__ contained in data is substituted with the color (integer value or #RRGGBB string value) to be sent.
topic_sub | string | yes | Topic on witch to subscribe.
topic_pub | string | yes | Topic on witch to publish.
keepalive | int | no | keepalive timeout in seconds. Time between two mqtt PING.
name | string | yes | Name of Input/Output.
path | string | yes | The path where to found the value in the mqtt payload. If payload if JSON, informations will be extracted depending on the path. for example weather[0]/description, try to read the description value of the 1 element of the array of the weather object. if payload is somple json, just try to use the key of the value you want to read, for example : {"temperature":14.23} use "temperature" as path

log_history | bool | no | If enabled, write an entry in the history event log for this IO
port | int | no | TCP port of the mqtt broker. Default value is 1883
enabled | bool | no | Enable the Input/Output. The default value is true. This parameter is added if it's not found in the configuration.
id | string | yes | Unique ID identifying the Input/Output in calaos-server
gui_type | string | no | Internal graphical type for all calaos objects. Set automatically, read-only parameter.
io_type | string | yes | IO type, can be "input", "output", "inout"
user | string | no | User to use for authentication with mqtt broker. Password must be defined in that case.
logged | bool | no | If enabled, and if influxdb is enabled in local_config send the value to influxdb for this IO
host | string | no | IP address of the mqtt broker to connect to. Default value is 127.0.0.1.
visible | bool | no | Display the Input/Output on all user interfaces if set. Default to true
password | string | no | Password to use for authentication with mqtt broker. User must be defined in that case.

## Conditions of MqttOutputLightRGB
Name | Description
---- | -----------
value | Event when light is at this value 
 changed | Event on any change of value 
 
## Actions of MqttOutputLightRGB
Name | Description
---- | -----------
down_red 5 | Decrease intensity by X percent of red channel 
 true | Switch the light on 
 false | Switch the light off 
 set_state false | Update internal light state without starting real action. This is useful when having updating the light state from an external source. 
 down_green 5 | Decrease intensity by X percent of green channel 
 toggle | Invert the light state (ON/OFF) 
 set_state true | Update internal light state without starting real action. This is useful when having updating the light state from an external source. 
 down_blue 5 | Decrease intensity by X percent of blue channel 
 set_state #AA1294 | Update internal light state without starting real action. This is useful when having updating the light state from an external source. 
 up_red 5 | Increase intensity by X percent of red channel 
 set_red 50 | Set red channel to X percent 
 set_blue 50 | Set blue channel to X percent 
 up_green 5 | Increase intensity by X percent of green channel 
 set_green 50 | Set green channel to X percent 
 set #AA1294 | Set color. Color can be represented by using HTML notation: #AABBCC, rgb(50, 10, 30), hsl(11, 22, 33) 
 up_blue 5 | Increase intensity by X percent of blue channel 
 

# MySensorsInputAnalog
Analog measurement with MySensors node


An analog input can be used to read analog values to display them and use them in rules.

## Parameters of MySensorsInputAnalog
Name | Type | Mandatory | Description
---- | ---- | --------- | -----------
sensor_id | string | yes | Sensor ID, as set in your node
node_id | string | yes | Node ID as set in your network
gateway | list | yes | Gateway type used, tcp or serial are supported
port | string | yes | If using serial gateway, port is the serial port (/dev/ttyUSB0 for ex.). If using tcp gateway port is TCP port of the gateway.
precision | int | no | Precision of the returned value. The value represents the number of decimal after the dot. The value is rounded like this : value = 19.275 => returned value 19.28 when preicision = 2, 19.3 when precision = 1, 19 when precision = 0
name | string | yes | Name of Input/Output.
io_style | list | yes | GUI style display. This will control the icon displayed on the UI
log_history | bool | no | If enabled, write an entry in the history event log for this IO
visible | bool | no | Display the Input/Output on all user interfaces if set. Default to true
display_warning | bool | no | Display a warning if value has not been updated for a long time. Default to true
enabled | bool | no | Enable the Input/Output. The default value is true. This parameter is added if it's not found in the configuration.
unit | string | no | Unit which will be displayed on the UI as a suffix.
coeff_a | float | no | use in conjunction of coeff_b to apply equation of the form `value_displayed = coeff_a * raw_value + coeff_b`. Default value is 1.0.
io_type | string | yes | IO type, can be "input", "output", "inout"
logged | bool | no | If enabled, and if influxdb is enabled in local_config send the value to influxdb for this IO
coeff_b | float | no | use in conjunction of coeff_a to apply equation of the form `value_displayed = coeff_a * raw_value + coeff_b`. Default value is 0.0
host | string | yes | IP address of the tcp gateway if relevant
offset | float | no | same as coeff_b, can be used alone. Default value is 0.0
id | string | yes | Unique ID identifying the Input/Output in calaos-server
gui_type | string | no | Internal graphical type for all calaos objects. Set automatically, read-only parameter.
period | float | no | Sampling time in microsecond. The value is read at this frequency. If this value is not set, calaos tries to read the interval parameter
interval | float | no | Sampling time in seconds. The value is read at this frequency. If this value is not set, the default value is 15s

## Conditions of MySensorsInputAnalog
Name | Description
---- | -----------
changed | Event on any change of value 
 value | Event on a specific value 
 
## More Infos
* MySensors: http://mysensors.org


# MySensorsInputString
Display string from MySensors node

## Parameters of MySensorsInputString
Name | Type | Mandatory | Description
---- | ---- | --------- | -----------
sensor_id | string | yes | Sensor ID, as set in your node
host | string | yes | IP address of the tcp gateway if relevant
logged | bool | no | If enabled, and if influxdb is enabled in local_config send the value to influxdb for this IO
io_type | string | yes | IO type, can be "input", "output", "inout"
gui_type | string | no | Internal graphical type for all calaos objects. Set automatically, read-only parameter.
enabled | bool | no | Enable the Input/Output. The default value is true. This parameter is added if it's not found in the configuration.
node_id | string | yes | Node ID as set in your network
gateway | list | yes | Gateway type used, tcp or serial are supported
visible | bool | no | Display the Input/Output on all user interfaces if set. Default to true
port | string | yes | If using serial gateway, port is the serial port (/dev/ttyUSB0 for ex.). If using tcp gateway port is TCP port of the gateway.
log_history | bool | no | If enabled, write an entry in the history event log for this IO
name | string | yes | Name of Input/Output.
id | string | yes | Unique ID identifying the Input/Output in calaos-server

## More Infos
* MySensors: http://mysensors.org


# MySensorsInputSwitch
Input switch with MySensors node


Basic switch with press/release states.

## Parameters of MySensorsInputSwitch
Name | Type | Mandatory | Description
---- | ---- | --------- | -----------
sensor_id | string | yes | Sensor ID, as set in your node
host | string | yes | IP address of the tcp gateway if relevant
logged | bool | no | If enabled, and if influxdb is enabled in local_config send the value to influxdb for this IO
io_type | string | yes | IO type, can be "input", "output", "inout"
gui_type | string | no | Internal graphical type for all calaos objects. Set automatically, read-only parameter.
enabled | bool | no | Enable the Input/Output. The default value is true. This parameter is added if it's not found in the configuration.
node_id | string | yes | Node ID as set in your network
gateway | list | yes | Gateway type used, tcp or serial are supported
visible | bool | no | A switch can't be visible. Always false.
port | string | yes | If using serial gateway, port is the serial port (/dev/ttyUSB0 for ex.). If using tcp gateway port is TCP port of the gateway.
log_history | bool | no | If enabled, write an entry in the history event log for this IO
name | string | yes | Name of Input/Output.
id | string | yes | Unique ID identifying the Input/Output in calaos-server

## Conditions of MySensorsInputSwitch
Name | Description
---- | -----------
changed | Event on any change of state 
 false | Event triggered when switch is released 
 true | Event triggered when switch is pressed 
 
## More Infos
* MySensors: http://mysensors.org


# MySensorsInputSwitchLongPress
Input long press switch with MySensors node


Long press switch. This switch supports single press and long press. User has 500ms to perform the long press.

## Parameters of MySensorsInputSwitchLongPress
Name | Type | Mandatory | Description
---- | ---- | --------- | -----------
sensor_id | string | yes | Sensor ID, as set in your node
host | string | yes | IP address of the tcp gateway if relevant
logged | bool | no | If enabled, and if influxdb is enabled in local_config send the value to influxdb for this IO
io_type | string | yes | IO type, can be "input", "output", "inout"
gui_type | string | no | Internal graphical type for all calaos objects. Set automatically, read-only parameter.
enabled | bool | no | Enable the Input/Output. The default value is true. This parameter is added if it's not found in the configuration.
node_id | string | yes | Node ID as set in your network
gateway | list | yes | Gateway type used, tcp or serial are supported
visible | bool | no | A switch can't be visible. Always false.
port | string | yes | If using serial gateway, port is the serial port (/dev/ttyUSB0 for ex.). If using tcp gateway port is TCP port of the gateway.
log_history | bool | no | If enabled, write an entry in the history event log for this IO
name | string | yes | Name of Input/Output.
id | string | yes | Unique ID identifying the Input/Output in calaos-server

## Conditions of MySensorsInputSwitchLongPress
Name | Description
---- | -----------
changed | Event on any change of state 
 2 | Event triggered when switch is pressed at least for 500ms (long press) 
 1 | Event triggered when switch is pressed quickly 
 
## More Infos
* MySensors: http://mysensors.org


# MySensorsInputSwitchTriple
Input triple click switch with MySensors node


Triple click switch. This switch can start 3 kind of actions. User has 500ms to do a multiple click.

## Parameters of MySensorsInputSwitchTriple
Name | Type | Mandatory | Description
---- | ---- | --------- | -----------
sensor_id | string | yes | Sensor ID, as set in your node
host | string | yes | IP address of the tcp gateway if relevant
logged | bool | no | If enabled, and if influxdb is enabled in local_config send the value to influxdb for this IO
io_type | string | yes | IO type, can be "input", "output", "inout"
gui_type | string | no | Internal graphical type for all calaos objects. Set automatically, read-only parameter.
enabled | bool | no | Enable the Input/Output. The default value is true. This parameter is added if it's not found in the configuration.
node_id | string | yes | Node ID as set in your network
gateway | list | yes | Gateway type used, tcp or serial are supported
visible | bool | no | A switch can't be visible. Always false.
port | string | yes | If using serial gateway, port is the serial port (/dev/ttyUSB0 for ex.). If using tcp gateway port is TCP port of the gateway.
log_history | bool | no | If enabled, write an entry in the history event log for this IO
name | string | yes | Name of Input/Output.
id | string | yes | Unique ID identifying the Input/Output in calaos-server

## Conditions of MySensorsInputSwitchTriple
Name | Description
---- | -----------
changed | Event on any change of state 
 2 | Event triggered when switch is double clicked 
 3 | Event triggered when switch is triple clicked 
 1 | Event triggered when switch is single clicked 
 
## More Infos
* MySensors: http://mysensors.org


# MySensorsInputTemp
Temperature sensor with MySensors node


Temperature sensor input. Use for displaying temperature and to control heating devices with rules based on temperature value

## Parameters of MySensorsInputTemp
Name | Type | Mandatory | Description
---- | ---- | --------- | -----------
sensor_id | string | yes | Sensor ID, as set in your node
node_id | string | yes | Node ID as set in your network
gateway | list | yes | Gateway type used, tcp or serial are supported
port | string | yes | If using serial gateway, port is the serial port (/dev/ttyUSB0 for ex.). If using tcp gateway port is TCP port of the gateway.
precision | int | no | Precision of the returned value. The value represents the number of decimal after the dot. The value is rounded like this : value = 19.275 => returned value 19.28 when preicision = 2, 19.3 when precision = 1, 19 when precision = 0
name | string | yes | Name of Input/Output.
log_history | bool | no | If enabled, write an entry in the history event log for this IO
visible | bool | no | Display the Input/Output on all user interfaces if set. Default to true
display_warning | bool | no | Display a warning if value has not been updated for a long time. Default to true
enabled | bool | no | Enable the Input/Output. The default value is true. This parameter is added if it's not found in the configuration.
coeff_a | float | no | use in conjunction of coeff_b to apply equation of the form `value_displayed = coeff_a * raw_value + coeff_b`. Default value is 1.0.
io_type | string | yes | IO type, can be "input", "output", "inout"
logged | bool | no | If enabled, and if influxdb is enabled in local_config send the value to influxdb for this IO
coeff_b | float | no | use in conjunction of coeff_a to apply equation of the form `value_displayed = coeff_a * raw_value + coeff_b`. Default value is 0.0
host | string | yes | IP address of the tcp gateway if relevant
offset | float | no | same as coeff_b, can be used alone. Default value is 0.0
id | string | yes | Unique ID identifying the Input/Output in calaos-server
gui_type | string | no | Internal graphical type for all calaos objects. Set automatically, read-only parameter.
period | float | no | Sampling time in microsecond. The value is read at this frequency. If this value is not set, calaos tries to read the interval parameter
interval | float | no | Sampling time in seconds. The value is read at this frequency. If this value is not set, the default value is 15s

## Conditions of MySensorsInputTemp
Name | Description
---- | -----------
changed | Event on any change of temperature value 
 value | Event on a temperature value in degree Celsius 
 
## More Infos
* MySensors: http://mysensors.org


# MySensorsOutputAnalog
Analog output with MySensors node


Analog output. Useful to control analog output devices connected to calaos.

## Parameters of MySensorsOutputAnalog
Name | Type | Mandatory | Description
---- | ---- | --------- | -----------
data_type | string | no | Data type sent to the node. Default: V_DIMMER, see MySensors.cpp for more values.
sensor_id | string | yes | Sensor ID, as set in your node
node_id | string | yes | Node ID as set in your network
host | string | yes | IP address of the tcp gateway if relevant
port | string | yes | If using serial gateway, port is the serial port (/dev/ttyUSB0 for ex.). If using tcp gateway port is TCP port of the gateway.
gateway | list | yes | Gateway type used, tcp or serial are supported
name | string | yes | Name of Input/Output.
io_style | list | yes | GUI style display. This will control the icon displayed on the UI
log_history | bool | no | If enabled, write an entry in the history event log for this IO
visible | bool | no | Display the Input/Output on all user interfaces if set. Default to true
step | float | no | Set a step for increment/decrement value. Default is 1.0
enabled | bool | no | Enable the Input/Output. The default value is true. This parameter is added if it's not found in the configuration.
id | string | yes | Unique ID identifying the Input/Output in calaos-server
gui_type | string | no | Internal graphical type for all calaos objects. Set automatically, read-only parameter.
io_type | string | yes | IO type, can be "input", "output", "inout"
logged | bool | no | If enabled, and if influxdb is enabled in local_config send the value to influxdb for this IO
coeff_b | float | no | use in conjunction of coeff_a to apply equation of the form `value_sent = coeff_a * raw_value + coeff_b`. Default value is 0.0
coeff_a | float | no | use in conjunction of coeff_b to apply equation of the form `value_sent = coeff_a * raw_value + coeff_b`. Default value is 1.0.
unit | string | no | Unit which will be displayed on the UI as a suffix.

## Conditions of MySensorsOutputAnalog
Name | Description
---- | -----------
0 | Event on a specific number value 
 changed | Event on any change of value 
 value | Event on a specific value 
 
## Actions of MySensorsOutputAnalog
Name | Description
---- | -----------
dec 1 | Decrement value by value 
 inc 1 | Increment value by value 
 dec | Decrement value with configured step 
 inc | Increment value with configured step 
 0 | Set a specific number value 
 
## More Infos
* MySensors: http://mysensors.org


# MySensorsOutputDimmer
Light dimmer with MySensors node


Light with dimming control. Light intensity can be changed for this light.

## Parameters of MySensorsOutputDimmer
Name | Type | Mandatory | Description
---- | ---- | --------- | -----------
data_type | string | no | Data type sent to the node. Default: V_DIMMER, see MySensors.cpp for more values.
name | string | yes | Name of Input/Output.
log_history | bool | no | If enabled, write an entry in the history event log for this IO
port | string | yes | If using serial gateway, port is the serial port (/dev/ttyUSB0 for ex.). If using tcp gateway port is TCP port of the gateway.
visible | bool | no | Display the Input/Output on all user interfaces if set. Default to true
gateway | list | yes | Gateway type used, tcp or serial are supported
node_id | string | yes | Node ID as set in your network
enabled | bool | no | Enable the Input/Output. The default value is true. This parameter is added if it's not found in the configuration.
id | string | yes | Unique ID identifying the Input/Output in calaos-server
gui_type | string | no | Internal graphical type for all calaos objects. Set automatically, read-only parameter.
io_type | string | yes | IO type, can be "input", "output", "inout"
logged | bool | no | If enabled, and if influxdb is enabled in local_config send the value to influxdb for this IO
host | string | yes | IP address of the tcp gateway if relevant
sensor_id | string | yes | Sensor ID, as set in your node

## Conditions of MySensorsOutputDimmer
Name | Description
---- | -----------
value | Event when light is at this value 
 changed | Event on any change of value 
 
## Actions of MySensorsOutputDimmer
Name | Description
---- | -----------
hold stop | Dynamically change light intensity when holding a switch (stop action) 
 true | Switch the light on 
 false | Switch the light off 
 set off 50 | Set light value without switching on. This will be the light intensity for the next ON 
 set_state false | Update internal light state without starting real action. This is useful when having updating the light state from an external source. 
 toggle | Invert the light state 
 down 5 | Decrease intensity by X percent 
 impulse 500 200 500 200 | Do an impulse on light state with a pattern.<br>Ex: 500 200 500 200 means: TRUE for 500ms, FALSE for 200ms, TRUE for 500ms, FALSE for 200ms<br>Ex: 500 loop 200 300 means: TRUE for 500ms, then loop the next steps for infinite, FALSE for 200ms, TRUE for 300ms<br>Ex: 100 100 200 old means: blinks and then set to the old start state (before impulse starts) 
 impulse 200 | Do an impulse on light state. Set to true for X ms then reset to false 
 set_state true | Update internal light state without starting real action. This is useful when having updating the light state from an external source. 
 hold press | Dynamically change light intensity when holding a switch (press action) 
 set_state 50 | Update internal light state without starting real action. This is useful when having updating the light state from an external source. 
 set 50 | Set light intensity and swith on if light is off 
 up 5 | Increase intensity by X percent 
 
## More Infos
* MySensors: http://mysensors.org


# MySensorsOutputLight
Light/relay with MySensors node


Basic light. This light have only 2 states, ON or OFF. Can also be used to control simple relays output

## Parameters of MySensorsOutputLight
Name | Type | Mandatory | Description
---- | ---- | --------- | -----------
data_type | string | no | Data type sent to the node. Default: V_LIGHT, see MySensors.cpp for more values.
sensor_id | string | yes | Sensor ID, as set in your node
name | string | yes | Name of Input/Output.
io_style | list | yes | GUI style display. This will control the icon displayed on the UI
log_history | bool | no | If enabled, write an entry in the history event log for this IO
port | string | yes | If using serial gateway, port is the serial port (/dev/ttyUSB0 for ex.). If using tcp gateway port is TCP port of the gateway.
visible | bool | no | Display the Input/Output on all user interfaces if set. Default to true
gateway | list | yes | Gateway type used, tcp or serial are supported
node_id | string | yes | Node ID as set in your network
enabled | bool | no | Enable the Input/Output. The default value is true. This parameter is added if it's not found in the configuration.
id | string | yes | Unique ID identifying the Input/Output in calaos-server
gui_type | string | no | Internal graphical type for all calaos objects. Set automatically, read-only parameter.
io_type | string | yes | IO type, can be "input", "output", "inout"
logged | bool | no | If enabled, and if influxdb is enabled in local_config send the value to influxdb for this IO
host | string | yes | IP address of the tcp gateway if relevant

## Conditions of MySensorsOutputLight
Name | Description
---- | -----------
false | Event when light is off 
 true | Event when light is on 
 changed | Event on any change of value 
 
## Actions of MySensorsOutputLight
Name | Description
---- | -----------
impulse 200 | Do an impulse on light state. Set to true for X ms then reset to false 
 set_state true | Update internal light state without starting real action. This is useful when having updating the light state from an external source. 
 impulse 500 200 500 200 | Do an impulse on light state with a pattern.<br>Ex: 500 200 500 200 means: TRUE for 500ms, FALSE for 200ms, TRUE for 500ms, FALSE for 200ms<br>Ex: 500 loop 200 300 means: TRUE for 500ms, then loop the next steps for infinite, FALSE for 200ms, TRUE for 300ms<br>Ex: 100 100 200 old means: blinks and then set to the old start state (before impulse starts) 
 toggle | Invert light state 
 set_state false | Update internal light state without starting real action. This is useful when having updating the light state from an external source. 
 false | Switch the light off 
 true | Switch the light on 
 
## More Infos
* MySensors: http://mysensors.org


# MySensorsOutputLightRGB
RGB Light dimmer with MySensors node


RGB light. Choose a color to be set for this light.

## Parameters of MySensorsOutputLightRGB
Name | Type | Mandatory | Description
---- | ---- | --------- | -----------
data_type | string | no | Data type sent to the node. Default: V_DIMMER, see MySensors.cpp for more values.
node_id_green | string | yes | Node ID for green channel, as set in your network
name | string | yes | Name of Input/Output.
log_history | bool | no | If enabled, write an entry in the history event log for this IO
port | string | yes | If using serial gateway, port is the serial port (/dev/ttyUSB0 for ex.). If using tcp gateway port is TCP port of the gateway.
gateway | list | yes | Gateway type used, tcp or serial are supported
enabled | bool | no | Enable the Input/Output. The default value is true. This parameter is added if it's not found in the configuration.
id | string | yes | Unique ID identifying the Input/Output in calaos-server
gui_type | string | no | Internal graphical type for all calaos objects. Set automatically, read-only parameter.
io_type | string | yes | IO type, can be "input", "output", "inout"
sensor_id_green | string | yes | Sensor ID green red channel, as set in your node
logged | bool | no | If enabled, and if influxdb is enabled in local_config send the value to influxdb for this IO
host | string | yes | IP address of the tcp gateway if relevant
sensor_id_blue | string | yes | Sensor ID blue red channel, as set in your node
node_id_blue | string | yes | Node ID for blue channel, as set in your network
visible | bool | no | Display the Input/Output on all user interfaces if set. Default to true
node_id_red | string | yes | Node ID for red channel, as set in your network
sensor_id_red | string | yes | Sensor ID for red channel, as set in your node

## Conditions of MySensorsOutputLightRGB
Name | Description
---- | -----------
value | Event when light is at this value 
 changed | Event on any change of value 
 
## Actions of MySensorsOutputLightRGB
Name | Description
---- | -----------
down_red 5 | Decrease intensity by X percent of red channel 
 true | Switch the light on 
 false | Switch the light off 
 set_state false | Update internal light state without starting real action. This is useful when having updating the light state from an external source. 
 down_green 5 | Decrease intensity by X percent of green channel 
 toggle | Invert the light state (ON/OFF) 
 set_state true | Update internal light state without starting real action. This is useful when having updating the light state from an external source. 
 down_blue 5 | Decrease intensity by X percent of blue channel 
 set_state #AA1294 | Update internal light state without starting real action. This is useful when having updating the light state from an external source. 
 up_red 5 | Increase intensity by X percent of red channel 
 set_red 50 | Set red channel to X percent 
 set_blue 50 | Set blue channel to X percent 
 up_green 5 | Increase intensity by X percent of green channel 
 set_green 50 | Set green channel to X percent 
 set #AA1294 | Set color. Color can be represented by using HTML notation: #AABBCC, rgb(50, 10, 30), hsl(11, 22, 33) 
 up_blue 5 | Increase intensity by X percent of blue channel 
 
## More Infos
* MySensors: http://mysensors.org


# MySensorsOutputShutter
Shutter with MySensors node


Simple shutter. This shutter supports open/close states, as well as impulse shutters.

## Parameters of MySensorsOutputShutter
Name | Type | Mandatory | Description
---- | ---- | --------- | -----------
data_type | string | no | Data type sent to the node. Default: V_LIGHT, see MySensors.cpp for more values.
sensor_id_down | string | yes | Sensor ID for closing shutter, as set in your node
host | string | yes | IP address of the tcp gateway if relevant
name | string | yes | Name of Input/Output.
log_history | bool | no | If enabled, write an entry in the history event log for this IO
visible | bool | no | Display the Input/Output on all user interfaces if set. Default to true
gateway | list | yes | Gateway type used, tcp or serial are supported
enabled | bool | no | Enable the Input/Output. The default value is true. This parameter is added if it's not found in the configuration.
node_id_up | string | yes | Node ID for opening shutter, as set in your network
id | string | yes | Unique ID identifying the Input/Output in calaos-server
gui_type | string | no | Internal graphical type for all calaos objects. Set automatically, read-only parameter.
node_id_down | string | yes | Node ID for closing shutter, as set in your network
io_type | string | yes | IO type, can be "input", "output", "inout"
sensor_id_up | string | yes | Sensor ID for opening shutter, as set in your node
logged | bool | no | If enabled, and if influxdb is enabled in local_config send the value to influxdb for this IO
port | string | yes | If using serial gateway, port is the serial port (/dev/ttyUSB0 for ex.). If using tcp gateway port is TCP port of the gateway.
stop_both | bool | no | If in impulse mode, some shutters needs to activate both up dans down relays when stopping the shutter
time | int | yes | Time in sec for shutter to open or close
impulse_time | int | no | Impulse time for shutter that needs impulse instead of holding up/down relays. If set to 0 impulse shutter is disabled. Time is in ms. Default to 0

## Conditions of MySensorsOutputShutter
Name | Description
---- | -----------
false | Event when shutter is closed 
 true | Event when shutter is open 
 changed | Event on any change of shutter state 
 
## Actions of MySensorsOutputShutter
Name | Description
---- | -----------
impulse up 200 | Open shutter for X ms 
 set_state true | Update internal shutter state without starting real action. This is useful when having updating the shutter state from an external source. 
 impulse down 200 | Close shutter for X ms 
 toggle | Invert shutter state 
 set_state false | Update internal shutter state without starting real action. This is useful when having updating the shutter state from an external source. 
 stop | Stop the shutter 
 down | Close the shutter 
 up | Open the shutter 
 
## More Infos
* MySensors: http://mysensors.org


# MySensorsOutputShutterSmart
Smart shutter with MySensors node


Smart shutter. This shutter calculates the position of the shutter based on the time it takes to open and close. It then allows to set directly the shutter at a specified position.

## Parameters of MySensorsOutputShutterSmart
Name | Type | Mandatory | Description
---- | ---- | --------- | -----------
data_type | string | no | Data type sent to the node. Default: V_LIGHT, see MySensors.cpp for more values.
sensor_id_down | string | yes | Sensor ID for closing shutter, as set in your node
host | string | yes | IP address of the tcp gateway if relevant
name | string | yes | Name of Input/Output.
log_history | bool | no | If enabled, write an entry in the history event log for this IO
visible | bool | no | Display the Input/Output on all user interfaces if set. Default to true
gateway | list | yes | Gateway type used, tcp or serial are supported
enabled | bool | no | Enable the Input/Output. The default value is true. This parameter is added if it's not found in the configuration.
node_id_up | string | yes | Node ID for opening shutter, as set in your network
id | string | yes | Unique ID identifying the Input/Output in calaos-server
gui_type | string | no | Internal graphical type for all calaos objects. Set automatically, read-only parameter.
node_id_down | string | yes | Node ID for closing shutter, as set in your network
time_up | int | yes | Time in sec for shutter to be fully open. The more accurate, the better it will work
io_type | string | yes | IO type, can be "input", "output", "inout"
sensor_id_up | string | yes | Sensor ID for opening shutter, as set in your node
logged | bool | no | If enabled, and if influxdb is enabled in local_config send the value to influxdb for this IO
time_down | int | yes | Time in sec for shutter to fully closed. The more accurate, the better it will work
port | string | yes | If using serial gateway, port is the serial port (/dev/ttyUSB0 for ex.). If using tcp gateway port is TCP port of the gateway.
stop_both | bool | no | If in impulse mode, some shutters needs to activate both up dans down relays when stopping the shutter
impulse_time | int | no | Impulse time for shutter that needs impulse instead of holding up/down relays. If set to 0 impulse shutter is disabled. Time is in ms. Default to 0

## Conditions of MySensorsOutputShutterSmart
Name | Description
---- | -----------
false | Event when shutter is closed 
 true | Event when shutter is open 
 changed | Event on any change of shutter state 
 
## Actions of MySensorsOutputShutterSmart
Name | Description
---- | -----------
up 5 | Open the shutter by X percent 
 set 50 | Set shutter at position X in percent 
 impulse up 200 | Open shutter for X ms 
 set_state true | Update internal shutter state without starting real action. This is useful when having updating the shutter state from an external source. 
 down 5 | Close the shutter by X percent 
 impulse down 200 | Close shutter for X ms 
 toggle | Invert shutter state 
 set_state false | Update internal shutter state without starting real action. This is useful when having updating the shutter state from an external source. 
 calibrate | Start calibration on shutter. This opens fully the shutter and resets all internal position values. Use this if shutter sync is lost. 
 stop | Stop the shutter 
 down | Close the shutter 
 up | Open the shutter 
 
## More Infos
* MySensors: http://mysensors.org


# MySensorsOutputString
String output with MySensors node

## Parameters of MySensorsOutputString
Name | Type | Mandatory | Description
---- | ---- | --------- | -----------
data_type | string | no | Data type sent to the node. Default: V_VAR1, see MySensors.cpp for more values.
name | string | yes | Name of Input/Output.
log_history | bool | no | If enabled, write an entry in the history event log for this IO
port | string | yes | If using serial gateway, port is the serial port (/dev/ttyUSB0 for ex.). If using tcp gateway port is TCP port of the gateway.
visible | bool | no | Display the Input/Output on all user interfaces if set. Default to true
gateway | list | yes | Gateway type used, tcp or serial are supported
node_id | string | yes | Node ID as set in your network
enabled | bool | no | Enable the Input/Output. The default value is true. This parameter is added if it's not found in the configuration.
id | string | yes | Unique ID identifying the Input/Output in calaos-server
gui_type | string | no | Internal graphical type for all calaos objects. Set automatically, read-only parameter.
io_type | string | yes | IO type, can be "input", "output", "inout"
logged | bool | no | If enabled, and if influxdb is enabled in local_config send the value to influxdb for this IO
host | string | yes | IP address of the tcp gateway if relevant
sensor_id | string | yes | Sensor ID, as set in your node

## More Infos
* MySensors: http://mysensors.org


# OLAOutputLightDimmer
DMX Light dimmer using OLA (Open Lighting Architecture)


Light with dimming control. Light intensity can be changed for this light.

## Parameters of OLAOutputLightDimmer
Name | Type | Mandatory | Description
---- | ---- | --------- | -----------
channel | int | yes | DMX channel to control
logged | bool | no | If enabled, and if influxdb is enabled in local_config send the value to influxdb for this IO
io_type | string | yes | IO type, can be "input", "output", "inout"
gui_type | string | no | Internal graphical type for all calaos objects. Set automatically, read-only parameter.
universe | int | yes | OLA universe to control
enabled | bool | no | Enable the Input/Output. The default value is true. This parameter is added if it's not found in the configuration.
visible | bool | no | Display the Input/Output on all user interfaces if set. Default to true
log_history | bool | no | If enabled, write an entry in the history event log for this IO
name | string | yes | Name of Input/Output.
id | string | yes | Unique ID identifying the Input/Output in calaos-server

## Conditions of OLAOutputLightDimmer
Name | Description
---- | -----------
value | Event when light is at this value 
 changed | Event on any change of value 
 
## Actions of OLAOutputLightDimmer
Name | Description
---- | -----------
hold stop | Dynamically change light intensity when holding a switch (stop action) 
 true | Switch the light on 
 false | Switch the light off 
 set off 50 | Set light value without switching on. This will be the light intensity for the next ON 
 set_state false | Update internal light state without starting real action. This is useful when having updating the light state from an external source. 
 toggle | Invert the light state 
 down 5 | Decrease intensity by X percent 
 impulse 500 200 500 200 | Do an impulse on light state with a pattern.<br>Ex: 500 200 500 200 means: TRUE for 500ms, FALSE for 200ms, TRUE for 500ms, FALSE for 200ms<br>Ex: 500 loop 200 300 means: TRUE for 500ms, then loop the next steps for infinite, FALSE for 200ms, TRUE for 300ms<br>Ex: 100 100 200 old means: blinks and then set to the old start state (before impulse starts) 
 impulse 200 | Do an impulse on light state. Set to true for X ms then reset to false 
 set_state true | Update internal light state without starting real action. This is useful when having updating the light state from an external source. 
 hold press | Dynamically change light intensity when holding a switch (press action) 
 set_state 50 | Update internal light state without starting real action. This is useful when having updating the light state from an external source. 
 set 50 | Set light intensity and swith on if light is off 
 up 5 | Increase intensity by X percent 
 
## More Infos
* OLA: http://www.openlighting.org


# OLAOutputLightRGB
RGB Light dimmer using 3 DMX channels with OLA (Open Lighting Architecture)


RGB light. Choose a color to be set for this light.

## Parameters of OLAOutputLightRGB
Name | Type | Mandatory | Description
---- | ---- | --------- | -----------
channel_blue | int | yes | DMX channel for blue to control
channel_red | int | yes | DMX channel for red to control
logged | bool | no | If enabled, and if influxdb is enabled in local_config send the value to influxdb for this IO
io_type | string | yes | IO type, can be "input", "output", "inout"
gui_type | string | no | Internal graphical type for all calaos objects. Set automatically, read-only parameter.
channel_green | int | yes | DMX channel for green to control
universe | int | yes | OLA universe to control
enabled | bool | no | Enable the Input/Output. The default value is true. This parameter is added if it's not found in the configuration.
visible | bool | no | Display the Input/Output on all user interfaces if set. Default to true
log_history | bool | no | If enabled, write an entry in the history event log for this IO
name | string | yes | Name of Input/Output.
id | string | yes | Unique ID identifying the Input/Output in calaos-server

## Conditions of OLAOutputLightRGB
Name | Description
---- | -----------
value | Event when light is at this value 
 changed | Event on any change of value 
 
## Actions of OLAOutputLightRGB
Name | Description
---- | -----------
down_red 5 | Decrease intensity by X percent of red channel 
 true | Switch the light on 
 false | Switch the light off 
 set_state false | Update internal light state without starting real action. This is useful when having updating the light state from an external source. 
 down_green 5 | Decrease intensity by X percent of green channel 
 toggle | Invert the light state (ON/OFF) 
 set_state true | Update internal light state without starting real action. This is useful when having updating the light state from an external source. 
 down_blue 5 | Decrease intensity by X percent of blue channel 
 set_state #AA1294 | Update internal light state without starting real action. This is useful when having updating the light state from an external source. 
 up_red 5 | Increase intensity by X percent of red channel 
 set_red 50 | Set red channel to X percent 
 set_blue 50 | Set blue channel to X percent 
 up_green 5 | Increase intensity by X percent of green channel 
 set_green 50 | Set green channel to X percent 
 set #AA1294 | Set color. Color can be represented by using HTML notation: #AABBCC, rgb(50, 10, 30), hsl(11, 22, 33) 
 up_blue 5 | Increase intensity by X percent of blue channel 
 
## More Infos
* OLA: http://www.openlighting.org


# OutputFake
Fake test output. Do nothing. Do not use.

## Parameters of OutputFake
Name | Type | Mandatory | Description
---- | ---- | --------- | -----------
logged | bool | no | If enabled, and if influxdb is enabled in local_config send the value to influxdb for this IO
io_type | string | yes | IO type, can be "input", "output", "inout"
gui_type | string | no | Internal graphical type for all calaos objects. Set automatically, read-only parameter.
enabled | bool | no | Enable the Input/Output. The default value is true. This parameter is added if it's not found in the configuration.
visible | bool | no | Display the Input/Output on all user interfaces if set. Default to true
log_history | bool | no | If enabled, write an entry in the history event log for this IO
name | string | yes | Name of Input/Output.
id | string | yes | Unique ID identifying the Input/Output in calaos-server


# OWTemp
Temperature measurement with DS18B20 Onewire Sensor


Temperature sensor input. Use for displaying temperature and to control heating devices with rules based on temperature value

## Parameters of OWTemp
Name | Type | Mandatory | Description
---- | ---- | --------- | -----------
use_w1 | bool | no | Force the use of w1 kernel driver instead of OneWire driver
precision | int | no | Precision of the returned value. The value represents the number of decimal after the dot. The value is rounded like this : value = 19.275 => returned value 19.28 when preicision = 2, 19.3 when precision = 1, 19 when precision = 0
ow_id | string | yes | Unique ID of sensor on OneWire bus.
name | string | yes | Name of Input/Output.
log_history | bool | no | If enabled, write an entry in the history event log for this IO
ow_args | string | yes | Additional parameter used for owfs initialization.For example you can use -u to use the USB owfs drivers
visible | bool | no | Display the Input/Output on all user interfaces if set. Default to true
display_warning | bool | no | Display a warning if value has not been updated for a long time. Default to true
enabled | bool | no | Enable the Input/Output. The default value is true. This parameter is added if it's not found in the configuration.
coeff_a | float | no | use in conjunction of coeff_b to apply equation of the form `value_displayed = coeff_a * raw_value + coeff_b`. Default value is 1.0.
io_type | string | yes | IO type, can be "input", "output", "inout"
logged | bool | no | If enabled, and if influxdb is enabled in local_config send the value to influxdb for this IO
coeff_b | float | no | use in conjunction of coeff_a to apply equation of the form `value_displayed = coeff_a * raw_value + coeff_b`. Default value is 0.0
offset | float | no | same as coeff_b, can be used alone. Default value is 0.0
id | string | yes | Unique ID identifying the Input/Output in calaos-server
gui_type | string | no | Internal graphical type for all calaos objects. Set automatically, read-only parameter.
period | float | no | Sampling time in microsecond. The value is read at this frequency. If this value is not set, calaos tries to read the interval parameter
interval | float | no | Sampling time in seconds. The value is read at this frequency. If this value is not set, the default value is 15s

## Conditions of OWTemp
Name | Description
---- | -----------
changed | Event on any change of temperature value 
 value | Event on a temperature value in degree Celsius 
 
## More Infos
* Calaos Wiki: http://calaos.fr/wiki/OneWire


# PingInputSwitch
A switch input based on the result of a ping command. Useful to detect presence of a host on the network.


Basic switch with press/release states.

## Parameters of PingInputSwitch
Name | Type | Mandatory | Description
---- | ---- | --------- | -----------
interval | int | no | Interval between pings in ms. Default to 15 sec
timeout | int | no | Timeout of the ping request in ms
host | string | yes | IP address or host where to send the ping
logged | bool | no | If enabled, and if influxdb is enabled in local_config send the value to influxdb for this IO
io_type | string | yes | IO type, can be "input", "output", "inout"
gui_type | string | no | Internal graphical type for all calaos objects. Set automatically, read-only parameter.
enabled | bool | no | Enable the Input/Output. The default value is true. This parameter is added if it's not found in the configuration.
visible | bool | no | A switch can't be visible. Always false.
log_history | bool | no | If enabled, write an entry in the history event log for this IO
name | string | yes | Name of Input/Output.
id | string | yes | Unique ID identifying the Input/Output in calaos-server

## Conditions of PingInputSwitch
Name | Description
---- | -----------
changed | Event on any change of state 
 false | The host is offline and/or does not respond to the ping 
 true | The host is online and respond to the ping 
 
# Planet - UNDOCUMENTED IO
SPANK SPANK SPANK : naughty programmer ! You did not add documentation for this IO, that's BAD :'(
Go document it in your code or you will burn in hell!



Planet IP Camera/Encoder. Camera can be viewed directly inside calaos and used in rules.

## Parameters of Planet
Name | Type | Mandatory | Description
---- | ---- | --------- | -----------
password | string | no | Password for user
username | string | no | Username for accessing the camera
model | string | yes | Camera model (ICA-210, ICA-210W, ICA-300, ICA-302, ICA-500) to use
rotate | int | no | Rotate the image. Set a value between. The value is in degrees. Example : -90 for  Counter Clock Wise rotation, 90 for Clock Wise rotationCW.
logged | bool | no | If enabled, and if influxdb is enabled in local_config send the value to influxdb for this IO
width | int | no | Width of the image, if this parameter is set, video will be resized to fit the given width. Let parameter empty to keep the original size.
io_type | string | yes | IO type, can be "input", "output", "inout"
gui_type | string | no | Internal graphical type for all calaos objects. Set automatically, read-only parameter.
enabled | bool | no | Enable the Input/Output. The default value is true. This parameter is added if it's not found in the configuration.
visible | bool | no | Display the Input/Output on all user interfaces if set. Default to true
log_history | bool | no | If enabled, write an entry in the history event log for this IO
name | string | yes | Name of Input/Output.
id | string | yes | Unique ID identifying the Input/Output in calaos-server


# Scenario
A scenario variable. Use this like a virtual button to start a scenario (list of actions)

## Parameters of Scenario
Name | Type | Mandatory | Description
---- | ---- | --------- | -----------
logged | bool | no | If enabled, and if influxdb is enabled in local_config send the value to influxdb for this IO
io_type | string | yes | IO type, can be "input", "output", "inout"
gui_type | string | no | Internal graphical type for all calaos objects. Set automatically, read-only parameter.
enabled | bool | no | Enable the Input/Output. The default value is true. This parameter is added if it's not found in the configuration.
visible | bool | no | Display the Input/Output on all user interfaces if set. Default to true
log_history | bool | no | If enabled, write an entry in the history event log for this IO
name | string | yes | Name of Input/Output.
auto_scenario | string | no | Internal use only for Auto Scenario. read only.
id | string | yes | Unique ID identifying the Input/Output in calaos-server

## Conditions of Scenario
Name | Description
---- | -----------
true | Event triggered when scenario is started 
 
## Actions of Scenario
Name | Description
---- | -----------
changed | Event triggered on any change 
 false | Stop the scenario (only for special looping scenarios) 
 true | Start the scenario 
 

# Squeezebox
#### Alias: slim
Squeezebox audio player allows control of a Squeezebox from Calaos

## Parameters of Squeezebox
Name | Type | Mandatory | Description
---- | ---- | --------- | -----------
port_cli | int | no | CLI port of LMS, default to 9090
host | string | yes | Logitech media server IP address
logged | bool | no | If enabled, and if influxdb is enabled in local_config send the value to influxdb for this IO
io_type | string | yes | IO type, can be "input", "output", "inout"
gui_type | string | no | Internal graphical type for all calaos objects. Set automatically, read-only parameter.
port_web | int | no | Web interface port of LMS, default to 9000.
enabled | bool | no | Enable the Input/Output. The default value is true. This parameter is added if it's not found in the configuration.
visible | bool | yes | Audio players are not displayed in rooms
log_history | bool | no | If enabled, write an entry in the history event log for this IO
name | string | yes | Name of Input/Output.
id | string | yes | Unique ID of squeezebox in LMS

## Conditions of Squeezebox
Name | Description
---- | -----------
onplaylistchange | Event when a change in the current playlist happens 
 onvolumechange | Event when a change of volume happens 
 onstop | Event when stopping player 
 onsongchange | Event when a new song is being played 
 onpause | Event when pausing player 
 onplay | Event when play is started 
 
## Actions of Squeezebox
Name | Description
---- | -----------
add <argument> | Add tracks to playlist. <argument> can be any of album_id:XX artist_id:XX playlist_id:XX, ... 
 play <argument> | Clear playlist and play argument. <argument> can be any of album_id:XX artist_id:XX playlist_id:XX, ... 
 pause | Pause player 
 sync <playerid> | Sync this player with an other 
 stop | Stop player 
 volume up 1 | Increase volume by a value 
 volume set 50 | Set current volume 
 previous | Play previous song in playlist 
 volume down 1 | Decrease volume by a value 
 play | Start playing 
 next | Play next song in playlist 
 power on | Switch player on 
 sleep 10 | Start sleep mode with X seconds 
 power off | Switch player off 
 unsync <playerid> | Stop sync of this player with an other 
 
# standard_mjpeg - UNDOCUMENTED IO
SPANK SPANK SPANK : naughty programmer ! You did not add documentation for this IO, that's BAD :'(
Go document it in your code or you will burn in hell!



MJPEG/Jpeg IP Camera. Camera can be viewed directly inside calaos and used in rules.

## Parameters of standard_mjpeg
Name | Type | Mandatory | Description
---- | ---- | --------- | -----------
ptz | bool | no | Set to true if camera has PTZ support
rotate | int | no | Rotate the image. Set a value between. The value is in degrees. Example : -90 for  Counter Clock Wise rotation, 90 for Clock Wise rotationCW.
logged | bool | no | If enabled, and if influxdb is enabled in local_config send the value to influxdb for this IO
width | int | no | Width of the image, if this parameter is set, video will be resized to fit the given width. Let parameter empty to keep the original size.
io_type | string | yes | IO type, can be "input", "output", "inout"
gui_type | string | no | Internal graphical type for all calaos objects. Set automatically, read-only parameter.
enabled | bool | no | Enable the Input/Output. The default value is true. This parameter is added if it's not found in the configuration.
url_mjpeg | string | no | URL for mjpeg stream support
visible | bool | no | Display the Input/Output on all user interfaces if set. Default to true
log_history | bool | no | If enabled, write an entry in the history event log for this IO
name | string | yes | Name of Input/Output.
url_jpeg | string | yes | URL for snapshot in jpeg format
id | string | yes | Unique ID identifying the Input/Output in calaos-server

# StandardMjpeg - UNDOCUMENTED IO
SPANK SPANK SPANK : naughty programmer ! You did not add documentation for this IO, that's BAD :'(
Go document it in your code or you will burn in hell!



MJPEG/Jpeg IP Camera. Camera can be viewed directly inside calaos and used in rules.

## Parameters of StandardMjpeg
Name | Type | Mandatory | Description
---- | ---- | --------- | -----------
ptz | bool | no | Set to true if camera has PTZ support
rotate | int | no | Rotate the image. Set a value between. The value is in degrees. Example : -90 for  Counter Clock Wise rotation, 90 for Clock Wise rotationCW.
logged | bool | no | If enabled, and if influxdb is enabled in local_config send the value to influxdb for this IO
width | int | no | Width of the image, if this parameter is set, video will be resized to fit the given width. Let parameter empty to keep the original size.
io_type | string | yes | IO type, can be "input", "output", "inout"
gui_type | string | no | Internal graphical type for all calaos objects. Set automatically, read-only parameter.
enabled | bool | no | Enable the Input/Output. The default value is true. This parameter is added if it's not found in the configuration.
url_mjpeg | string | no | URL for mjpeg stream support
visible | bool | no | Display the Input/Output on all user interfaces if set. Default to true
log_history | bool | no | If enabled, write an entry in the history event log for this IO
name | string | yes | Name of Input/Output.
url_jpeg | string | yes | URL for snapshot in jpeg format
id | string | yes | Unique ID identifying the Input/Output in calaos-server

# SynoSurveillanceStation - UNDOCUMENTED IO
SPANK SPANK SPANK : naughty programmer ! You did not add documentation for this IO, that's BAD :'(
Go document it in your code or you will burn in hell!



Synology Surveillance Station IP Camera. Camera can be viewed directly inside calaos and used in rules.

## Parameters of SynoSurveillanceStation
Name | Type | Mandatory | Description
---- | ---- | --------- | -----------
name | string | yes | Name of Input/Output.
log_history | bool | no | If enabled, write an entry in the history event log for this IO
id | string | yes | Unique ID identifying the Input/Output in calaos-server
gui_type | string | no | Internal graphical type for all calaos objects. Set automatically, read-only parameter.
io_type | string | yes | IO type, can be "input", "output", "inout"
width | int | no | Width of the image, if this parameter is set, video will be resized to fit the given width. Let parameter empty to keep the original size.
camera_profile | int | no | Profile to use for snapshot. 0- High quality, 1- Balanced, 2- Low bandwidth
logged | bool | no | If enabled, and if influxdb is enabled in local_config send the value to influxdb for this IO
rotate | int | no | Rotate the image. Set a value between. The value is in degrees. Example : -90 for  Counter Clock Wise rotation, 90 for Clock Wise rotationCW.
enabled | bool | no | Enable the Input/Output. The default value is true. This parameter is added if it's not found in the configuration.
url | string | yes | Full url to Synology nas. Ex: https://192.168.0.22:5000
username | string | yes | Username which can access Surveillance Station
camera_id | string | yes | ID of the camera
visible | bool | no | Display the Input/Output on all user interfaces if set. Default to true
password | string | yes | Password for user


# TeleinfoInputAnalog
Analog measurement retrieved from Teleinfo informations.


An analog input can be used to read analog values to display them and use them in rules.

## Parameters of TeleinfoInputAnalog
Name | Type | Mandatory | Description
---- | ---- | --------- | -----------
value | list | yes | All theses values are reported by the Teleinfo equipment as double.
port | string | yes | port on which to get Teleinfo information usually a serial port like /dev/ttyS0 or /dev/ttyAMA0
precision | int | no | Precision of the returned value. The value represents the number of decimal after the dot. The value is rounded like this : value = 19.275 => returned value 19.28 when preicision = 2, 19.3 when precision = 1, 19 when precision = 0
name | string | yes | Name of Input/Output.
io_style | list | yes | GUI style display. This will control the icon displayed on the UI
log_history | bool | no | If enabled, write an entry in the history event log for this IO
visible | bool | no | Display the Input/Output on all user interfaces if set. Default to true
display_warning | bool | no | Display a warning if value has not been updated for a long time. Default to true
enabled | bool | no | Enable the Input/Output. The default value is true. This parameter is added if it's not found in the configuration.
unit | string | no | Unit which will be displayed on the UI as a suffix.
coeff_a | float | no | use in conjunction of coeff_b to apply equation of the form `value_displayed = coeff_a * raw_value + coeff_b`. Default value is 1.0.
io_type | string | yes | IO type, can be "input", "output", "inout"
logged | bool | no | If enabled, and if influxdb is enabled in local_config send the value to influxdb for this IO
coeff_b | float | no | use in conjunction of coeff_a to apply equation of the form `value_displayed = coeff_a * raw_value + coeff_b`. Default value is 0.0
offset | float | no | same as coeff_b, can be used alone. Default value is 0.0
id | string | yes | Unique ID identifying the Input/Output in calaos-server
gui_type | string | no | Internal graphical type for all calaos objects. Set automatically, read-only parameter.
period | float | no | Sampling time in microsecond. The value is read at this frequency. If this value is not set, calaos tries to read the interval parameter
interval | float | no | Sampling time in seconds. The value is read at this frequency. If this value is not set, the default value is 15s

## Conditions of TeleinfoInputAnalog
Name | Description
---- | -----------
changed | Event on any change of value 
 value | Event on a specific value 
 

# TimeRange
#### Alias: InPlageHoraire
Represent a time range object. A time range is true if current time is in one of the included range, false otherwise. The time range also support weekdays and months.

## Parameters of TimeRange
Name | Type | Mandatory | Description
---- | ---- | --------- | -----------
logged | bool | no | If enabled, and if influxdb is enabled in local_config send the value to influxdb for this IO
io_type | string | yes | IO type, can be "input", "output", "inout"
gui_type | string | no | Internal graphical type for all calaos objects. Set automatically, read-only parameter.
enabled | bool | no | Enable the Input/Output. The default value is true. This parameter is added if it's not found in the configuration.
visible | bool | no | A time range can't be visible. Always false.
log_history | bool | no | If enabled, write an entry in the history event log for this IO
name | string | yes | Name of Input/Output.
id | string | yes | Unique ID identifying the Input/Output in calaos-server

## Conditions of TimeRange
Name | Description
---- | -----------
changed | Event on any change of range 
 false | Event triggered when exiting the range 
 true | Event triggered when entering the range 
 

# WebInputAnalog
Analog input read from a web document


An analog input can be used to read analog values to display them and use them in rules.

## Parameters of WebInputAnalog
Name | Type | Mandatory | Description
---- | ---- | --------- | -----------
precision | int | no | Precision of the returned value. The value represents the number of decimal after the dot. The value is rounded like this : value = 19.275 => returned value 19.28 when preicision = 2, 19.3 when precision = 1, 19 when precision = 0
name | string | yes | Name of Input/Output.
path | string | yes | The path where to found the value. This value can take multiple values depending on the file type. If file_type is JSON, the json file downloaded will be read, and the informations will be extracted from the path. for example weather[0]/description, try to read the description value of the 1 element of the array of the weather object.
If file_type is XML, the path is an xpath expression; Look here for syntax : http://www.w3schools.com/xsl/xpath_syntax.asp If file_type is TEXT, the downloaded file is returned as plain text file, and path must be in the form line/pos/separator Line is read, and is split using separator as delimiters The value returned is the value at pos in the split list. If the separator is not found, the whole line is returned. Example the file contains 
10.0,10.1,10.2,10.3
20.0,20.1,20.2,20.3
If the path is 2/4/, the value returne wil be 20.3

io_style | list | yes | GUI style display. This will control the icon displayed on the UI
log_history | bool | no | If enabled, write an entry in the history event log for this IO
visible | bool | no | Display the Input/Output on all user interfaces if set. Default to true
url | string | yes | URL where to download the document from
If URL begins with / or with file:// the data is read from the local file
display_warning | bool | no | Display a warning if value has not been updated for a long time. Default to true
enabled | bool | no | Enable the Input/Output. The default value is true. This parameter is added if it's not found in the configuration.
unit | string | no | Unit which will be displayed on the UI as a suffix.
coeff_a | float | no | use in conjunction of coeff_b to apply equation of the form `value_displayed = coeff_a * raw_value + coeff_b`. Default value is 1.0.
io_type | string | yes | IO type, can be "input", "output", "inout"
logged | bool | no | If enabled, and if influxdb is enabled in local_config send the value to influxdb for this IO
coeff_b | float | no | use in conjunction of coeff_a to apply equation of the form `value_displayed = coeff_a * raw_value + coeff_b`. Default value is 0.0
file_type | string | yes | File type of the document. Values can be xml, json or text.
offset | float | no | same as coeff_b, can be used alone. Default value is 0.0
id | string | yes | Unique ID identifying the Input/Output in calaos-server
gui_type | string | no | Internal graphical type for all calaos objects. Set automatically, read-only parameter.
period | float | no | Sampling time in microsecond. The value is read at this frequency. If this value is not set, calaos tries to read the interval parameter
interval | float | no | Sampling time in seconds. The value is read at this frequency. If this value is not set, the default value is 15s

## Conditions of WebInputAnalog
Name | Description
---- | -----------
changed | Event on any change of value 
 value | Event on a specific value 
 

# WebInputString
String input providing from a web document

## Parameters of WebInputString
Name | Type | Mandatory | Description
---- | ---- | --------- | -----------
url | string | yes | URL where to download the document from
If URL begins with / or with file:// the data is read from the local file
logged | bool | no | If enabled, and if influxdb is enabled in local_config send the value to influxdb for this IO
io_type | string | yes | IO type, can be "input", "output", "inout"
file_type | string | yes | File type of the document. Values can be xml, json or text.
gui_type | string | no | Internal graphical type for all calaos objects. Set automatically, read-only parameter.
enabled | bool | no | Enable the Input/Output. The default value is true. This parameter is added if it's not found in the configuration.
path | string | yes | The path where to found the value. This value can take multiple values depending on the file type. If file_type is JSON, the json file downloaded will be read, and the informations will be extracted from the path. for example weather[0]/description, try to read the description value of the 1 element of the array of the weather object.
If file_type is XML, the path is an xpath expression; Look here for syntax : http://www.w3schools.com/xsl/xpath_syntax.asp If file_type is TEXT, the downloaded file is returned as plain text file, and path must be in the form line/pos/separator Line is read, and is split using separator as delimiters The value returned is the value at pos in the split list. If the separator is not found, the whole line is returned. Example the file contains 
10.0,10.1,10.2,10.3
20.0,20.1,20.2,20.3
If the path is 2/4/, the value returne wil be 20.3

visible | bool | no | Display the Input/Output on all user interfaces if set. Default to true
log_history | bool | no | If enabled, write an entry in the history event log for this IO
name | string | yes | Name of Input/Output.
id | string | yes | Unique ID identifying the Input/Output in calaos-server


# WebInputTemp
Temperature input read from a web document


Temperature sensor input. Use for displaying temperature and to control heating devices with rules based on temperature value

## Parameters of WebInputTemp
Name | Type | Mandatory | Description
---- | ---- | --------- | -----------
precision | int | no | Precision of the returned value. The value represents the number of decimal after the dot. The value is rounded like this : value = 19.275 => returned value 19.28 when preicision = 2, 19.3 when precision = 1, 19 when precision = 0
name | string | yes | Name of Input/Output.
path | string | yes | The path where to found the value. This value can take multiple values depending on the file type. If file_type is JSON, the json file downloaded will be read, and the informations will be extracted from the path. for example weather[0]/description, try to read the description value of the 1 element of the array of the weather object.
If file_type is XML, the path is an xpath expression; Look here for syntax : http://www.w3schools.com/xsl/xpath_syntax.asp If file_type is TEXT, the downloaded file is returned as plain text file, and path must be in the form line/pos/separator Line is read, and is split using separator as delimiters The value returned is the value at pos in the split list. If the separator is not found, the whole line is returned. Example the file contains 
10.0,10.1,10.2,10.3
20.0,20.1,20.2,20.3
If the path is 2/4/, the value returne wil be 20.3

log_history | bool | no | If enabled, write an entry in the history event log for this IO
visible | bool | no | Display the Input/Output on all user interfaces if set. Default to true
url | string | yes | URL where to download the document from
If URL begins with / or with file:// the data is read from the local file
display_warning | bool | no | Display a warning if value has not been updated for a long time. Default to true
enabled | bool | no | Enable the Input/Output. The default value is true. This parameter is added if it's not found in the configuration.
coeff_a | float | no | use in conjunction of coeff_b to apply equation of the form `value_displayed = coeff_a * raw_value + coeff_b`. Default value is 1.0.
io_type | string | yes | IO type, can be "input", "output", "inout"
logged | bool | no | If enabled, and if influxdb is enabled in local_config send the value to influxdb for this IO
coeff_b | float | no | use in conjunction of coeff_a to apply equation of the form `value_displayed = coeff_a * raw_value + coeff_b`. Default value is 0.0
file_type | string | yes | File type of the document. Values can be xml, json or text.
offset | float | no | same as coeff_b, can be used alone. Default value is 0.0
id | string | yes | Unique ID identifying the Input/Output in calaos-server
gui_type | string | no | Internal graphical type for all calaos objects. Set automatically, read-only parameter.
period | float | no | Sampling time in microsecond. The value is read at this frequency. If this value is not set, calaos tries to read the interval parameter
interval | float | no | Sampling time in seconds. The value is read at this frequency. If this value is not set, the default value is 15s

## Conditions of WebInputTemp
Name | Description
---- | -----------
changed | Event on any change of temperature value 
 value | Event on a temperature value in degree Celsius 
 

# WebOutputAnalog
Analog output in a web request


Analog output. Useful to control analog output devices connected to calaos.

## Parameters of WebOutputAnalog
Name | Type | Mandatory | Description
---- | ---- | --------- | -----------
file_type | string | yes | File type of the document. Values can be xml, json or text.
name | string | yes | Name of Input/Output.
path | string | yes | The path where to found the value. This value can take multiple values depending on the file type. If file_type is JSON, the json file downloaded will be read, and the informations will be extracted from the path. for example weather[0]/description, try to read the description value of the 1 element of the array of the weather object.
If file_type is XML, the path is an xpath expression; Look here for syntax : http://www.w3schools.com/xsl/xpath_syntax.asp If file_type is TEXT, the downloaded file is returned as plain text file, and path must be in the form line/pos/separator Line is read, and is split using separator as delimiters The value returned is the value at pos in the split list. If the separator is not found, the whole line is returned. Example the file contains 
10.0,10.1,10.2,10.3
20.0,20.1,20.2,20.3
If the path is 2/4/, the value returne wil be 20.3

io_style | list | yes | GUI style display. This will control the icon displayed on the UI
log_history | bool | no | If enabled, write an entry in the history event log for this IO
visible | bool | no | Display the Input/Output on all user interfaces if set. Default to true
step | float | no | Set a step for increment/decrement value. Default is 1.0
url | string | yes | URL where to download the document from
If URL begins with / or with file:// the data is read from the local file
enabled | bool | no | Enable the Input/Output. The default value is true. This parameter is added if it's not found in the configuration.
id | string | yes | Unique ID identifying the Input/Output in calaos-server
gui_type | string | no | Internal graphical type for all calaos objects. Set automatically, read-only parameter.
io_type | string | yes | IO type, can be "input", "output", "inout"
logged | bool | no | If enabled, and if influxdb is enabled in local_config send the value to influxdb for this IO
coeff_b | float | no | use in conjunction of coeff_a to apply equation of the form `value_sent = coeff_a * raw_value + coeff_b`. Default value is 0.0
coeff_a | float | no | use in conjunction of coeff_b to apply equation of the form `value_sent = coeff_a * raw_value + coeff_b`. Default value is 1.0.
unit | string | no | Unit which will be displayed on the UI as a suffix.

## Conditions of WebOutputAnalog
Name | Description
---- | -----------
0 | Event on a specific number value 
 changed | Event on any change of value 
 value | Event on a specific value 
 
## Actions of WebOutputAnalog
Name | Description
---- | -----------
dec 1 | Decrement value by value 
 inc 1 | Increment value by value 
 dec | Decrement value with configured step 
 inc | Increment value with configured step 
 0 | Set a specific number value 
 

# WebOutputLight
Bool output written to a web document or URL


Basic light. This light have only 2 states, ON or OFF. Can also be used to control simple relays output

## Parameters of WebOutputLight
Name | Type | Mandatory | Description
---- | ---- | --------- | -----------
url | string | yes | URL where to POST the document to. The POST request is associated with the data field if not null. When no data is provided, Calaos substitutes __##VALUE##__ string with the value to send. For example if the url is http://example.com/api?value=__##VALUE##__ the url post will be :
http://example.com/api?value=20.3
The url is encoded before being sent.
If the URL begins with / or file:// the data is written to a file.
logged | bool | no | If enabled, and if influxdb is enabled in local_config send the value to influxdb for this IO
io_type | string | yes | IO type, can be "input", "output", "inout"
gui_type | string | no | Internal graphical type for all calaos objects. Set automatically, read-only parameter.
data_type | string | yes | The HTTP header Content-Type used when posting the document. It depends on the website, but you can use application/json application/xml as correct values.
enabled | bool | no | Enable the Input/Output. The default value is true. This parameter is added if it's not found in the configuration.
data | string | yes | The document send when posting data. This value can be void, in, that case the value is substituted in the url, otherwise the __##VALUE##__ contained in data is substituted with with the value to be sent.
visible | bool | no | Display the Input/Output on all user interfaces if set. Default to true
log_history | bool | no | If enabled, write an entry in the history event log for this IO
name | string | yes | Name of Input/Output.
io_style | list | yes | GUI style display. This will control the icon displayed on the UI
id | string | yes | Unique ID identifying the Input/Output in calaos-server

## Conditions of WebOutputLight
Name | Description
---- | -----------
false | Event when light is off 
 true | Event when light is on 
 changed | Event on any change of value 
 
## Actions of WebOutputLight
Name | Description
---- | -----------
impulse 200 | Do an impulse on light state. Set to true for X ms then reset to false 
 set_state true | Update internal light state without starting real action. This is useful when having updating the light state from an external source. 
 impulse 500 200 500 200 | Do an impulse on light state with a pattern.<br>Ex: 500 200 500 200 means: TRUE for 500ms, FALSE for 200ms, TRUE for 500ms, FALSE for 200ms<br>Ex: 500 loop 200 300 means: TRUE for 500ms, then loop the next steps for infinite, FALSE for 200ms, TRUE for 300ms<br>Ex: 100 100 200 old means: blinks and then set to the old start state (before impulse starts) 
 toggle | Invert light state 
 set_state false | Update internal light state without starting real action. This is useful when having updating the light state from an external source. 
 false | Switch the light off 
 true | Switch the light on 
 

# WebOutputLightRGB
RGB value written to a web document or URL


RGB light. Choose a color to be set for this light.

## Parameters of WebOutputLightRGB
Name | Type | Mandatory | Description
---- | ---- | --------- | -----------
url | string | yes | URL where to POST the document to. The POST request is associated with the data field if not null. When no data is provided, Calaos substitutes __##VALUE##__ string with the value to send. For example if the url is http://example.com/api?value=__##VALUE##__ the url post will be :
http://example.com/api?value=20.3
The url is encoded before being sent.
If the URL begins with / or file:// the data is written to a file.
logged | bool | no | If enabled, and if influxdb is enabled in local_config send the value to influxdb for this IO
io_type | string | yes | IO type, can be "input", "output", "inout"
gui_type | string | no | Internal graphical type for all calaos objects. Set automatically, read-only parameter.
data_type | string | yes | The HTTP header Content-Type used when posting the document. It depends on the website, but you can use application/json application/xml as correct values.
enabled | bool | no | Enable the Input/Output. The default value is true. This parameter is added if it's not found in the configuration.
data | string | yes | The document send when posting data. This value can be void, in, that case the value is substituted in the url, otherwise the __##VALUE##__ contained in data is substituted with with the value to be sent.
visible | bool | no | Display the Input/Output on all user interfaces if set. Default to true
log_history | bool | no | If enabled, write an entry in the history event log for this IO
name | string | yes | Name of Input/Output.
raw_value | bool | no | RGB value has #RRGGBB. Sometimes some web api take only RRGGBBformat. If raw_value is true, the # in front of the line isremoved. The default value for this parameter is false.
id | string | yes | Unique ID identifying the Input/Output in calaos-server

## Conditions of WebOutputLightRGB
Name | Description
---- | -----------
value | Event when light is at this value 
 changed | Event on any change of value 
 
## Actions of WebOutputLightRGB
Name | Description
---- | -----------
down_red 5 | Decrease intensity by X percent of red channel 
 true | Switch the light on 
 false | Switch the light off 
 set_state false | Update internal light state without starting real action. This is useful when having updating the light state from an external source. 
 down_green 5 | Decrease intensity by X percent of green channel 
 toggle | Invert the light state (ON/OFF) 
 set_state true | Update internal light state without starting real action. This is useful when having updating the light state from an external source. 
 down_blue 5 | Decrease intensity by X percent of blue channel 
 set_state #AA1294 | Update internal light state without starting real action. This is useful when having updating the light state from an external source. 
 up_red 5 | Increase intensity by X percent of red channel 
 set_red 50 | Set red channel to X percent 
 set_blue 50 | Set blue channel to X percent 
 up_green 5 | Increase intensity by X percent of green channel 
 set_green 50 | Set green channel to X percent 
 set #AA1294 | Set color. Color can be represented by using HTML notation: #AABBCC, rgb(50, 10, 30), hsl(11, 22, 33) 
 up_blue 5 | Increase intensity by X percent of blue channel 
 

# WebOutputString
String output written to a web document or URL

## Parameters of WebOutputString
Name | Type | Mandatory | Description
---- | ---- | --------- | -----------
url | string | yes | URL where to POST the document to. The POST request is associated with the data field if not null. When no data is provided, Calaos substitutes __##VALUE##__ string with the value to send. For example if the url is http://example.com/api?value=__##VALUE##__ the url post will be :
http://example.com/api?value=20.3
The url is encoded before being sent.
If the URL begins with / or file:// the data is written to a file.
logged | bool | no | If enabled, and if influxdb is enabled in local_config send the value to influxdb for this IO
io_type | string | yes | IO type, can be "input", "output", "inout"
file_type | string | yes | File type of the document. Values can be xml, json or text.
gui_type | string | no | Internal graphical type for all calaos objects. Set automatically, read-only parameter.
data_type | string | yes | The HTTP header Content-Type used when posting the document. It depends on the website, but you can use application/json application/xml as correct values.
enabled | bool | no | Enable the Input/Output. The default value is true. This parameter is added if it's not found in the configuration.
data | string | yes | The document send when posting data. This value can be void, in, that case the value is substituted in the url, otherwise the __##VALUE##__ contained in data is substituted with with the value to be sent.
path | string | yes | The path where to found the value. This value can take multiple values depending on the file type. If file_type is JSON, the json file downloaded will be read, and the informations will be extracted from the path. for example weather[0]/description, try to read the description value of the 1 element of the array of the weather object.
If file_type is XML, the path is an xpath expression; Look here for syntax : http://www.w3schools.com/xsl/xpath_syntax.asp If file_type is TEXT, the downloaded file is returned as plain text file, and path must be in the form line/pos/separator Line is read, and is split using separator as delimiters The value returned is the value at pos in the split list. If the separator is not found, the whole line is returned. Example the file contains 
10.0,10.1,10.2,10.3
20.0,20.1,20.2,20.3
If the path is 2/4/, the value returne wil be 20.3

visible | bool | no | Display the Input/Output on all user interfaces if set. Default to true
log_history | bool | no | If enabled, write an entry in the history event log for this IO
name | string | yes | Name of Input/Output.
id | string | yes | Unique ID identifying the Input/Output in calaos-server


# WIAnalog
#### Alias: WagoInputAnalog
Analog measurement with Wago module (like 0-10V, 4-20mA, ...)


An analog input can be used to read analog values to display them and use them in rules.

## Parameters of WIAnalog
Name | Type | Mandatory | Description
---- | ---- | --------- | -----------
port | int | no | Wago ethernet port, default to 502
precision | int | no | Precision of the returned value. The value represents the number of decimal after the dot. The value is rounded like this : value = 19.275 => returned value 19.28 when preicision = 2, 19.3 when precision = 1, 19 when precision = 0
var | int | yes | PLC address of the input sensor
name | string | yes | Name of Input/Output.
io_style | list | yes | GUI style display. This will control the icon displayed on the UI
log_history | bool | no | If enabled, write an entry in the history event log for this IO
visible | bool | no | Display the Input/Output on all user interfaces if set. Default to true
display_warning | bool | no | Display a warning if value has not been updated for a long time. Default to true
enabled | bool | no | Enable the Input/Output. The default value is true. This parameter is added if it's not found in the configuration.
unit | string | no | Unit which will be displayed on the UI as a suffix.
coeff_a | float | no | use in conjunction of coeff_b to apply equation of the form `value_displayed = coeff_a * raw_value + coeff_b`. Default value is 1.0.
io_type | string | yes | IO type, can be "input", "output", "inout"
logged | bool | no | If enabled, and if influxdb is enabled in local_config send the value to influxdb for this IO
coeff_b | float | no | use in conjunction of coeff_a to apply equation of the form `value_displayed = coeff_a * raw_value + coeff_b`. Default value is 0.0
host | string | yes | Wago PLC IP address on the network
offset | float | no | same as coeff_b, can be used alone. Default value is 0.0
id | string | yes | Unique ID identifying the Input/Output in calaos-server
gui_type | string | no | Internal graphical type for all calaos objects. Set automatically, read-only parameter.
period | float | no | Sampling time in microsecond. The value is read at this frequency. If this value is not set, calaos tries to read the interval parameter
interval | float | no | Sampling time in seconds. The value is read at this frequency. If this value is not set, the default value is 15s

## Conditions of WIAnalog
Name | Description
---- | -----------
changed | Event on any change of value 
 value | Event on a specific value 
 
## More Infos
* Calaos Wiki: http://calaos.fr/wiki/fr/entree_analog


# WIDigitalBP
#### Alias: WIDigital, WagoInputSwitch
Switch with digital input Wago modules (like 750-1405, ...)


Basic switch with press/release states.

## Parameters of WIDigitalBP
Name | Type | Mandatory | Description
---- | ---- | --------- | -----------
host | string | yes | Wago PLC IP address on the network
logged | bool | no | If enabled, and if influxdb is enabled in local_config send the value to influxdb for this IO
io_type | string | yes | IO type, can be "input", "output", "inout"
gui_type | string | no | Internal graphical type for all calaos objects. Set automatically, read-only parameter.
enabled | bool | no | Enable the Input/Output. The default value is true. This parameter is added if it's not found in the configuration.
visible | bool | no | A switch can't be visible. Always false.
port | int | no | Wago ethernet port, default to 502
log_history | bool | no | If enabled, write an entry in the history event log for this IO
name | string | yes | Name of Input/Output.
var | int | yes | PLC address of the digital input
id | string | yes | Unique ID identifying the Input/Output in calaos-server

## Conditions of WIDigitalBP
Name | Description
---- | -----------
changed | Event on any change of state 
 false | Event triggered when switch is released 
 true | Event triggered when switch is pressed 
 
## More Infos
* Calaos Wiki: http://calaos.fr/wiki/fr/750-1045


# WIDigitalLong
#### Alias: WagoInputSwitchLongPress
Switch long press with digital input Wago modules (like 750-1405, ...)


Long press switch. This switch supports single press and long press. User has 500ms to perform the long press.

## Parameters of WIDigitalLong
Name | Type | Mandatory | Description
---- | ---- | --------- | -----------
host | string | yes | Wago PLC IP address on the network
logged | bool | no | If enabled, and if influxdb is enabled in local_config send the value to influxdb for this IO
io_type | string | yes | IO type, can be "input", "output", "inout"
gui_type | string | no | Internal graphical type for all calaos objects. Set automatically, read-only parameter.
enabled | bool | no | Enable the Input/Output. The default value is true. This parameter is added if it's not found in the configuration.
visible | bool | no | A switch can't be visible. Always false.
port | int | no | Wago ethernet port, default to 502
log_history | bool | no | If enabled, write an entry in the history event log for this IO
name | string | yes | Name of Input/Output.
var | int | yes | PLC address of the digital input
id | string | yes | Unique ID identifying the Input/Output in calaos-server

## Conditions of WIDigitalLong
Name | Description
---- | -----------
changed | Event on any change of state 
 2 | Event triggered when switch is pressed at least for 500ms (long press) 
 1 | Event triggered when switch is pressed quickly 
 
## More Infos
* Calaos Wiki: http://calaos.fr/wiki/fr/750-1045


# WIDigitalTriple
#### Alias: WagoInputSwitchTriple
Switch triple click with digital input Wago modules (like 750-1405, ...)


Triple click switch. This switch can start 3 kind of actions. User has 500ms to do a multiple click.

## Parameters of WIDigitalTriple
Name | Type | Mandatory | Description
---- | ---- | --------- | -----------
host | string | yes | Wago PLC IP address on the network
logged | bool | no | If enabled, and if influxdb is enabled in local_config send the value to influxdb for this IO
io_type | string | yes | IO type, can be "input", "output", "inout"
gui_type | string | no | Internal graphical type for all calaos objects. Set automatically, read-only parameter.
enabled | bool | no | Enable the Input/Output. The default value is true. This parameter is added if it's not found in the configuration.
visible | bool | no | A switch can't be visible. Always false.
port | int | no | Wago ethernet port, default to 502
log_history | bool | no | If enabled, write an entry in the history event log for this IO
name | string | yes | Name of Input/Output.
var | int | yes | PLC address of the digital input
id | string | yes | Unique ID identifying the Input/Output in calaos-server

## Conditions of WIDigitalTriple
Name | Description
---- | -----------
changed | Event on any change of state 
 2 | Event triggered when switch is double clicked 
 3 | Event triggered when switch is triple clicked 
 1 | Event triggered when switch is single clicked 
 
## More Infos
* Calaos Wiki: http://calaos.fr/wiki/fr/750-1045


# WITemp
#### Alias: WagoInputTemp
Temperature measurement with Wago temperature module (like 750-460)


Temperature sensor input. Use for displaying temperature and to control heating devices with rules based on temperature value

## Parameters of WITemp
Name | Type | Mandatory | Description
---- | ---- | --------- | -----------
port | int | no | Wago ethernet port, default to 502
precision | int | no | Precision of the returned value. The value represents the number of decimal after the dot. The value is rounded like this : value = 19.275 => returned value 19.28 when preicision = 2, 19.3 when precision = 1, 19 when precision = 0
var | int | yes | PLC address of the input sensor
name | string | yes | Name of Input/Output.
log_history | bool | no | If enabled, write an entry in the history event log for this IO
visible | bool | no | Display the Input/Output on all user interfaces if set. Default to true
display_warning | bool | no | Display a warning if value has not been updated for a long time. Default to true
enabled | bool | no | Enable the Input/Output. The default value is true. This parameter is added if it's not found in the configuration.
coeff_a | float | no | use in conjunction of coeff_b to apply equation of the form `value_displayed = coeff_a * raw_value + coeff_b`. Default value is 1.0.
io_type | string | yes | IO type, can be "input", "output", "inout"
logged | bool | no | If enabled, and if influxdb is enabled in local_config send the value to influxdb for this IO
coeff_b | float | no | use in conjunction of coeff_a to apply equation of the form `value_displayed = coeff_a * raw_value + coeff_b`. Default value is 0.0
host | string | yes | Wago PLC IP address on the network
offset | float | no | same as coeff_b, can be used alone. Default value is 0.0
id | string | yes | Unique ID identifying the Input/Output in calaos-server
gui_type | string | no | Internal graphical type for all calaos objects. Set automatically, read-only parameter.
period | float | no | Sampling time in microsecond. The value is read at this frequency. If this value is not set, calaos tries to read the interval parameter
interval | float | no | Sampling time in seconds. The value is read at this frequency. If this value is not set, the default value is 15s

## Conditions of WITemp
Name | Description
---- | -----------
changed | Event on any change of temperature value 
 value | Event on a temperature value in degree Celsius 
 
## More Infos
* Calaos Wiki: http://calaos.fr/wiki/fr/750-460


# WOAnalog
#### Alias: WagoOutputAnalog
Analog output with Wago module (like 0-10V, 4-20mA, ...)


Analog output. Useful to control analog output devices connected to calaos.

## Parameters of WOAnalog
Name | Type | Mandatory | Description
---- | ---- | --------- | -----------
port | int | no | Wago ethernet port, default to 502
host | string | yes | Wago PLC IP address on the network
var | int | yes | PLC address of the output
name | string | yes | Name of Input/Output.
io_style | list | yes | GUI style display. This will control the icon displayed on the UI
log_history | bool | no | If enabled, write an entry in the history event log for this IO
visible | bool | no | Display the Input/Output on all user interfaces if set. Default to true
step | float | no | Set a step for increment/decrement value. Default is 1.0
enabled | bool | no | Enable the Input/Output. The default value is true. This parameter is added if it's not found in the configuration.
id | string | yes | Unique ID identifying the Input/Output in calaos-server
gui_type | string | no | Internal graphical type for all calaos objects. Set automatically, read-only parameter.
io_type | string | yes | IO type, can be "input", "output", "inout"
logged | bool | no | If enabled, and if influxdb is enabled in local_config send the value to influxdb for this IO
coeff_b | float | no | use in conjunction of coeff_a to apply equation of the form `value_sent = coeff_a * raw_value + coeff_b`. Default value is 0.0
coeff_a | float | no | use in conjunction of coeff_b to apply equation of the form `value_sent = coeff_a * raw_value + coeff_b`. Default value is 1.0.
unit | string | no | Unit which will be displayed on the UI as a suffix.

## Conditions of WOAnalog
Name | Description
---- | -----------
0 | Event on a specific number value 
 changed | Event on any change of value 
 value | Event on a specific value 
 
## Actions of WOAnalog
Name | Description
---- | -----------
dec 1 | Decrement value by value 
 inc 1 | Increment value by value 
 dec | Decrement value with configured step 
 inc | Increment value with configured step 
 0 | Set a specific number value 
 
## More Infos
* Calaos Wiki: http://calaos.fr/wiki/fr/sortie_analog


# WODali
#### Alias: WagoOutputDimmer
Light using DALI or DMX. For DALI you need a 750-641 wago module. For DMX, a DMX4ALL-LAN device connected to the Wago PLC.


Light with dimming control. Light intensity can be changed for this light.

## Parameters of WODali
Name | Type | Mandatory | Description
---- | ---- | --------- | -----------
name | string | yes | Name of Input/Output.
log_history | bool | no | If enabled, write an entry in the history event log for this IO
port | int | no | Wago ethernet port, default to 502
visible | bool | no | Display the Input/Output on all user interfaces if set. Default to true
fade_time | int | no | DALI fade time. value is between 1-10
line | int | no | DALI bus line, usually 1
enabled | bool | no | Enable the Input/Output. The default value is true. This parameter is added if it's not found in the configuration.
id | string | yes | Unique ID identifying the Input/Output in calaos-server
address | int | yes | Device address. For DALI address is between 1-64. For DMX, the address starts at 100. So for DMX device 5, address should be 105
gui_type | string | no | Internal graphical type for all calaos objects. Set automatically, read-only parameter.
io_type | string | yes | IO type, can be "input", "output", "inout"
logged | bool | no | If enabled, and if influxdb is enabled in local_config send the value to influxdb for this IO
host | string | yes | Wago PLC IP address on the network
group | int | no | Set to 1 if address is a DALI group address, set to 0 otherwise.

## Conditions of WODali
Name | Description
---- | -----------
value | Event when light is at this value 
 changed | Event on any change of value 
 
## Actions of WODali
Name | Description
---- | -----------
hold stop | Dynamically change light intensity when holding a switch (stop action) 
 true | Switch the light on 
 false | Switch the light off 
 set off 50 | Set light value without switching on. This will be the light intensity for the next ON 
 set_state false | Update internal light state without starting real action. This is useful when having updating the light state from an external source. 
 toggle | Invert the light state 
 down 5 | Decrease intensity by X percent 
 impulse 500 200 500 200 | Do an impulse on light state with a pattern.<br>Ex: 500 200 500 200 means: TRUE for 500ms, FALSE for 200ms, TRUE for 500ms, FALSE for 200ms<br>Ex: 500 loop 200 300 means: TRUE for 500ms, then loop the next steps for infinite, FALSE for 200ms, TRUE for 300ms<br>Ex: 100 100 200 old means: blinks and then set to the old start state (before impulse starts) 
 impulse 200 | Do an impulse on light state. Set to true for X ms then reset to false 
 set_state true | Update internal light state without starting real action. This is useful when having updating the light state from an external source. 
 hold press | Dynamically change light intensity when holding a switch (press action) 
 set_state 50 | Update internal light state without starting real action. This is useful when having updating the light state from an external source. 
 set 50 | Set light intensity and swith on if light is off 
 up 5 | Increase intensity by X percent 
 
## More Infos
* Calaos Wiki: http://calaos.fr/wiki/fr/750-641
* Calaos Wiki: http://calaos.fr/wiki/fr/dmx-lan


# WODaliRVB
#### Alias: WagoOutputDimmerRGB
RGB Light using DALI or DMX. To work you need 3 DALI/DMX channels. For DALI you need a 750-641 wago module. For DMX, a DMX4ALL-LAN device connected to the Wago PLC.


RGB light. Choose a color to be set for this light.

## Parameters of WODaliRVB
Name | Type | Mandatory | Description
---- | ---- | --------- | -----------
bfade_time | int | no | DALI fade time for blue channel. value is between 1-10
bgroup | int | no | Set to 1 if address for blue channel is a DALI group address, set to 0 otherwise.
ggroup | int | no | Set to 1 if address for green channel is a DALI group address, set to 0 otherwise.
gaddress | int | yes | Device address for green channel. For DALI address is between 1-64. For DMX, the address starts at 100. So for DMX device 5, address should be 105
gline | int | no | DALI bus line for green channel, usually 1
rfade_time | int | no | DALI fade time for red channel. value is between 1-10
name | string | yes | Name of Input/Output.
baddress | int | yes | Device address for blue channel. For DALI address is between 1-64. For DMX, the address starts at 100. So for DMX device 5, address should be 105
log_history | bool | no | If enabled, write an entry in the history event log for this IO
port | int | no | Wago ethernet port, default to 502
gfade_time | int | no | DALI fade time for green channel. value is between 1-10
visible | bool | no | Display the Input/Output on all user interfaces if set. Default to true
enabled | bool | no | Enable the Input/Output. The default value is true. This parameter is added if it's not found in the configuration.
id | string | yes | Unique ID identifying the Input/Output in calaos-server
gui_type | string | no | Internal graphical type for all calaos objects. Set automatically, read-only parameter.
bline | int | no | DALI bus line for blue channel, usually 1
io_type | string | yes | IO type, can be "input", "output", "inout"
host | string | yes | Wago PLC IP address on the network
rline | int | no | DALI bus line for red channel, usually 1
raddress | int | yes | Device address for red channel. For DALI address is between 1-64. For DMX, the address starts at 100. So for DMX device 5, address should be 105
logged | bool | no | If enabled, and if influxdb is enabled in local_config send the value to influxdb for this IO
rgroup | int | no | Set to 1 if address for red channel is a DALI group address, set to 0 otherwise.

## Conditions of WODaliRVB
Name | Description
---- | -----------
value | Event when light is at this value 
 changed | Event on any change of value 
 
## Actions of WODaliRVB
Name | Description
---- | -----------
down_red 5 | Decrease intensity by X percent of red channel 
 true | Switch the light on 
 false | Switch the light off 
 set_state false | Update internal light state without starting real action. This is useful when having updating the light state from an external source. 
 down_green 5 | Decrease intensity by X percent of green channel 
 toggle | Invert the light state (ON/OFF) 
 set_state true | Update internal light state without starting real action. This is useful when having updating the light state from an external source. 
 down_blue 5 | Decrease intensity by X percent of blue channel 
 set_state #AA1294 | Update internal light state without starting real action. This is useful when having updating the light state from an external source. 
 up_red 5 | Increase intensity by X percent of red channel 
 set_red 50 | Set red channel to X percent 
 set_blue 50 | Set blue channel to X percent 
 up_green 5 | Increase intensity by X percent of green channel 
 set_green 50 | Set green channel to X percent 
 set #AA1294 | Set color. Color can be represented by using HTML notation: #AABBCC, rgb(50, 10, 30), hsl(11, 22, 33) 
 up_blue 5 | Increase intensity by X percent of blue channel 
 
## More Infos
* Calaos Wiki: http://calaos.fr/wiki/fr/750-641
* Calaos Wiki: http://calaos.fr/wiki/fr/dmx-lan


# WODigital
#### Alias: WagoOutputLight
Simple light or relay control using wago digital output modules (like 750-1504, ...)


Basic light. This light have only 2 states, ON or OFF. Can also be used to control simple relays output

## Parameters of WODigital
Name | Type | Mandatory | Description
---- | ---- | --------- | -----------
knx | bool | no | Set to true if output is a KNX device (only for 750-849 with KNX/TP1 module)
var | int | yes | PLC address of the output
name | string | yes | Name of Input/Output.
io_style | list | yes | GUI style display. This will control the icon displayed on the UI
log_history | bool | no | If enabled, write an entry in the history event log for this IO
port | int | no | Wago ethernet port, default to 502
visible | bool | no | Display the Input/Output on all user interfaces if set. Default to true
enabled | bool | no | Enable the Input/Output. The default value is true. This parameter is added if it's not found in the configuration.
id | string | yes | Unique ID identifying the Input/Output in calaos-server
gui_type | string | no | Internal graphical type for all calaos objects. Set automatically, read-only parameter.
io_type | string | yes | IO type, can be "input", "output", "inout"
logged | bool | no | If enabled, and if influxdb is enabled in local_config send the value to influxdb for this IO
host | string | yes | Wago PLC IP address on the network
wago_841 | bool | yes | Should be false if PLC is 750-842, true otherwise

## Conditions of WODigital
Name | Description
---- | -----------
false | Event when light is off 
 true | Event when light is on 
 changed | Event on any change of value 
 
## Actions of WODigital
Name | Description
---- | -----------
impulse 200 | Do an impulse on light state. Set to true for X ms then reset to false 
 set_state true | Update internal light state without starting real action. This is useful when having updating the light state from an external source. 
 impulse 500 200 500 200 | Do an impulse on light state with a pattern.<br>Ex: 500 200 500 200 means: TRUE for 500ms, FALSE for 200ms, TRUE for 500ms, FALSE for 200ms<br>Ex: 500 loop 200 300 means: TRUE for 500ms, then loop the next steps for infinite, FALSE for 200ms, TRUE for 300ms<br>Ex: 100 100 200 old means: blinks and then set to the old start state (before impulse starts) 
 toggle | Invert light state 
 set_state false | Update internal light state without starting real action. This is useful when having updating the light state from an external source. 
 false | Switch the light off 
 true | Switch the light on 
 
## More Infos
* Calaos Wiki: http://calaos.fr/wiki/fr/750-1504


# WOLOutputBool
Wake On Lan output object. Send wake-on-lan packet to a device on the network.

## Parameters of WOLOutputBool
Name | Type | Mandatory | Description
---- | ---- | --------- | -----------
interval | int | no | Interval between pings in ms. Default to 15 sec
logged | bool | no | If enabled, and if influxdb is enabled in local_config send the value to influxdb for this IO
io_type | string | yes | IO type, can be "input", "output", "inout"
gui_type | string | no | Internal graphical type for all calaos objects. Set automatically, read-only parameter.
enabled | bool | no | Enable the Input/Output. The default value is true. This parameter is added if it's not found in the configuration.
visible | bool | no | Display the Input/Output on all user interfaces if set. Default to true
address | string | yes | Ethernet MAC address of the host to wake up
log_history | bool | no | If enabled, write an entry in the history event log for this IO
name | string | yes | Name of Input/Output.
id | string | yes | Unique ID identifying the Input/Output in calaos-server

## Actions of WOLOutputBool
Name | Description
---- | -----------
true | Send wake on lan packet to the configured device 
 

# WOVolet
#### Alias: WagoOutputShutter
Simple shutter using wago digital output modules (like 750-1504, ...)


Simple shutter. This shutter supports open/close states, as well as impulse shutters.

## Parameters of WOVolet
Name | Type | Mandatory | Description
---- | ---- | --------- | -----------
knx | bool | no | Set to true if output is a KNX device (only for 750-849 with KNX/TP1 module)
wago_841 | bool | yes | Should be false if PLC is 750-842, true otherwise
var_down | int | yes | Digital output address on the PLC for closing the shutter
var_up | int | yes | Digital output address on the PLC for opening the shutter
name | string | yes | Name of Input/Output.
log_history | bool | no | If enabled, write an entry in the history event log for this IO
visible | bool | no | Display the Input/Output on all user interfaces if set. Default to true
enabled | bool | no | Enable the Input/Output. The default value is true. This parameter is added if it's not found in the configuration.
id | string | yes | Unique ID identifying the Input/Output in calaos-server
gui_type | string | no | Internal graphical type for all calaos objects. Set automatically, read-only parameter.
io_type | string | yes | IO type, can be "input", "output", "inout"
logged | bool | no | If enabled, and if influxdb is enabled in local_config send the value to influxdb for this IO
port | int | no | Wago ethernet port, default to 502
stop_both | bool | no | If in impulse mode, some shutters needs to activate both up dans down relays when stopping the shutter
time | int | yes | Time in sec for shutter to open or close
impulse_time | int | no | Impulse time for shutter that needs impulse instead of holding up/down relays. If set to 0 impulse shutter is disabled. Time is in ms. Default to 0
host | string | yes | Wago PLC IP address on the network

## Conditions of WOVolet
Name | Description
---- | -----------
false | Event when shutter is closed 
 true | Event when shutter is open 
 changed | Event on any change of shutter state 
 
## Actions of WOVolet
Name | Description
---- | -----------
impulse up 200 | Open shutter for X ms 
 set_state true | Update internal shutter state without starting real action. This is useful when having updating the shutter state from an external source. 
 impulse down 200 | Close shutter for X ms 
 toggle | Invert shutter state 
 set_state false | Update internal shutter state without starting real action. This is useful when having updating the shutter state from an external source. 
 stop | Stop the shutter 
 down | Close the shutter 
 up | Open the shutter 
 
## More Infos
* Calaos Wiki: http://calaos.fr/wiki/fr/750-1504


# WOVoletSmart
#### Alias: WagoOutputShutterSmart
Smart shutter using wago digital output modules (like 750-1504, ...)


Smart shutter. This shutter calculates the position of the shutter based on the time it takes to open and close. It then allows to set directly the shutter at a specified position.

## Parameters of WOVoletSmart
Name | Type | Mandatory | Description
---- | ---- | --------- | -----------
knx | bool | no | Set to true if output is a KNX device (only for 750-849 with KNX/TP1 module)
wago_841 | bool | yes | Should be false if PLC is 750-842, true otherwise
var_down | int | yes | Digital output address on the PLC for closing the shutter
var_up | int | yes | Digital output address on the PLC for opening the shutter
name | string | yes | Name of Input/Output.
log_history | bool | no | If enabled, write an entry in the history event log for this IO
visible | bool | no | Display the Input/Output on all user interfaces if set. Default to true
enabled | bool | no | Enable the Input/Output. The default value is true. This parameter is added if it's not found in the configuration.
id | string | yes | Unique ID identifying the Input/Output in calaos-server
gui_type | string | no | Internal graphical type for all calaos objects. Set automatically, read-only parameter.
time_up | int | yes | Time in sec for shutter to be fully open. The more accurate, the better it will work
io_type | string | yes | IO type, can be "input", "output", "inout"
logged | bool | no | If enabled, and if influxdb is enabled in local_config send the value to influxdb for this IO
time_down | int | yes | Time in sec for shutter to fully closed. The more accurate, the better it will work
port | int | no | Wago ethernet port, default to 502
stop_both | bool | no | If in impulse mode, some shutters needs to activate both up dans down relays when stopping the shutter
impulse_time | int | no | Impulse time for shutter that needs impulse instead of holding up/down relays. If set to 0 impulse shutter is disabled. Time is in ms. Default to 0
host | string | yes | Wago PLC IP address on the network

## Conditions of WOVoletSmart
Name | Description
---- | -----------
false | Event when shutter is closed 
 true | Event when shutter is open 
 changed | Event on any change of shutter state 
 
## Actions of WOVoletSmart
Name | Description
---- | -----------
up 5 | Open the shutter by X percent 
 set 50 | Set shutter at position X in percent 
 impulse up 200 | Open shutter for X ms 
 set_state true | Update internal shutter state without starting real action. This is useful when having updating the shutter state from an external source. 
 down 5 | Close the shutter by X percent 
 impulse down 200 | Close shutter for X ms 
 toggle | Invert shutter state 
 set_state false | Update internal shutter state without starting real action. This is useful when having updating the shutter state from an external source. 
 calibrate | Start calibration on shutter. This opens fully the shutter and resets all internal position values. Use this if shutter sync is lost. 
 stop | Stop the shutter 
 down | Close the shutter 
 up | Open the shutter 
 
## More Infos
* Calaos Wiki: http://calaos.fr/wiki/fr/750-1504


# X10Output
Light dimmer using X10 and heyu.


Light with dimming control. Light intensity can be changed for this light.

## Parameters of X10Output
Name | Type | Mandatory | Description
---- | ---- | --------- | -----------
logged | bool | no | If enabled, and if influxdb is enabled in local_config send the value to influxdb for this IO
io_type | string | yes | IO type, can be "input", "output", "inout"
gui_type | string | no | Internal graphical type for all calaos objects. Set automatically, read-only parameter.
enabled | bool | no | Enable the Input/Output. The default value is true. This parameter is added if it's not found in the configuration.
code | string | yes | House code of the X10 light device
visible | bool | no | Display the Input/Output on all user interfaces if set. Default to true
log_history | bool | no | If enabled, write an entry in the history event log for this IO
name | string | yes | Name of Input/Output.
id | string | yes | Unique ID identifying the Input/Output in calaos-server

## Conditions of X10Output
Name | Description
---- | -----------
value | Event when light is at this value 
 changed | Event on any change of value 
 
## Actions of X10Output
Name | Description
---- | -----------
hold stop | Dynamically change light intensity when holding a switch (stop action) 
 true | Switch the light on 
 false | Switch the light off 
 set off 50 | Set light value without switching on. This will be the light intensity for the next ON 
 set_state false | Update internal light state without starting real action. This is useful when having updating the light state from an external source. 
 toggle | Invert the light state 
 down 5 | Decrease intensity by X percent 
 impulse 500 200 500 200 | Do an impulse on light state with a pattern.<br>Ex: 500 200 500 200 means: TRUE for 500ms, FALSE for 200ms, TRUE for 500ms, FALSE for 200ms<br>Ex: 500 loop 200 300 means: TRUE for 500ms, then loop the next steps for infinite, FALSE for 200ms, TRUE for 300ms<br>Ex: 100 100 200 old means: blinks and then set to the old start state (before impulse starts) 
 impulse 200 | Do an impulse on light state. Set to true for X ms then reset to false 
 set_state true | Update internal light state without starting real action. This is useful when having updating the light state from an external source. 
 hold press | Dynamically change light intensity when holding a switch (press action) 
 set_state 50 | Update internal light state without starting real action. This is useful when having updating the light state from an external source. 
 set 50 | Set light intensity and swith on if light is off 
 up 5 | Increase intensity by X percent 
 

# xPLInputAnalog
xPL analog sensor


An analog input can be used to read analog values to display them and use them in rules.

## Parameters of xPLInputAnalog
Name | Type | Mandatory | Description
---- | ---- | --------- | -----------
precision | int | no | Precision of the returned value. The value represents the number of decimal after the dot. The value is rounded like this : value = 19.275 => returned value 19.28 when preicision = 2, 19.3 when precision = 1, 19 when precision = 0
name | string | yes | Name of Input/Output.
io_style | list | yes | GUI style display. This will control the icon displayed on the UI
log_history | bool | no | If enabled, write an entry in the history event log for this IO
visible | bool | no | Display the Input/Output on all user interfaces if set. Default to true
display_warning | bool | no | Display a warning if value has not been updated for a long time. Default to true
enabled | bool | no | Enable the Input/Output. The default value is true. This parameter is added if it's not found in the configuration.
unit | string | no | Unit which will be displayed on the UI as a suffix.
coeff_a | float | no | use in conjunction of coeff_b to apply equation of the form `value_displayed = coeff_a * raw_value + coeff_b`. Default value is 1.0.
sensor | string | yes | Sensor ID, as set in your xPL network
io_type | string | yes | IO type, can be "input", "output", "inout"
logged | bool | no | If enabled, and if influxdb is enabled in local_config send the value to influxdb for this IO
coeff_b | float | no | use in conjunction of coeff_a to apply equation of the form `value_displayed = coeff_a * raw_value + coeff_b`. Default value is 0.0
offset | float | no | same as coeff_b, can be used alone. Default value is 0.0
source | string | yes | Source name, as set in your xPL network (Format VendorId-DeviceId.Instance)
id | string | yes | Unique ID identifying the Input/Output in calaos-server
gui_type | string | no | Internal graphical type for all calaos objects. Set automatically, read-only parameter.
period | float | no | Sampling time in microsecond. The value is read at this frequency. If this value is not set, calaos tries to read the interval parameter
interval | float | no | Sampling time in seconds. The value is read at this frequency. If this value is not set, the default value is 15s

## Conditions of xPLInputAnalog
Name | Description
---- | -----------
changed | Event on any change of value 
 value | Event on a specific value 
 

# xPLInputAnalog
xPL string sensor

## Parameters of xPLInputAnalog
Name | Type | Mandatory | Description
---- | ---- | --------- | -----------
logged | bool | no | If enabled, and if influxdb is enabled in local_config send the value to influxdb for this IO
io_type | string | yes | IO type, can be "input", "output", "inout"
gui_type | string | no | Internal graphical type for all calaos objects. Set automatically, read-only parameter.
enabled | bool | no | Enable the Input/Output. The default value is true. This parameter is added if it's not found in the configuration.
visible | bool | no | Display the Input/Output on all user interfaces if set. Default to true
sensor | string | yes | Sensor ID, as set in your xPL network
source | string | yes | Source name, as set in your xPL network (Format VendorId-DeviceId.Instance)
log_history | bool | no | If enabled, write an entry in the history event log for this IO
name | string | yes | Name of Input/Output.
id | string | yes | Unique ID identifying the Input/Output in calaos-server


# xPLInputSwitch
xPL input switch


Basic switch with press/release states.

## Parameters of xPLInputSwitch
Name | Type | Mandatory | Description
---- | ---- | --------- | -----------
logged | bool | no | If enabled, and if influxdb is enabled in local_config send the value to influxdb for this IO
io_type | string | yes | IO type, can be "input", "output", "inout"
gui_type | string | no | Internal graphical type for all calaos objects. Set automatically, read-only parameter.
enabled | bool | no | Enable the Input/Output. The default value is true. This parameter is added if it's not found in the configuration.
visible | bool | no | A switch can't be visible. Always false.
sensor | string | yes | Sensor ID, as set in your xPL network
source | string | yes | Source name, as set in your xPL network (Format VendorId-DeviceId.Instance)
log_history | bool | no | If enabled, write an entry in the history event log for this IO
name | string | yes | Name of Input/Output.
id | string | yes | Unique ID identifying the Input/Output in calaos-server

## Conditions of xPLInputSwitch
Name | Description
---- | -----------
changed | Event on any change of state 
 false | Event triggered when switch is released 
 true | Event triggered when switch is pressed 
 

# xPLInputTemp
xPL temperature sensor


Temperature sensor input. Use for displaying temperature and to control heating devices with rules based on temperature value

## Parameters of xPLInputTemp
Name | Type | Mandatory | Description
---- | ---- | --------- | -----------
precision | int | no | Precision of the returned value. The value represents the number of decimal after the dot. The value is rounded like this : value = 19.275 => returned value 19.28 when preicision = 2, 19.3 when precision = 1, 19 when precision = 0
name | string | yes | Name of Input/Output.
log_history | bool | no | If enabled, write an entry in the history event log for this IO
visible | bool | no | Display the Input/Output on all user interfaces if set. Default to true
display_warning | bool | no | Display a warning if value has not been updated for a long time. Default to true
enabled | bool | no | Enable the Input/Output. The default value is true. This parameter is added if it's not found in the configuration.
coeff_a | float | no | use in conjunction of coeff_b to apply equation of the form `value_displayed = coeff_a * raw_value + coeff_b`. Default value is 1.0.
sensor | string | yes | Sensor ID, as set in your xPL network
io_type | string | yes | IO type, can be "input", "output", "inout"
logged | bool | no | If enabled, and if influxdb is enabled in local_config send the value to influxdb for this IO
coeff_b | float | no | use in conjunction of coeff_a to apply equation of the form `value_displayed = coeff_a * raw_value + coeff_b`. Default value is 0.0
offset | float | no | same as coeff_b, can be used alone. Default value is 0.0
source | string | yes | Source name, as set in your xPL network (Format VendorId-DeviceId.Instance)
id | string | yes | Unique ID identifying the Input/Output in calaos-server
gui_type | string | no | Internal graphical type for all calaos objects. Set automatically, read-only parameter.
period | float | no | Sampling time in microsecond. The value is read at this frequency. If this value is not set, calaos tries to read the interval parameter
interval | float | no | Sampling time in seconds. The value is read at this frequency. If this value is not set, the default value is 15s

## Conditions of xPLInputTemp
Name | Description
---- | -----------
changed | Event on any change of temperature value 
 value | Event on a temperature value in degree Celsius 
 

# xPLOutputAnalog
Analog output controlled by xPL Protocol


Analog output. Useful to control analog output devices connected to calaos.

## Parameters of xPLOutputAnalog
Name | Type | Mandatory | Description
---- | ---- | --------- | -----------
actuator | string | yes | Actuator ID, as set in your xPL network
name | string | yes | Name of Input/Output.
io_style | list | yes | GUI style display. This will control the icon displayed on the UI
log_history | bool | no | If enabled, write an entry in the history event log for this IO
visible | bool | no | Display the Input/Output on all user interfaces if set. Default to true
step | float | no | Set a step for increment/decrement value. Default is 1.0
enabled | bool | no | Enable the Input/Output. The default value is true. This parameter is added if it's not found in the configuration.
source | string | yes | Source name, as set in your xPL network (Format VendorId-DeviceId.Instance)
id | string | yes | Unique ID identifying the Input/Output in calaos-server
gui_type | string | no | Internal graphical type for all calaos objects. Set automatically, read-only parameter.
io_type | string | yes | IO type, can be "input", "output", "inout"
logged | bool | no | If enabled, and if influxdb is enabled in local_config send the value to influxdb for this IO
coeff_b | float | no | use in conjunction of coeff_a to apply equation of the form `value_sent = coeff_a * raw_value + coeff_b`. Default value is 0.0
coeff_a | float | no | use in conjunction of coeff_b to apply equation of the form `value_sent = coeff_a * raw_value + coeff_b`. Default value is 1.0.
unit | string | no | Unit which will be displayed on the UI as a suffix.

## Conditions of xPLOutputAnalog
Name | Description
---- | -----------
0 | Event on a specific number value 
 changed | Event on any change of value 
 value | Event on a specific value 
 
## Actions of xPLOutputAnalog
Name | Description
---- | -----------
dec 1 | Decrement value by value 
 inc 1 | Increment value by value 
 dec | Decrement value with configured step 
 inc | Increment value with configured step 
 0 | Set a specific number value 
 

# xPLOutputAnalog
Analog output controlled by xPL Protocol

## Parameters of xPLOutputAnalog
Name | Type | Mandatory | Description
---- | ---- | --------- | -----------
actuator | string | yes | Actuator ID, as set in your xPL network
logged | bool | no | If enabled, and if influxdb is enabled in local_config send the value to influxdb for this IO
io_type | string | yes | IO type, can be "input", "output", "inout"
gui_type | string | no | Internal graphical type for all calaos objects. Set automatically, read-only parameter.
enabled | bool | no | Enable the Input/Output. The default value is true. This parameter is added if it's not found in the configuration.
visible | bool | no | Display the Input/Output on all user interfaces if set. Default to true
source | string | yes | Source name, as set in your xPL network (Format VendorId-DeviceId.Instance)
log_history | bool | no | If enabled, write an entry in the history event log for this IO
name | string | yes | Name of Input/Output.
id | string | yes | Unique ID identifying the Input/Output in calaos-server


# xPLOutputSwitch
Light/relay controlled by xPL Protocol


Basic light. This light have only 2 states, ON or OFF. Can also be used to control simple relays output

## Parameters of xPLOutputSwitch
Name | Type | Mandatory | Description
---- | ---- | --------- | -----------
actuator | string | yes | Actuator ID, as set in your xPL network
logged | bool | no | If enabled, and if influxdb is enabled in local_config send the value to influxdb for this IO
io_type | string | yes | IO type, can be "input", "output", "inout"
gui_type | string | no | Internal graphical type for all calaos objects. Set automatically, read-only parameter.
enabled | bool | no | Enable the Input/Output. The default value is true. This parameter is added if it's not found in the configuration.
visible | bool | no | Display the Input/Output on all user interfaces if set. Default to true
source | string | yes | Source name, as set in your xPL network (Format VendorId-DeviceId.Instance)
log_history | bool | no | If enabled, write an entry in the history event log for this IO
name | string | yes | Name of Input/Output.
io_style | list | yes | GUI style display. This will control the icon displayed on the UI
id | string | yes | Unique ID identifying the Input/Output in calaos-server

## Conditions of xPLOutputSwitch
Name | Description
---- | -----------
false | Event when light is off 
 true | Event when light is on 
 changed | Event on any change of value 
 
## Actions of xPLOutputSwitch
Name | Description
---- | -----------
impulse 200 | Do an impulse on light state. Set to true for X ms then reset to false 
 set_state true | Update internal light state without starting real action. This is useful when having updating the light state from an external source. 
 impulse 500 200 500 200 | Do an impulse on light state with a pattern.<br>Ex: 500 200 500 200 means: TRUE for 500ms, FALSE for 200ms, TRUE for 500ms, FALSE for 200ms<br>Ex: 500 loop 200 300 means: TRUE for 500ms, then loop the next steps for infinite, FALSE for 200ms, TRUE for 300ms<br>Ex: 100 100 200 old means: blinks and then set to the old start state (before impulse starts) 
 toggle | Invert light state 
 set_state false | Update internal light state without starting real action. This is useful when having updating the light state from an external source. 
 false | Switch the light off 
 true | Switch the light on 
 

# ZibaseAnalogIn
Zibase analog input. This object can read value from devices like Energy monitor sensors, Lux sensors, ...


An analog input can be used to read analog values to display them and use them in rules.

## Parameters of ZibaseAnalogIn
Name | Type | Mandatory | Description
---- | ---- | --------- | -----------
zibase_sensor | list | yes | Type of sensor
zibase_id | string | yes | Zibase device ID (ABC)
port | int | no | Zibase ethernet port, default to 17100
precision | int | no | Precision of the returned value. The value represents the number of decimal after the dot. The value is rounded like this : value = 19.275 => returned value 19.28 when preicision = 2, 19.3 when precision = 1, 19 when precision = 0
name | string | yes | Name of Input/Output.
io_style | list | yes | GUI style display. This will control the icon displayed on the UI
log_history | bool | no | If enabled, write an entry in the history event log for this IO
visible | bool | no | Display the Input/Output on all user interfaces if set. Default to true
display_warning | bool | no | Display a warning if value has not been updated for a long time. Default to true
enabled | bool | no | Enable the Input/Output. The default value is true. This parameter is added if it's not found in the configuration.
unit | string | no | Unit which will be displayed on the UI as a suffix.
coeff_a | float | no | use in conjunction of coeff_b to apply equation of the form `value_displayed = coeff_a * raw_value + coeff_b`. Default value is 1.0.
io_type | string | yes | IO type, can be "input", "output", "inout"
logged | bool | no | If enabled, and if influxdb is enabled in local_config send the value to influxdb for this IO
coeff_b | float | no | use in conjunction of coeff_a to apply equation of the form `value_displayed = coeff_a * raw_value + coeff_b`. Default value is 0.0
host | string | yes | Zibase IP address on the network
offset | float | no | same as coeff_b, can be used alone. Default value is 0.0
id | string | yes | Unique ID identifying the Input/Output in calaos-server
gui_type | string | no | Internal graphical type for all calaos objects. Set automatically, read-only parameter.
period | float | no | Sampling time in microsecond. The value is read at this frequency. If this value is not set, calaos tries to read the interval parameter
interval | float | no | Sampling time in seconds. The value is read at this frequency. If this value is not set, the default value is 15s

## Conditions of ZibaseAnalogIn
Name | Description
---- | -----------
changed | Event on any change of value 
 value | Event on a specific value 
 

# ZibaseDigitalIn
Zibase digital input. This object acts as a switch


Basic switch with press/release states.

## Parameters of ZibaseDigitalIn
Name | Type | Mandatory | Description
---- | ---- | --------- | -----------
zibase_id2 | string | yes | Second Zibase device ID (ABC)
host | string | yes | Zibase IP address on the network
logged | bool | no | If enabled, and if influxdb is enabled in local_config send the value to influxdb for this IO
io_type | string | yes | IO type, can be "input", "output", "inout"
zibase_sensor | list | yes | Type of sensor
gui_type | string | no | Internal graphical type for all calaos objects. Set automatically, read-only parameter.
enabled | bool | no | Enable the Input/Output. The default value is true. This parameter is added if it's not found in the configuration.
zibase_id | string | yes | First Zibase device ID (ABC)
visible | bool | no | A switch can't be visible. Always false.
port | int | no | Zibase ethernet port, default to 17100
log_history | bool | no | If enabled, write an entry in the history event log for this IO
name | string | yes | Name of Input/Output.
id | string | yes | Unique ID identifying the Input/Output in calaos-server

## Conditions of ZibaseDigitalIn
Name | Description
---- | -----------
changed | Event on any change of state 
 false | Event triggered when switch is released 
 true | Event triggered when switch is pressed 
 

# ZibaseDigitalOut
Zibase digital output. This object controls Zibase devices


Basic light. This light have only 2 states, ON or OFF. Can also be used to control simple relays output

## Parameters of ZibaseDigitalOut
Name | Type | Mandatory | Description
---- | ---- | --------- | -----------
name | string | yes | Name of Input/Output.
io_style | list | yes | GUI style display. This will control the icon displayed on the UI
log_history | bool | no | If enabled, write an entry in the history event log for this IO
port | int | no | Zibase ethernet port, default to 17100
visible | bool | no | Display the Input/Output on all user interfaces if set. Default to true
nbburst | int | no | Number of burst to send to the device
enabled | bool | no | Enable the Input/Output. The default value is true. This parameter is added if it's not found in the configuration.
id | string | yes | Unique ID identifying the Input/Output in calaos-server
gui_type | string | no | Internal graphical type for all calaos objects. Set automatically, read-only parameter.
io_type | string | yes | IO type, can be "input", "output", "inout"
logged | bool | no | If enabled, and if influxdb is enabled in local_config send the value to influxdb for this IO
host | string | yes | Zibase IP address on the network
zibase_id | string | yes | Zibase device ID (ABC)
protocol | list | yes | Protocol to use

## Conditions of ZibaseDigitalOut
Name | Description
---- | -----------
false | Event when light is off 
 true | Event when light is on 
 changed | Event on any change of value 
 
## Actions of ZibaseDigitalOut
Name | Description
---- | -----------
impulse 200 | Do an impulse on light state. Set to true for X ms then reset to false 
 set_state true | Update internal light state without starting real action. This is useful when having updating the light state from an external source. 
 impulse 500 200 500 200 | Do an impulse on light state with a pattern.<br>Ex: 500 200 500 200 means: TRUE for 500ms, FALSE for 200ms, TRUE for 500ms, FALSE for 200ms<br>Ex: 500 loop 200 300 means: TRUE for 500ms, then loop the next steps for infinite, FALSE for 200ms, TRUE for 300ms<br>Ex: 100 100 200 old means: blinks and then set to the old start state (before impulse starts) 
 toggle | Invert light state 
 set_state false | Update internal light state without starting real action. This is useful when having updating the light state from an external source. 
 false | Switch the light off 
 true | Switch the light on 
 

# ZibaseTemp
Zibase temperature sensor


Temperature sensor input. Use for displaying temperature and to control heating devices with rules based on temperature value

## Parameters of ZibaseTemp
Name | Type | Mandatory | Description
---- | ---- | --------- | -----------
zibase_sensor | list | yes | Type of sensor
zibase_id | string | yes | Zibase device ID (ABC)
port | int | no | Zibase ethernet port, default to 17100
precision | int | no | Precision of the returned value. The value represents the number of decimal after the dot. The value is rounded like this : value = 19.275 => returned value 19.28 when preicision = 2, 19.3 when precision = 1, 19 when precision = 0
name | string | yes | Name of Input/Output.
log_history | bool | no | If enabled, write an entry in the history event log for this IO
visible | bool | no | Display the Input/Output on all user interfaces if set. Default to true
display_warning | bool | no | Display a warning if value has not been updated for a long time. Default to true
enabled | bool | no | Enable the Input/Output. The default value is true. This parameter is added if it's not found in the configuration.
coeff_a | float | no | use in conjunction of coeff_b to apply equation of the form `value_displayed = coeff_a * raw_value + coeff_b`. Default value is 1.0.
io_type | string | yes | IO type, can be "input", "output", "inout"
logged | bool | no | If enabled, and if influxdb is enabled in local_config send the value to influxdb for this IO
coeff_b | float | no | use in conjunction of coeff_a to apply equation of the form `value_displayed = coeff_a * raw_value + coeff_b`. Default value is 0.0
host | string | yes | Zibase IP address on the network
offset | float | no | same as coeff_b, can be used alone. Default value is 0.0
id | string | yes | Unique ID identifying the Input/Output in calaos-server
gui_type | string | no | Internal graphical type for all calaos objects. Set automatically, read-only parameter.
period | float | no | Sampling time in microsecond. The value is read at this frequency. If this value is not set, calaos tries to read the interval parameter
interval | float | no | Sampling time in seconds. The value is read at this frequency. If this value is not set, the default value is 15s

## Conditions of ZibaseTemp
Name | Description
---- | -----------
changed | Event on any change of temperature value 
 value | Event on a temperature value in degree Celsius 
 
