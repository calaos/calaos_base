Le module IO/zibase contient les modules permettant d'appeler la couche bas niveau lib/zibase, qui permet la communication avec la zibase

pour le moment sont gérés:
-les inputs analogiques (températures, moniteur d'énergie) via le module ZibaseAnalogIn.




ZibaseAnalogIn: 

les champs nécessaire pour le module zibase, a remplir dans le fichier io.xml sont les suivants:

host="@ip de la zibase"
name="chambre ...."
Zibase_id= id radio de la sonde, visible sur le configurateur de la zibase (lors de l'ajout d'une nouvelle sonde)
port= port sur lequel la zibase est en écoute, mettre 49999.
Zibase_sensor= type de sonde (pour le moment, seul 2 types, "temp" pour une sonde de températuer, "energy" pour une sonde de consommation électrique
type=ZibaseAnalogIn 

les autres champs sont les champs utiles pour Calaos, mais ne sont pas utilisés par le module zibase.

Un exemple d'entrée analogique pour une sonde de température:
</calaos:room>
<calaos:room name="Chambre" type="chambre" hits="0">
    <calaos:input chauffage_id="1" gui_type="temp" logged="true" host="xx.xx.xx.xx" id="input_1" name="Température Chambre Maxime" Zibase_id="OS439213057" port="49999" Zibase_sensor ="temp" time="60" type="ZibaseAnalogIn" var="0" visible="true" />
</calaos:room>

Un exemple d'entrée analogique pour une sonde de consommation electrique:
</calaos:room>
        <calaos:room name="Conso" type="divers" hits="0">
	<calaos:input  gui_type="analog_in" host="xx.xx.xx.xx" id="input_4" name="Conso Maison" port="49999"  type="ZibaseAnalogIn" Zibase_id="WS132935" unit="W" var="0" visible="true"  Zibase_sensor ="energy" />
        </calaos:room>

