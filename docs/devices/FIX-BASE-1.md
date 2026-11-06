# FIX-BASE-1 — Singletons détruits avant ListeRoom à la sortie (calaos_base)

## Objectif
Supprimer la corruption de tas (« corrupted double-linked list ») qui faisait avorter
`core/IncomingLogStockLevel_test` à la sortie en build de couverture, et retirer ce test de la
liste d'échecs tolérés du job « Diff coverage ».

## Livré
PR calaos/calaos_base#181.
- Cause : les singletons sont des statiques locales, détruites dans l'ordre inverse de leur
  construction. `~ListeRoom` détruit les pièces restantes, et `~Room` atteint encore `ListeRule`
  (`detachIOFromRules`), `EventManager` (`create(EventIODeleted)`) et la boucle uvw par défaut
  (`Idler::singleIdler` → `uvw::Loop::getDefault()`). Seul `main()` garantissait en partie l'ordre.
- Correctif : `ListeRoom::ListeRoom()` construit d'abord `uvw::Loop::getDefault()`, puis
  `EventManager`, puis `ListeRule`, qui lui survivent donc quel que soit l'ordre d'accès.
- Test `core/ListeRoomTeardownOrder_test` : enfant forké qui vide la boucle, sondes de destruction
  écrivant dans un pipe, délai de 30 s puis SIGKILL.
- La liste d'échecs tolérés du job de couverture est retirée.
- Impact : `calaos_server` portait un comportement indéfini latent (son premier `getDefault()`
  vient de `RemoteUIManager`, après `ListeRoom`), sans crash observé car des handles ouverts
  gardaient la boucle en vie.

## Mesuré
Avant : heap-use-after-free ASan dans `ListeRule::Remove` puis dans `uvw::Loop::getDefault`, à la
sortie. Après : ASan propre, `make check` vert (177 PASS, 3 SKIP), job de couverture sans aucun
échec toléré. Revue en deux tours (troisième occurrence, la boucle uvw, trouvée et corrigée).

## Contre-mutation
| Mutant | Résultat (`ListeRoomTeardownOrder_test`) |
|---|---|
| CM0 | 0 rouge |
| sans `ListeRule` / sans `EventManager` | rouge (ordre ; ASan) |
| sans les trois (= master) | rouge (enfant mort ; ASan) |
| appels après la statique dans `Instance()` | rouge |
| appels déplacés dans `~ListeRoom` | rouge |
| sans `getDefault()` dans le constructeur | rouge (enfant mort 3/3 ; ASan) |

## Reste ouvert
- `AVRManager` (`~IOAVReceiver` → `Delete()`) a le même défaut théorique, sans accès mémoire réel :
  à forcer le jour où un test laisse un `IOAVReceiver` vivant à la sortie.
- `SidecarLoopExitStatus_test` échoue sous ASan sur master (LD_PRELOAD), sans lien.
