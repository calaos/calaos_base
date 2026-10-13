# CLIM-3 — `InternalThermostat` : régulation (calaos_base)

## Objectif
`impl="thermostat"` (spec §8.1) : Calaos pilote output_heat/output_cool depuis current_temp et les consignes.

## Livré
PR calaos/calaos_base#192.
- Modes off + heat (output_heat) + cool (output_cool) + auto (les deux). Mode stocké dans `iostates.cache` quand il change. Un mode sauvegardé indisponible repasse à off, journalisé et sauvegardé.
- `evaluate()` : hystérésis ; entre les seuils, état de la dernière commande ; auto contre low/high ; consigne sans valeur → OFF. Évalué sur current_temp, les consignes, les sorties (1 ms après), les verrous, le premier tour de boucle, et une fois par commande entière.
- min_on/min_off en secondes décimales, `unique_ptr<Timer>` réarmable par sortie. Une sortie ne passe ON qu'après le min_off de l'autre.
- Jamais les deux : arrêt sans verrou en mode off, sur refus de sûreté ou avec deux sorties ON. Si l'autre sortie est tenue ON de l'extérieur sans pouvoir encore être remise, la sienne s'arrête.
- Une sortie changée par autre chose compte comme commutée (horodatée). La remettre passe par ses verrous.
- Les remises sont espacées de put_back_delay (1 s, au moins 1 ms), doublé tant que chaque remise est défaite, jusqu'à put_back_delay_max (60 s, pas moins que put_back_delay). L'espacement revient à zéro au calme ; un avertissement est journalisé une fois par sortie. Une valeur refusée est journalisée et remplacée. Une écriture refusée ne change rien.
- Décision 17 : une sortie appartient au premier thermostat chargé. Le suivant journalise les deux ids et laisse le lien de côté (pas de capacité, aucune écriture). Le même IO en heat et en cool garde la chauffe.
- Hook `outputsAllowed()` par sortie ; sans valeur de current_temp, tout reste OFF quoi que dise une surcharge. Un id en double ne régule ni ne réclame rien.
- `Device::createOwnedLink()` recrée target_temp/action (+ low/high). Suppression : sorties laissées en l'état.

## Mesuré
make check 190 (187 PASS, 3 SKIP) ; 44 cas ; ASan propre ; couverture : 0 ligne non couverte ; CI verte.
Combat règle/sortie : plus de 1000 écritures/s avant, 14 puis 2 par minute simulée après. put_back_delay=0 : 546 écritures en 300 ms avant, 6 au plus après. Chaque défaut de revue a un `test:` rouge avant son `fix:`.

## Contre-mutation
CM0 : 0 rouge (normal et ASan). 50 mutants tués sur 52.
- Comme au tour précédent : hystérésis, auto, jamais les deux, verrous, bascule, réentrance, règle capteur, recréation, masquage, persistance, combat (adoption, horodatage, plancher, doublement, remise à zéro, avertissement unique), propriété des sorties (×4), ASan (×4).
- Garde « la sienne cède » retirée → NeverBothOnWhileSomethingElse…
- Pas de minimum de 1 ms → PutBackDelaysBelowOneMillisecondAreRefused.
- Maximum plus court accepté → même test.

Survivants, équivalents :
- transition calculée sur `isOn` : ne touche que la ré-affirmation d'une relecture en retard ;
- timer de sortie à 0 au lieu de 1 ms : le plancher borne le combat dans les deux cas (mesuré à nouveau avec put_back_delay 1 ms).

## Reste ouvert
- CLIM-4 : étendre `outputsAllowed()` ; verrou au boot.
- CLIM-5 : journal plus bruyant pendant un OFF forcé défait par une règle.
- Spec §14 : défauts des IOs recréés et des params (h 0,5, min 0, put_back 1/60 s).
- DOC-1 : `device:thermostat` sans description dans io_doc.json.
