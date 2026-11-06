# FIX-INST-8 — Ne plus créer de types retirés du serveur (calaos_installer)

## Objectif
L'installateur ne propose plus de créer un type d'IO que le serveur n'enregistre plus, mais continue
de lire et réécrire les fichiers existants.

## Livré
PR calaos/calaos_installer#64.
- Création retirée : MySensors (13 types ; serveur ea31a024, T2.12), Gadspot (47badd28, T3.6), xPL
  (7), Zibase (4) et X10Output (7a1f47ee), « Web > Switch Input » (créait en réalité un
  WebOutputLight) et « Web > Shutter » (code mort) — ces deux-là n'ont jamais existé côté serveur.
- Retirés avec eux : DialogDetectMySensors, DialogDetectxPL, MySensors.cpp/h, le mécanisme
  d'entry-helper et `WidgetIOProperties::setValues` ; images et entrées qrc mortes.
- Les 27 types retirés restent lisibles (`REGISTER_LEGACY_*`) : un projet existant est réécrit à
  l'octet près ; l'arbre les marque (icône inconnue, grisé, « Not supported by the server »).
- Chemin d'icône `icon_unknown.png` (inexistant) corrigé partout ; dialogue caméra rouvert sur
  Foscam.
- `tst_servertypes` (35 cas) : types créables ⊆ types du serveur (liste tirée de calaos_base
  7f27608b, 85 types), littéraux de type des sources, sous-dossiers compris.

## Mesuré
11 suites vertes ; CI 4/4. Revue en deux tours : chaque famille retirée vérifiée contre l'historique
du serveur, aller-retour octet à octet d'un projet aux 27 types legacy, chaque index du combo Web
vérifié.

## Contre-mutation
5 mutants tués (type legacy ré-enregistré créable, littéral dans un menu, enregistrement legacy
supprimé, marqueur neutralisé, ancien chemin d'icône).

## Reste ouvert
Les listes de règles ne signalent pas un IO d'un type retiré ; chaînes MySensors obsolètes dans les
`.ts` jusqu'au prochain lupdate.
