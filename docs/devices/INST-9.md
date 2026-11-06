# INST-9 — Libellés et types d'IODoc dans l'installer (calaos_installer)

## Objectif
Revue d'INST-7 : libellés anglais dérivés des ids, un relais proposé pour target_temp_low, emitter en texte libre.

## Livré
PR calaos/calaos_installer#77.
- data/doc régénéré depuis le run master calaos_base de #205 (labels, value_type, emitter en liste, textes fr) : iodoc-sync de nouveau vert.
- `label` d'IODoc, repli sur le nom dérivé de l'id (comme INST-7), pour les rôles et params : éditeur (id en infobulle) et assistant (« Lien : libellé (id) », « Paramètre : libellé (id) », l'id seulement s'il diffère).
- Choix d'IO filtrés par `value_type` (IODoc, puis templates, puis IO lié) en plus des filtres existants, à la création et à la validation ; absent ou inconnu = permissif. Un lien existant vers un IO d'un autre type est affiché « d'un autre type » et conservé.
- `emitter` : combo dans l'éditeur par la gestion générique des listes.
- IODoc.cpp dans calaos_model.pri.

## Mesuré
tst_devicetemplates 107, tst_deviceedit 34, tst_dialognewdevice 14 ; make check vert ; CI 5/5 dont iodoc-sync ; captures en/fr. Revue opus : merge, ses remarques traitées (test de précédence, repli dérivé, nettoyage).

## Contre-mutation
CM0 : 0 rouge. 15 mutants, 14 rouges (value_type IODoc, types de rôle dans fits et validate, label→id, repli, type inconnu, params de l'assistant, list→combo, infobulle, hint, IO lié avant IODoc, nom non dérivé, sans majuscule). « Template avant IODoc » équivalent aujourd'hui (mêmes types des deux côtés).

## Reste ouvert
- Tests écrits après le code (preuve par mutation).
- fr.ts sans lupdate complet (il réécrit ~1500 lignes sans rapport) : à faire à part.
- Textes de, es, hi, nb, pl, ru absents (DOC-6).
