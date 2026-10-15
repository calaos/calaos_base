# DOC-4 — Traduction française des templates d'équipements (calaos_base)

## Objectif
`fr/device_templates.json`, produit par `scripts/create_iodoc`, était identique à `en` : les textes de `DeviceTemplates.cpp` n'étaient dans aucun catalogue de `po/`.

## Livré
PR calaos/calaos_base#204.
- `DeviceTemplates.cpp` était déjà dans `po/POTFILES.in` et ses textes déjà marqués `_()` ; seuls `calaos.pot` et `fr.po` ne les avaient pas. 91 entrées ajoutées aux deux (emplacement = fichier, sans numéro de ligne) : xgettext sur ce seul fichier, msgmerge `--no-fuzzy-matching`, ajout en fin de fichier. Les autres catalogues ne sont pas régénérés (`make` relance msgmerge sur tous les .po dès que `calaos.pot` est plus récent qu'eux).
- Termes : consigne (basse, haute, minimale, maximale), sonde, plancher chauffant, pompe à chaleur, sortie chauffage / refroidissement, émetteur, ventilo-convecteurs, équipement, climat, pièce ; modes alignés sur les clients (Arrêt, Chauffage, Froid, Auto, Déshumidification, Ventilation). Gardés : ESPHome, MQTT, YAML, identifiants et code des notes.
- Test `DeviceTemplatesFrench.EveryTextOfEveryTemplateIsTranslated` : compile `po/fr.po`, lit avec `LANGUAGE=fr` sous `C.UTF-8`, parcourt `all()` en en et en fr champ par champ ; les textes doivent différer, garder leurs `{placeholders}` et leurs blocs de code ; le reste du document doit rester identique. Exceptions explicites (Auto, Horizontal, Modes, Vertical, `{name} action`, `{name} mode`), toutes obligatoirement rencontrées. Garde-fou : plus de 150 textes.
- CI : le contrôle fr ≠ en couvre `device_templates.json`.

## Mesuré
CI verte : Build & test (`make check` 207 cas, 206 PASS, 1 SKIP), Diff coverage, Format, MCP, Dependabot, Workflow gating ; étape « Generate the IO documentation » : 5 templates, fr ≠ en.

## Contre-mutation
CM0 : 0 rouge.

| Mutant | Rouge |
|---|---|
| fr.po : un libellé laissé en anglais (Sonde de température) | « …/label is not translated: Temperature sensor » |
| fr.po : msgstr vide (Pièce) | « Room is not translated » |
| fr.po : msgid modifié (Both absent) | « Both is not translated » |
| fr.po : `{name}` perdu | comparaison des placeholders |
| fr.po : bloc ``` injecté | comparaison des blocs de code |
| fr.po : « Modes » traduit | « an exception no longer applies » |
| code : `_()` retiré sur « Underfloor heating » | « …/values/2/label is not translated: Underfloor heating » |

## Reste ouvert
- calaos_installer : régénérer `data/doc` avec `./generate_iodoc.sh` après le run push de master (seul `fr/device_templates.json` change).
- de, es, hi, nb, pl, ru n'ont pas ces textes ; `calaos.pot` global reste à régénérer.
