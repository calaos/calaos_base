# CORE-2 — Classe `Device` : XML et propriété (calaos_base)

## Objectif
`<calaos:device>` dans une pièce, IOs possédés, liens, params, sauvegarde imbriquée et suppression
en cascade (spec §3.1, §3.2, §4.1, §4.2).

## Livré
PR calaos/calaos_base#183.
- `Device` (`IO/Device.{h,cpp}`), clé de fabrique `device:<impl>` (`REGISTER_DEVICE`) pour qu'une
  impl ne masque jamais un type d'IO ; `set_param`/`del_param` refusent d'ajouter ou de retirer
  `impl` sur un IO chargeable. Aucun io.xml connu ne porte `impl` (52 fichiers vérifiés en revue).
- IOs possédés créés par `IOFactory`, insérés juste après le device dans la liste plate de la pièce,
  marqués par `IOBase::getOwnerDevice()` ; liens et params dans l'ordre du fichier ;
  `ListeRoom::resolveDeviceLinks()` après toutes les pièces et avant les règles ; lien non résolu
  journalisé et conservé ; le device ne stocke que des ids.
- Sauvegarde imbriquée stable ; un IO possédé orphelin est écrit au niveau de la pièce.
- Un `<calaos:device>` impossible à instancier (impl inconnue ou absente) est **conservé inerte** :
  sous-arbre XML réécrit à l'identique, à sa place (pas de perte sur un retour de version). Un
  device imbriqué n'est pas construit.
- Tout IO construit puis refusé au chargement passe par `ListeRoom::discardUnattachedIO()`, qui le
  retire de la boucle d'événements des règles avant sa destruction (InputTime, InPlageHoraire,
  InputAnalog s'y inscrivent dès leur constructeur) ; même correctif pour un doublon refusé par
  `createIO()`.
- Cascade : `deleteIO(device)` supprime chaque IO possédé (recherché par propriétaire, robuste aux
  ids dupliqués) via `deleteIO()` ; `delete_io(del=true)` refusé pendant la marche des règles.
- Doc : `docs/01_core_data_model.md`, `docs/10_events_notifications.md`.

## Mesuré
`make check` 181 tests (178 PASS, 3 SKIP) ; ASan propre (Device 16, IOChangeHub 16,
ListeRoomTeardownOrder, ListeRoomRobustness, ConfigRoundTrip) ; porte de couverture : 138 lignes
couvertes, 0 non couverte. Revue en deux tours : bloquant (use-after-free sur les refus au
chargement, crash au boot avec un io.xml édité à la main) corrigé et vérifié par les scénarios du
relecteur.

## Contre-mutation
20 mutants tués, dont : la pièce ne saute plus les IOs possédés (5 rouges), pas de cascade,
`resolveDeviceLinks()` non appelé, device imbriqué accepté, `impl` non structurel, propriétaire non
marqué (7), clé de fabrique par `type` (SEGV), `discardUnattachedIO` vide (SEGV ×2), `~Device` sans
retrait de la boucle (SEGV), pas de conservation opaque.

## Reste ouvert
- CORE-3 : compter les devices opaques dans « des devices disparaîtraient » ; réserver les ids des
  IOs contenus dans un device opaque.
- DOC-1 : `genDocIO()` devra filtrer les clés `device:<impl>`.
- Un device opaque glisse après le dernier IO quand ceux qui le précédaient disparaissent.
