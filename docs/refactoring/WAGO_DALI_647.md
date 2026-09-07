# Dossier de réflexion — la borne DALI 753-647 et l'unification du firmware

> ⚠️ **Ce document n'est pas un ticket et n'engage aucun travail.** Il consigne une réflexion menée
> avec l'auteur du firmware le **2026-09-07**, à sa demande expresse : *« c'est purement de la
> réflexion, surtout rien à acter »*, puis *« consigne tout ce qu'on a dit »*.
>
> Les tickets nés de la même session sont ailleurs et vivent leur vie :
> [`T3.156`](T3.156.md) (livré), [`T3.157`](T3.157.md), [`T3.160`](T3.160.md), [`T3.161`](T3.161.md),
> [`T3.162`](T3.162.md), [`T3.163`](T3.163.md), [`T3.164`](T3.164.md).

---

## 1. Les deux idées de départ

1. Pouvoir placer **n'importe quel module n'importe où** dans le rack.
2. **Un seul code** pour la borne DALI **753-647** et pour la **750-641**, qui fonctionne aussi
   quand la 647 est absente.

⛔ **Hors sujet, par décision de l'auteur** : faire converger le moteur de règles du mode dégradé et
celui de `calaos_server`. *« Elles ne sont pas du tout pareilles, et ça ne peut pas être fait. »*
Ce n'est pas un oubli, c'est une décision — ne pas la rouvrir.

## 2. Le point de départ : une contrainte mesurée sur banc

> « Sans la borne 750-647, **les premières bornes IO digitales ne sont pas utilisables**, car mappées
> sur la lib DALI via `DALI647_OutReal AT %QB0` ; la lib DALI doit faire quelque chose et écrire/lire
> dans cette zone, ça **invalide toutes nos écritures** sur cette zone. »

C'est ce constat qui a motivé l'avertissement du `README` de la 3.0 et le fichier-marqueur
`ne_pas_utiliser_v3_sauf_avec_borne_dali_master.txt`.

⭐ **Une lecture attentive du ST conduit à la conclusion INVERSE** — le garde
`IF Config.CONFIG_DALI_647 = TRUE THEN DaliPrg647` couvre le seul site d'appel, les variables sont
locales au programme, aucune tâche dédiée n'existe (`MainTask → PLC_PRG()`, `DMXTask → DMX()`).
**Le banc bat le code source**, et c'est la leçon de méthode de ce dossier.

## 3. L'architecture, et pourquoi elle est ainsi

- Les vieux **750-842** permettaient l'accès direct aux IO depuis Modbus. La **1.7** en profitait :
  `byOutArr AT %QB0`, miroir bidirectionnel pleine largeur.
- Les automates récents (**750-841, 849, 880**…) ne l'permettent plus — *« en tout cas le runtime ne
  le permet plus »*. D'où, dès la **1.8** : le serveur écrit `netOutStandard AT %IB512`
  (coil 4096 = mot 256), et le PLC recopie vers les sorties par `write_word()`.
- ⇒ **`%QB` appartient au PLC ; c'est Modbus qui n'y accède plus.** Les deux mondes sont distincts.
- Prérequis d'installation Calaos, acté en documentation : les IO **analogiques** toujours **après**
  les digitaux. ⚠️ Ce prérequis **ne couvre pas** la 753-647, que le code exige **avant tout**.

⭐ **Chronologie, qui corrobore tout le reste** : 1.7 = tout localisé, zéro `write_word` ·
1.8–2.3 = tout Mod_com, aucune variable localisée sur des octets physiques · **3.0 = le premier
mélange des deux sur des octets physiques**, et c'est exactement là qu'apparaît le symptôme.

## 4. ⭐⭐ Ce que l'interface du bloc a tranché

Définition extraite de CoDeSys par l'auteur :

```
FUNCTION_BLOCK FbMaster753_647
VAR_INPUT
    abIn_753_647  : ARRAY [0..23] OF BYTE;   (* par VALEUR *)
    bModule_753_647 : BYTE := 1;
    xQuit : BOOL;
END_VAR
VAR_OUTPUT
    abOut_753_647 : ARRAY [0..23] OF BYTE;   (* il PRODUIT la valeur *)
    bFeedback : BYTE;
END_VAR
```

**Ni l'un ni l'autre n'est un `VAR_IN_OUT`.** Le bloc ne détient **jamais** de référence sur le
tableau et ne connaît **jamais** son adresse : il reçoit une copie, il rend une copie. Tout son
intérieur (`RxBufPrio*`, `TxBufPrio*`, `MBX2_RX_DEFRAG`, `MBX2_TX_FRAG`, `pt`) travaille sur ses
propres variables.

⇒ **Il lui est structurellement impossible de toucher `%QB0..23`.**

## 5. Les deux conclusions qui en découlent

1. **La bibliothèque n'exige pas de variable localisée.** Le `AT %IB0` de la documentation Wago est
   un **exemple**, pas une contrainte de type. ✅ **Confirmé par l'essai E0** (voir §8) : *« ça
   compile si je mets un tableau sans le `AT %QB0` »*.
2. **Le symptôme du banc ne vient donc pas de la bibliothèque.** Par élimination, il vient de la
   **déclaration** : la variable localisée marque `%QB0..23` comme possédés par l'image IEC, et le
   rafraîchissement de cette image écrase à chaque cycle ce que `write_word` (accès **indirect** à
   l'image, doc Wago) y a écrit. ⚠️ **Déduit, pas mesuré** — c'est E1 qui le tranchera.

## 6. La piste principale

**Aucune variable localisée.** `read_word` ×12 depuis `AddrDali647In` → **tableau ordinaire** → le
bloc → `write_word` ×12 vers `AddrDali647Out`. Un seul idiome, celui que tout le reste du programme
utilise déjà ; position **dynamique** ; inoffensif borne absente (on ne copie pas, on n'appelle pas).

⭐ **Le code mort retrouve son sens** : `AddrDali647In` / `AddrDali647Out`, calculés dans le scan de
modules et **jamais lus** dans aucune version, sont exactement les deux valeurs dont cette solution a
besoin. La tentative de positionnement dynamique était à mi-chemin.

⚠️ **Réserve à lever avec la borne** : `MBX2_RX_DEFRAG` / `MBX2_TX_FRAG`, `mInHandshake` et
`stTrackContext[1..32]` révèlent que ces 24 octets portent un **protocole de boîte aux lettres avec
handshake**. Un handshake est sensible à la fraîcheur et aux doublons : si `read_word`/`write_word`
n'ont pas la même cadence que le rafraîchissement d'image, le dialogue peut se bloquer. Contrôle :
`bFeedback = 0` et les capteurs répondent.

## 7. Les pistes écartées, et pourquoi

| Piste | Verdict |
|---|---|
| **Pointeur dans un grand tableau localisé** (`ADR(image[offset])^`) | *Douteuse*. CoDeSys 2.3 le permet, mais il faut un grand tableau `AT %QB0` qui **recrée le conflit sur toute la zone**. Ne vaut qu'en revenant à l'idiome 1.7 « tout localisé, zéro Mod_com » — prouvé sur 750-842 seulement. |
| **Formaliser la perte** (réserver toujours les 24 octets) | ⛔ **Morte.** 24 octets = **192 sorties digitales**, et sans la 647 ce sont les **192 premières** : tout le rack d'une installation ordinaire. C'est l'état actuel de la 3.0 sans borne, et il est inacceptable. |
| **Deux builds factorisés** | *Réaliste mais à payer indéfiniment* : déjà 7 `.pro` par cible ⇒ 14. CoDeSys 2.3 offre Export/Import `.EXP` par POU, Project Compare, libs internes. ⚠️ La **compilation conditionnelle** (`{IF defined}`) est une capacité de la **V3**, absente de la 2.3 — non vérifié, à ne pas bâtir dessus. À ne retenir que si E1 dit « instance ». |
| **Unifier 641 et 647** | ⛔ **Bloqué par les bibliothèques elles-mêmes**, indépendamment de tout le reste. Modèles opposés : la `DALI_02_v2.x` (641) est **autonome**, l'application ne voit aucune image process ; la `DALI_647_02_v2.3` impose de lui passer l'image et de l'appeler à chaque cycle. Retours incompatibles (`bDimmValue: BYTE` contre `arActualValue: ARRAY OF REAL`, décalage d'adresse `-1`). Commentaire de Wago dans le ST : *« Both modules cannot be used at the same time »*. |

## 8. L'ordre des essais

| | Essai | Coût | Ce qu'il prouve |
|---|---|---|---|
| **E0** | Passer un tableau **ordinaire** au bloc | compile seul, **aucun banc** | ✅ **FAIT le 2026-09-07 : ça compile.** « Le compilateur exige des IO réelles » est **faux**. |
| **E1** | Garder `AT %QB0`, **retirer l'instance et la lib**, sans borne | ~15 min de banc | Zone toujours inutilisable ⇒ **c'est l'image/la déclaration**, la piste §6 s'ouvre. Tout marche ⇒ mécanisme inconnu de la lib, il ne reste que les deux builds. ⏳ **En attente** — l'auteur ne peut pas tout de suite. |
| **E2** | Journaliser `%QB0..23` par `READ_OUTPUT_WORD` si E1 est ambigu | banc | Valeur qui **change** (quelqu'un écrit) contre valeur **figée à 0 malgré nos `write_word`** (écrasement au rafraîchissement) — deux causes distinctes, deux remèdes distincts. |
| **E3** | Piste §6 **avec** la borne | banc | Le handshake survit-il à `read_word`/`write_word` ? (`bFeedback = 0`, capteurs qui répondent) |
| **E4** | Le même binaire **sans** la borne | banc | L'unification tient. |

⚠️ **Piège à ne pas commettre en instrumentant** : utiliser `READ_OUTPUT_WORD`, **jamais** une
variable localisée — celle-ci **recréerait exactement la condition qu'on cherche à mesurer**.

## 9. ⛔ Ce que la lecture ne donnera jamais

Mesuré le 2026-09-07 : **aucune des neuf `.lib` testées** (`DALI_647_02_v2.3`, `DALI_647_PFC_02`,
`DALI_647_SensorType1_02`, `DALI_02_v2.14`, `DALI_02_v2.5`, `DMX_01`, `KNX_02`, `Modb_l05`,
`Wago_1.7/DALI_02v2.4`) ne contient de ST — **zéro `END_VAR`, zéro `FUNCTION_BLOCK`, zéro `IF`**.
Conteneur `CoDeSys+` comme les `.pro`, corps à haute entropie, **aucun flux zlib/deflate** trouvé par
balayage de tous les offsets. `DALI_647_02.EXP` (411 Ko) : **visualisations uniquement, zéro POU**.
`01_ReleaseNote.pdf` : journal de versions, rien sur l'image process.

⭐ **Ce qui EST lisible vient de la table de symboles du `.pro`**, qui met en cache l'interface des
bibliothèques utilisées — pas des `.lib`. On a les signatures et les commentaires de documentation,
jamais l'implémentation. Le mécanisme interne (mailbox MBX2, handshake, délais) est **hors
d'atteinte pour toujours**.

⇒ Puisqu'on ne peut pas savoir ce que la bibliothèque **fait**, il faut mesurer ce qu'elle **nous
fait**. C'est tout l'objet du §8.

## 10. ⚠️ Une hypothèse à surveiller, née de la définition du bloc

```
abActualLevel     : ARRAY [0..96] OF BYTE := 97(255);
xQueryActualValue : BOOL := TRUE;  (* damit bei starten die Datenbank abgefragt wird *)
```

Les niveaux sont initialisés à **255**, la valeur DALI `MASK` — « inconnu » — et la bibliothèque
interroge la base **au démarrage**, ce qui prend du temps.

Or l'automate met son jeton à `1` dès que le niveau est **≠ 0**, et `WODali.cpp:100-107` fait
`if (tokens[1] == "0") value = 0; else value = 100;` ⇒ **un ballast dont le niveau n'est pas encore
connu s'affiche ALLUMÉ**. Et [`T3.156`](T3.156.md) §4 a établi que l'état n'est lu **qu'une fois, à
la construction**, et jamais rafraîchi.

⇒ **Course possible** entre la lecture unique de `calaos_server` au démarrage et l'interrogation de
la base par la borne. ⚠️ **Ce n'est pas le bug de l'utilisateur** — celui-ci ne frappait qu'**un**
luminaire, celui-là les frapperait **tous**. Mais si des luminaires apparaissent encore allumés à
tort une fois `T3.156` en service, c'est ici qu'il faut regarder. Consigné en `F-DALI-11`.

⭐ **Et la morale dépasse le cas** : l'information « je ne sais pas » **existe** au niveau de la
bibliothèque (255 = `MASK`) et elle est **détruite deux étages plus haut**, où tout ce qui n'est pas
`"0"` devient « allumé ». Rendre cette distinction représentable de bout en bout est le **prérequis**
de toute unification — sans quoi un chemin unifié qui interroge une borne absente afficherait
« allumé » au lieu de « inconnu », et propagerait le défaut au lieu de le supprimer.
