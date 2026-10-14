# CLIM-5 — Sorties protégées (calaos_base)

## Objectif
Spec §8.3 : écrire `output_heat`/`output_cool` d'un thermostat depuis une règle, Lua ou `set_state` est journalisé, jamais bloqué ; un OFF forcé qu'une règle défait laisse une trace à chaque remise défaite.

## Livré
PR calaos/calaos_base#196.
- `Device::writtenFromOutside(io, source)` / `observeOutsideWrites()` : appelé par `ActionStd` et par `decodeSetState` (3 appelants : HTTP, WebSocket, Lua ; la source est un paramètre obligatoire). `InternalThermostat` s'y abonne et journalise un avertissement qui nomme le thermostat, la sortie et la source (`a rule`, `a Lua script`, `set_state over HTTP`, `set_state over WebSocket`, `set_state from a RemoteUI panel` (virtuel `JsonApiHandlerWS::setStateSource()`, surchargé par `RemoteUIWebSocketHandler`)). La valeur est appliquée, le thermostat reprend la main selon ses verrous.
- Rien pour : un set_state refusé avant écriture, un IO qui n'est pas une sortie, le device lui-même, un doublon d'id masqué, un thermostat supprimé. Une sortie écartée (décision 17) nomme le thermostat qui la tient.
- Limite : une ligne par source et par sortie pendant `put_back_delay_max` (60 s). Pas de nouveau paramètre. La règle n'est pas nommée (ActionStd ne connaît pas la sienne).
- Le registre des thermostats vivants est alloué sur le tas et jamais libéré : le premier thermostat est construit après ListeRoom, une statique locale serait détruite avant lui alors que `~ListeRoom` détruit encore les thermostats restés dans leur pièce (trouvé en revue).
- OFF forcé (mode off, refus de `outputsAllowed()`) défait : une erreur à chaque remise défaite (`mode off` ou `safety`), au plus une par espacement puisque les remises sont espacées de `put_back_delay` au moins. L'avertissement unique par sortie reste pour le premier rallumage ; une remise défaite hors OFF forcé n'est pas une erreur.
- MCP : le sidecar expose l'écriture directe (`set_io_state`). Laissé à MCP-1 (masquer de `list_ios`, refuser en écriture) ; `get_home` donne `device.ios` pour identifier les sorties.
- docs/01_core_data_model.md : sous-section « Sorties protégées ».

## Mesuré
make check 191 (188 PASS, 3 SKIP) ; InternalThermostat_test 54 cas (+10), ProtectedOutputWrites_test 5 cas (HTTP, WebSocket, fenêtre, valeurs non écrites, vrai calaos_script), ThermostatTeardownOrder_test 1 cas, RemoteUIDevice_test +1 ; ASan propre ; couverture différentielle : 126 lignes ajoutées sous src/, 0 non couverte ; CI verte. Le commit `test:` est rouge sur master (10 cas).

## Contre-mutation
CM0 : 0 rouge. 26 mutants tués sur 26.
- Observateur jamais appelé / hook ActionStd retiré / hook decodeSetState retiré → les cas d'avertissement de chaque chemin (7 / 3 / 4 rouges).
- Filtre `claimed` retiré → AShadowedThermostatProtectsNothing. Appartenance de la sortie toujours vraie → 7 rouges ; `linkIo` au lieu de `outputIo` → TheWarningNamesTheThermostatHoldingTheOutput.
- Limite retirée / fenêtre jamais expirée / clé sans la source / fenêtre = put_back_delay → TheWriteWarningIsLimitedPerSourceAndOutput, EachSourceIsWarnedOnceInTheWindow.
- Libellés HTTP/WS échangés, libellé Lua faux → SetStateOverHttp…, SetStateOverWebSocket…, ALuaScript… ; libellé de panneau codé en dur ou surcharge au libellé WS → SetStateFromAPanelIsLabelledAsOne
- Hook placé avant le refus de valeur → OnlyAWrittenOutputIsWarnedAbout.
- Erreur : sans `stopForced`, sans `undone`, `putBackWritten` jamais posé ou jamais remis à zéro, `stopForced` jamais posé, raison toujours « mode off » → AnUndonePutBackOutsideAForcedStopIsNotAnError, ARuleUndoingAForcedOffIsAnErrorAtEachPutBack, ARuleUndoingASafetyStopIsAnErrorToo, AnUndonePutBackIsOneErrorWhateverFollows.
- Avertissement unique retiré → ARuleFightingAnOutputIsBounded et 2 autres. Désinscription au destructeur retirée → plantage (ARuleFightingAnOutputIsBounded). Registre redevenu statique locale → ThermostatTeardownOrderTest sous ASan.

Survivants au premier passage, tués ensuite : fenêtre = put_back_delay (test renforcé), `putBackWritten` non remis à zéro (test ajouté). Aucun équivalent.

## Reste ouvert
- MCP-1 : masquer/refuser les sorties protégées dans le sidecar.
- DOC-1 : mentionner dans le texte IODoc de `put_back_delay_max` qu'il fixe aussi la fenêtre du journal des écritures extérieures.
- Source « règle » sans nom de règle ni de script ; autres écrivains (pilotes, scénarios) couverts seulement par l'avertissement « changée par autre chose ».
- Le test d'ordre de destruction n'a d'oracle que sous ASan (la CI ne le lance pas).
