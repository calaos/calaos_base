
IO/Zibase module allow communication with zibase domotic box from zodianet, through zapi API.

For now, only the following sensor are managed:

-Analog input through ZibaseAnalogIn.cpp (tested on OWL CM119 energy monitor and temperature sensor Oregon Scientific)
-Digital input switch through ZibaseDigitalIn (tested on fibaro Door/window sensor)


---------------------------ZibaseAnalogIn: -----------------------------------------------------------------
Mandatory fields for ZibaseAnalogIn module, are the following (to be filled in io.xml calaos conf file)

host="zibase @ip"
name="label to be displayed by calaos home"
zibase_id= radio id of the sensor, this is the id writen in the zibase configurator (when new sensor is added)

port= local oprt to use for the udp connection (works with 17100)
zibase_sensor= sensor type 
        "temp" for temperature sensor
        "energy" for energy monitoring sensor
        
        
type=ZibaseAnalogIn (say to calaos that we use the module ZibaseAnalogIn for this input)

other fields are used by calaos but not used by zibase module.

Temperature sensor definition example:
<calaos:room name="Chambre" type="chambre" hits="0">
            <calaos:input chauffage_id="1" gui_type="temp" logged="true" host="XX.XX.XX.XX" id="input_1" name="Température Chambre" zibase_id="ABCDEFG" port="17100" zibase_sensor ="temp" time="60" type="ZibaseAnalogIn" var="0" visible="true" />
            </calaos:room>

Energy monitor sensor definition example:
</calaos:room>
        <calaos:room name="Conso" type="divers" hits="0">
	<calaos:input  gui_type="analog_in" host="XX.XX.XX.XX" id="input_4" name="Conso Maison" port="17100"  type="ZibaseAnalogIn" zibase_id="WSABCDEFG" unit="W" var="0" visible="true"  zibase_sensor ="energy" />
        </calaos:room>

---------------------------ZibaseAnalogIn: -----------------------------------------------------------------
Mandatory fields for ZibaseDigitalIn module, are the following (to be filled in io.xml calaos conf file)

host="zibase @ip"
name="label to be displayed by calaos home"
zibase_id= radio id of the sensor, this is the id writen in the zibase configurator (when new sensor is added)

port= local oprt to use for the udp connection (works with 17100)
zibase_sensor= sensor type 
        "detect" for door/window sensor
        
type=ZibaseDigitalIn (say to calaos that we use the module ZibaseDigital In for this input)

other fields are used by calaos but not used by zibase module.

Door/window detector sensor definition example:
      <calaos:room name="garage" type="garage" hits="0">
	<calaos:input gui_type="switch" id="input_30" name="Detecteur garage" time="1" type="ZibaseDigitalIn" var="0" visible="true" host="XX.XX.XX.XX" zibase_id="ZA4" port="17100" zibase_sensor ="detect" />
        </calaos:room>


