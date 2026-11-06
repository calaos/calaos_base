# CORE-3 — Format 2 et refus bruyant (calaos_base)

## Objectif
`format="2"` écrit dès qu'un device existe, `io_format` annoncé par `config get`, `config put`
refusé quand un ancien installateur ferait disparaître des devices (spec §3.3, décision 16) ; ids
des IOs d'un device opaque réservés.

## Livré
PR calaos/calaos_base#184.
- `SaveConfigIO()` écrit `<calaos:home format="2">` dès qu'un device existe, opaques compris ;
  sinon sortie inchangée.
- `config get` : `"io_format": 2`.
- `config put` : le client déclare `io_format` (entier 64 bits ou chaîne entièrement décimale,
  borné à 1..1000 ; tout le reste vaut 1). Si le serveur a au moins un device et que la requête ne
  déclare pas `io_format >= 2` : refus total avant `BackupFiles()` (aucune sauvegarde, écriture ni
  redémarrage), `error: "update calaos_installer: this io.xml would drop N device(s)"`. Un client qui
  déclare le format 2 est cru (supprimer le dernier device compris). Avec des devices, un `io.xml`
  illisible est refusé avec `io.xml does not parse` ; sans device, rien ne change.
- `config` ajouté aux commandes interdites au scope service (MCP).
- `ListeRoom::isIdReserved()` : ids d'un device opaque et de ses IOs refusés par `get_new_id()` et
  `createIO()` ; `ListeRoom::deviceCount()` compte chaque device.
- Harnais : `HttpClient::needsRestart()`. Docs : 08_http_api.md, 01_core_data_model.md, 15.

## Mesuré
`make check` 183 tests (180 PASS, 3 SKIP) ; DeviceFormat_test ; porte de couverture : 0 ligne non
couverte ; CI verte. Revue : refus avant tout effet de bord, seule voie HTTP admin, aucune régression
pour les installateurs actuels.

## Contre-mutation
Rejouée sur le code final : 19 mutants tués. Un survivant (entier signé tronqué : `-4294967294`
donnait 2) a été tué par un cas de test ajouté. Mutants : format toujours écrit, opaques non
comptés, comptage façon set, refus supprimé, refus qui redémarre, déclaration ignorée (entière ou
chaîne), analyse retirée, toute déclaration crue, chaîne partiellement numérique, troncatures signée
et non signée, message d'erreur confondu, `config` hors liste MCP, borne retirée, réservations
retirées (3), `io_format` absent du `get`.

## Reste ouvert
INST-3 : envoyer `io_format: 2` dans chaque `put`, lire `io_format` du `get`, afficher `error`.
