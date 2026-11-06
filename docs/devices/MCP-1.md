# MCP-1 — Devices dans le MCP (calaos_base)

## Objectif
Spec §9.2 et §8.3, décision 18 : le sidecar MCP pilote les devices (mode simple ou commande JSON), masque de `list_ios` les sorties protégées d'un thermostat interne et les refuse en écriture ; la description de `set_io_state` est générée depuis IODoc.

## Livré
PR calaos/calaos_base#200.
- Sorties protégées = `device.ios[output_heat|output_cool]` des devices d'impl `thermostat`, lues dans `get_home`. L'impl est comparée sans casse ni espaces (IOFactory la met en minuscules pour construire le device). Masquées de `list_ios`, `find_io`, `get_room` et du `io_count` de `list_rooms` ; refusées par `set_io_state` (message : thermostat, rôle, « commande le thermostat »). L'écriture relit `get_home(force=True)`.
- Un device est un IO inscriptible : mode simple ou JSON compact (`allow_nan=False`). Refusés : JSON invalide ou vide, NaN, valeur imbriquée, champ absent des capabilities, valeur d'énumération hors liste, champ inconnu d'IODoc, champ numérique qui n'est pas un nombre fini (« 21.5 » en chaîne, booléen, 1e999), champ énuméré qui n'est pas une chaîne. Les bornes restent au serveur.
- Les outils de lecture (`list_ios`, `find_io`, `get_room`, `get_io_state`) renvoient l'objet `device` (family, impl, capabilities, state, ios), chaînes sanitisées.
- Description de `set_io_state` rendue depuis `io_doc.json` (`iodoc.py`). Le fichier est écrit au build par `calaos_server --gendoc` (LC_ALL=C, timeout 120 s), installé dans `share/calaos/`, chemin exporté par le wrapper (`CALAOS_IODOC_PATH`). Sans fichier, gendoc en échec ou fichier mal formé : l'impl est ignorée avec un avertissement et la description courte renvoie aux capabilities ; le sidecar démarre.
- Cache get_home : les événements arrivent en `msg=event`, `data.type_str` (l'ancien code testait un type inexistant). Invalidation sur `io_added`, `io_deleted`, `room_changed` ; `io_changed` appliqué (état, `device` remplacé en entier) ; cache vidé à chaque déconnexion.
- Docs : docs/15_mcp_server.md (« Devices dans le MCP »), docs/01_core_data_model.md.

## Mesuré
pytest 138 cas (+77) ; run-python-tests 105/105 avec CALAOS_PYTHON_TESTS_REQUIRED=1 ; CI verte. Revue : contournement par la casse de `impl`, cache non vidé à la reconnexion et nombres en chaîne corrigés, chacun prouvé par un test rouge.

## Contre-mutation
CM0 : 0 rouge. 33 mutants : 32 tués, 1 survivant équivalent.
- Masquage : output_cool non protégé (4), impl quelconque protégée (9), masquage retiré (4), find_io/get_room sans masque (1 chacun), impl comparée exactement (TheImplIsComparedTheWayTheFactoryReadsIt, 3), io_count qui compte les masquées.
- Refus : écriture non refusée (4), écriture sur le cache (TheRefusalDoesNotDependOnACachedTopology).
- Commande : NaN accepté, capacité non exigée, énumération non vérifiée, mode simple non vérifié, champs IODoc non vérifiés, JSON non compacté, nombre en chaîne accepté (5), infini accepté (4), énumération non-chaîne acceptée (2).
- Device : objet jamais renvoyé (3), non sanitisé (1).
- Cache : device non remplacé sur io_changed, événement de topologie sans invalidation (4), room_changed ignoré, cache gardé à la déconnexion (TheDisconnectDropsTheCachedHome).
- Description : bornes fausses (3), value_requires_links perdu, impl non JSON documentée, description jamais regénérée, phrase des sorties protégées retirée (2), IODoc lu une fois pour toutes, exemple commençant par off, champs mal formés documentés (4).
Survivant équivalent : `allow_nan=False` retiré (le test `isfinite` le précède). Non couvert : la règle Makefile.am et l'export du wrapper (gardés par le test sur le vrai io_doc.json).

## Reste ouvert
- Build croisé : `calaos_server --gendoc` n'y tourne pas, la box reçoit `{}` et la description courte ; générer côté hôte ou via DOC-3.
- io_doc.json en anglais seulement.
- Un champ rejeté par le serveur reste journalisé sans retour au modèle.
- Le test sur le vrai `io_doc.json` échoue en pytest isolé sans build préalable.
- Sans `io_doc.json`, un champ inconnu (`{"anything":1}`) est encore envoyé au serveur (refusé quand le fichier existe).
