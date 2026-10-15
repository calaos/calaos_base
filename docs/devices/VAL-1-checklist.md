# VAL-1 — Checklist de recette sur la box (canal pre-release de calaos-os)

Paquets : calaos-server, calaos-home, calaos-webapp, calaos-remoteui. Le mobile iPhone vient plus tard.
Noter pour chaque point : OK, KO (avec capture et extrait de journal), ou non testé.

## 0. Avant de commencer
- [ ] calaos-server contient CLIM-7 (journal de démarrage, ou `get_home` d'un thermostat sans sonde : `fault` vaut `sensor_waiting`).
- [ ] Sauvegarde de `io.xml`, `rules.xml` et `iostates.cache`.

## 1. Installer
- [ ] « Device from a template... » : les 5 templates apparaissent, les textes sont en français.
- [ ] Création d'un thermostat chauffage seul sur un relais Wago et une sonde existante ; le device et ses IOs apparaissent dans l'arbre.
- [ ] Choisir comme sortie un relais déjà pris par un autre thermostat est refusé, avec le nom du propriétaire.
- [ ] Double-clic sur le device : changer la sonde et l'hystérésis, sauvegarder, rouvrir : les valeurs sont gardées.
- [ ] OK sans rien changer : le projet n'est pas marqué modifié.
- [ ] Envoi de la config au serveur, puis redémarrage : pas d'erreur `device` dans le journal.
- [ ] Règle avec une action « mettre le thermostat à une valeur » : le formulaire propose mode et consigne, la règle s'enregistre et se relit à l'identique.
- [ ] Le popup d'action reprend sa taille normale en mode texte.

## 2. Thermostat sur relais
- [ ] Mode chauffage, consigne au-dessus de la pièce : le relais passe ON (après `min_off_time` au démarrage).
- [ ] Consigne sous la pièce : le relais coupe dès la consigne franchie.
- [ ] Mode off : coupure immédiate, même pendant `min_on_time`.
- [ ] Une règle qui force le relais ON pendant le mode off : le journal le signale en erreur, et le relais n'oscille pas en continu.
- [ ] Sonde débranchée ou coupée : sorties OFF, statut « Sonde déconnectée » (ou « Sonde muette » après `sensor_timeout`), le thermostat reste réglable.
- [ ] Redémarrage du serveur : « En attente de la sonde » jusqu'à la première lecture, puis régulation normale.

## 3. Clim ESPHome (si un module est disponible)
- [ ] Création depuis le template ESPHome ; mode, consigne, ventilation et oscillation pilotables.
- [ ] Changement fait depuis la télécommande ou ESPHome : il remonte dans toutes les UI.
- [ ] Module débranché : « Hors ligne » partout, puis retour à la reconnexion.
- [ ] Température relayée (option) : arrive sur le module, s'arrête si la sonde source se tait.

## 4. Web app
- [ ] Rangée climate dans la pièce : consigne, pièce, statut coloré, chevron.
- [ ] Panneau de réglage : cadran, « − 22° + », pastille de mode, « Plus de réglages ».
- [ ] Plusieurs appuis rapides sur + : la valeur finale est la bonne, rien ne revient en arrière.
- [ ] Changement fait ailleurs pendant qu'un réglage est ouvert : la valeur se met à jour.

## 5. calaos-home (desktop)
- [ ] Item sur 2 rangées, textes en français.
- [ ] Dialogue de réglage, choix du mode et « Plus de réglages » empilés.
- [ ] « Démarre dans N min » pendant l'attente d'un relais.

## 6. Panneau mural (calaos-remoteui)
- [ ] Widgets 1×1, 2×1, 1×2 : pièce, consigne, statut ; un tap ouvre la vue plein écran.
- [ ] Vue plein écran : réglage de la consigne, popups de mode et de réglages.
- [ ] Widget 2×2 ou plus : climate complet, titre en bas.
- [ ] Reconfiguration depuis l'installer avec un popup ouvert : pas de plantage.

## 7. MCP (si utilisé)
- [ ] L'assistant voit le thermostat et peut changer son mode et sa consigne.
- [ ] Les relais du thermostat ne sont ni listés ni écrivables.
