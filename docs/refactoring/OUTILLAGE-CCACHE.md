# Outillage — cache de compilation (`ccache`) : mesures, honnêteté, recette

⚠️ **Branche `tooling/ccache`, non fusionnée. Rien n'est actif par défaut DANS LE CONTENEUR.**
Le cache ne s'allume que si `/usr/lib/ccache` est en tête du `PATH`. Le `Dockerfile` installe
l'outil, `devcontainer.json` monte le volume : les deux sont **inertes** tant que le `PATH` n'est pas
changé.

> ⛔ ⚠️ **MAIS PAS HORS CONTENEUR.** Sur **Fedora** (`/usr/lib64/ccache`) et **Gentoo**
> (`/usr/lib/ccache`), ce répertoire est dans le `PATH` **par défaut** et `g++` y est un **lien vers
> `ccache`**. ⇒ **La sonde y trouve un cache en service et non configuré, rend `1`, et `make check`
> est ROUGE dès le premier essai.** ⭐ **C'est le cas de la machine de ce dépôt** — voir §4, *hors
> conteneur*, et ses **TROIS** issues.

**Les conditions de bascule sont dans [T3.51](T3.51.md)** — **7 numérotées, 6 ACTIVES** (la n° 4 est
retirée, son numéro conservé pour ne pas décaler les renvois), dont 3 issues de la revue.
**Le parallélisme de `make distcheck` est dans [T3.52](T3.52.md)** — c'est un levier **indépendant**,
sorti d'ici pour que les deux gains restent attribuables séparément.

> ⚠️ **Provenance des chiffres.** Chaque nombre de ce document porte sa source. Trois sources se
> mélangeaient dans la version précédente et **un chiffre lu sur le cache PARTAGÉ pendant que trois
> agents construisaient y était publié comme s'il était attribuable**. Il ne l'était pas.
> - **[A]** mesuré par l'auteur de la branche, machine 64 cœurs, **trois autres agents en
>   construction**, `master` `74d0c520`, image `calaos-ccache:essai` (= image de dev + `ccache`
>   4.7.5).
> - **[R]** re-mesuré par la revue sur un **`CCACHE_DIR` privé** — c'est la source qui **corrige** [A].
> - **[C]** mesuré au retour de revue, sur la branche **rebasée sur `7667838f`**.
> - **[C2]** mesuré à la **3ᵉ passe de correction**, branche **rebasée sur `75ed9cb9`**, sur `ccache`
>   **4.12.3** (hôte Fedora) **et** **4.7.5** (image `calaos-ccache:essai`) — la version est dite
>   à chaque fois, parce qu'elle change des résultats.

## 1. Le gain — avec la charge, et en fourchette

⚠️ **Un rapport unique n'a pas de sens sur cette machine** : la charge varie d'un facteur 10 entre le
début et la fin d'une mesure. Les rapports sont donnés **avec la charge relevée**.

| mesure | sans cache | avec cache chaud | rapport | charge | src |
|---|---|---|---|---|---|
| **bras de campagne de mutations** (purge des objets/logs + binaires, 1 `.cpp` muté, `make` racine + `make check`) — **3 répétitions** | 102,1 / 103,7 / 104,3 s | **20,2 / 20,5 / 21,0 s** | **×5,0** | — | **[R]** |
| **reconstruction complète** | — | — | **×6,2** | **10 → 125** | **[R]** |
| `make check` seul | **105 s** | **29,8 s** | ×3,5 | — | **[R]** |
| ⭐ **purge chirurgicale** (`rm -f` d'un seul objet + son binaire) | **6,37 s** | **2,06 s** | **×3,1**, **−68 %** | — | **[R]** |
| plancher `autogen` + `configure` | **10,3 – 12,4 s** | idem | **×1,0** | — | **[R]** |

⭐ **Correction d'une affirmation de la branche.** Elle annonçait **×8,4** sur la reconstruction
complète et **×4,3** sur le bras de campagne. ⚠️ Les deux étaient **fondées sur une charge non
relevée** : re-mesurées, c'est **×6,2** (charge 10 → 125) et **×5,0**. ⭐ **Le bras de campagne est
donc MEILLEUR qu'annoncé, et la reconstruction MOINS BONNE.** Les deux chiffres étaient faux dans des
sens opposés — c'est exactement ce que produit un rapport publié sans sa charge.

⭐ **Correction d'une seconde affirmation, dans l'autre sens.** La branche écrivait que le gain était
« **nul** sur un `rm -f` chirurgical ». ⚠️ **C'est infirmé** : **6,37 s → 2,06 s, ×3,1, −68 %**.
**Petit en absolu, pas nul.** Le `make` à la racine, lui, ne gagne effectivement rien ; **tout le
gain vient du `make check`, qui recompile le `.cpp` du test.**

⚠️ **Là où le cache ne rend RIEN** : `autogen`+`configure` (**10,3–12,4 s**) est un plancher
incompressible. Le gain reste proportionnel au nombre d'unités recompilées **à l'identique** : maximal
sur les protocoles façon [T3.25](T3.25.md) (purge de tous les objets de `tests/`) et sur tout
`distclean`.

### Taux de succès — et d'où il vient

| mesure | taux | `CCACHE_DIR` | src |
|---|---|---|---|
| reconstruction complète | **417/420 = 99,29 %** | **privé** | **[R]** |
| reconstruction complète | **471/477 = 98,74 %** | **privé** | **[R]** |
| ~~416/417 = 99,76 %~~ | ⛔ **retiré** | **partagé, 3 agents en construction** | ~~[A]~~ |

⛔ **Le `416/417` de la version précédente n'est PAS attribuable** : il a été lu par `ccache -s` sur
le cache **partagé** pendant que trois agents construisaient. `ccache -s` publie l'**agrégat de tous
les agents**. ⇒ **Condition (6) de [T3.51](T3.51.md) : aucun taux de succès n'est publié depuis le
`CCACHE_DIR` partagé, et jamais `ccache -z` dessus.**

## 2. L'honnêteté — éprouvée, pas citée

Le cache **tel que livré** ne ment pas. **[A]**, confirmé **[R]** :

- **12 scénarios d'attaque, tous sains** : chemins identiques · macro en ligne de commande ·
  `config.h` généré · collision (taille, mtime) sur **en-tête** *et* sur **source** ·
  `__DATE__`/`__TIME__` · mtime décalée d'un an · **trois compilations dans la même seconde** ·
  `cp -p` · **cache saturé, forcé à évincer**.
- **370 objets sur 370 identiques** octet pour octet entre un arbre bâti depuis le cache et un arbre
  compilé de zéro ; **370/370** aussi entre deux constructions concurrentes.
- **Aller-retour rouge** : mutation → restauration → re-mutation ⇒ l'objet du mutant revient, le test
  **redevient rouge**.
- **90/90 en concurrence** sous **charge 127**.
- **11 étapes jouées deux fois**, avec et sans cache, sur `src/lib/base64.cpp` (mutant `i < 64` →
  `i < 63`) et `MqttWire.h` : **11 empreintes d'objets IDENTIQUES entre les deux modes**.

⚠️ **La restauration `cp -p` (dates préservées) n'est ni aggravée ni masquée par le cache** : `make`
n'appelle simplement pas le compilateur, le cache n'est pas consulté (`CXXLD` = 0). ⭐ **La discipline
`rm -f` reste indispensable — le cache ne dispense d'aucune règle du protocole.**

### ⛔ « Le seul réglage qui rendrait le cache menteur » — **cette phrase est FAUSSE et elle est retirée**

Il y en a **cinq**, pas un. Quatre d'entre eux n'étaient **ni livrés ni empêchés** par la branche, et
⭐ **le deuxième était le défaut que la branche livrait**.

| # | réglage | ce qu'il fait | ancienne sonde |
|---|---|---|---|
| ① | `hash_dir = false` | l'objet du chemin A servi au chemin B ; **diffère** d'une compilation propre (`DW_AT_comp_dir`) | `rc=0` |
| ② | ⭐ `compiler_check = mtime` — **LE DÉFAUT LIVRÉ** | compilateur remplacé à taille et date égales ⇒ **objet périmé** | **`rc=0` par construction** |
| ③ | `compiler_check = string:CONST` | ≡ `none`, que la sonde refusait nommément | `rc=0`, **non listé** |
| ④ | `sloppiness = file_stat_matches[,_ctime]` | en-tête modifié à taille **et** dates égales ⇒ objet précédent servi | **`rc=0`** — voir §5 |
| ⑤ | ⭐ **`ignore_options = -D*`** **[C2]** | une **option** sort de la clef : `-DVAL=2` reçoit l'objet de `-DVAL=1` — objets identiques à l'octet, `direct_cache_hit`, **`mov $0x1`** au désassemblage | **`rc=0`**, ⚠️ **y compris avec la « liste blanche » de 4 clefs** |

⭐ **La cause commune est la LISTE NOIRE** : elle laisse passer tout ce qu'on n'a pas pensé à y
écrire, et ③ en est la démonstration littérale. ⚠️ **Et la « liste blanche » de quatre clefs qui l'a
d'abord remplacée était la même faute sous un autre nom** — ⑤ l'a démontré. ⇒ **remplacée à son tour
par l'AUDIT INTÉGRAL de ce que `ccache -p` publie** (§5).

*(Note mesurée, et ⭐ **TRANCHÉE** : **`base_dir` n'est pas malhonnête** — ⛔ **mais ce n'est pas une
option ouverte**, et ce document ne le recommande plus : la sonde l'exige **vide** et rend `1` sinon.
⚠️ **Le partage entre chemins différents n'est donc PAS disponible aujourd'hui** ; le rendre possible
demandera d'**auditer** `base_dir` — le mesurer contre une compilation propre — et de modifier la
table de la sonde. ⛔ Ce qui reste interdit sans condition, c'est `hash_dir=false`, le mensonge ①.)*

## 3. Le partage entre agents — et ce qui n'est pas « sans danger »

`hash_dir` est **vrai** : le répertoire de compilation entre dans la clef. Le partage marche **parce
que tous les conteneurs montent leur worktree au même chemin** `/workspaces/calaos_base`.
`make distcheck` compile dans `calaos-*/_build/sub` : il a ses propres entrées.

⭐ **Correction.** La branche écrivait : « *un agent qui monte ailleurs n'aura aucun succès de cache —
c'est sans danger, seulement sans gain* ». ⚠️ **C'est vrai à moitié.** La partie correcte l'est
(0 succès, les objets restent la vérité). **Ce qui manquait** :

1. ⚠️ Les **1763 fichiers** du cache partagé sont **tous `uid=0`**, dans un `$HOME` de l'`uid` 1000
   — **constaté [C]**. Un agent hors conteneur ne peut ni les lire ni les nettoyer sans `busybox`.
2. ⚠️ **`max_size` est PARTAGÉ.** Un agent monté à un autre chemin **double l'empreinte** et
   **évince les entrées chaudes des autres, invisiblement** : les voisins ne voient qu'un taux de
   succès qui baisse, sans cause visible.
3. ⛔ **Le « correctif » intuitif à l'absence de gain est précisément `hash_dir=false`** — c'est-à-dire
   le mensonge ①. **C'est le piège** : celui qui constate « je n'ai aucun succès de cache » est
   conduit tout droit vers le réglage qui rend le cache menteur. ⚠️ **Et `base_dir` n'est pas la
   porte de sortie** : la sonde l'exige vide (voir §2). **Monter au même chemin est aujourd'hui la
   seule façon de partager.**

### ⚠️ ⭐ `ccache.conf` est un ÉTAT PARTAGÉ MUTABLE

Un `ccache -o` joué dans un conteneur **persiste dans `$CCACHE_DIR/ccache.conf`**, et **tout conteneur
ultérieur qui monte le même répertoire en hérite**, sans trace de qui l'a écrit. Un `-M` change la
taille maximale **de tous les autres**.

⚠️ **Constaté [C]** : le cache partagé `$HOME/.cache/calaos-ccache` porte **`max_size = 30G`** alors
que ce document annonçait **10G**, **sans attribution**.

**Comment s'en prémunir** — par ordre de préférence :
1. ⭐ **Un `CCACHE_DIR` par agent.** C'est la seule parade complète : ni configuration ni compteurs ni
   quota partagés.
2. **Les variables d'environnement `CCACHE_*`** (`CCACHE_COMPILERCHECK`, `CCACHE_SLOPPINESS`,
   `CCACHE_HASHDIR`, `CCACHE_BASEDIR`, `CCACHE_MAXSIZE`) : elles **ne persistent rien**, `ccache -p`
   les publie avec l'origine `(environment)`, et la sonde les audite comme les autres.
3. **`scripts/ccache-setup.sh` imprime la configuration HÉRITÉE avant de la réécrire**, pour qu'un
   écart non attribué se voie au lieu de se transmettre.

⚠️ **`ccache -s` lit des compteurs partagés** — l'agrégat de tous, pas ceux de sa propre construction.
⛔ **Et `ccache -z` sur un cache partagé remet à zéro les compteurs de TOUS les agents** : la version
précédente de ce document le recommandait sans le dire. **Ne pas le faire.** La sonde n'appelle
jamais `ccache -z` et n'utilise jamais `ccache -s` : elle attribue le succès de cache par
`CCACHE_STATSLOG`, **journal par invocation**.

**Dimensionnement [A]** : 370 objets ≈ **150 Mo** compressés (`compression_level=1`) par état d'arbre
distinct ; 287 Mo observés pour un arbre complet + 6 variantes. **Sur DISQUE**
(`$HOME/.cache/calaos-...`), ⚠️ **jamais sur `/tmp`** (tmpfs 32 Go partagé, déjà saturé une fois).

## 4. La recette pour un agent

```sh
# UN CACHE PAR AGENT : pas de configuration, pas de quota, pas de compteurs partagés.
mkdir -p "$HOME/.cache/calaos-ccache-$AGENT"
docker run --rm \
  -v <worktree>:/workspaces/calaos_base -w /workspaces/calaos_base \
  -v "$HOME/.cache/calaos-ccache-$AGENT":/ccache -e CCACHE_DIR=/ccache \
  -e PATH=/usr/lib/ccache:/opt/.local/bin:/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin \
  calaos-ccache:essai \
  bash -c "scripts/ccache-setup.sh && ./autogen.sh && ./configure && make -j32 && make check -j16"
```

Retirer les trois lignes `-v /ccache`, `-e CCACHE_DIR`, `-e PATH` rend la recette canonique **à
l'identique**.

⚠️ **`scripts/ccache-setup.sh` n'est pas facultatif.** Sans lui, `compiler_check` vaut `mtime` (②) et
**la sonde échoue** (`rc=1`) — c'est voulu : le défaut de `ccache` n'est pas sûr, il est seulement
courant.

### ⛔ ⚠️ HORS CONTENEUR : **`make check` est ROUGE par défaut sur Fedora et Gentoo** — **[C2]**

⚠️ **Ce document affirmait « rien n'est actif par défaut ». C'est FAUX sur la machine de ce dépôt.**
`/usr/lib64/ccache` est dans le `PATH` **par défaut** sur Fedora (`/usr/lib/ccache` sur Gentoo) et
`g++` y est un **lien vers `ccache`** : `which g++` rend `/usr/lib64/ccache/g++`. ⇒ **La sonde y
trouve un cache EN SERVICE et NON CONFIGURÉ, et rend `1` sans que personne n'ait rien allumé.**

⭐ **TROIS issues, pas une** :

| # | geste | résultat |
|---|---|---|
| **1** | `scripts/ccache-setup.sh` (avec ou sans `CCACHE_DIR`) | **`0` PASS** — vérifié **[C2]** hors conteneur |
| **2** | `CCACHE_SLOPPINESS= CCACHE_COMPILERCHECK=content CCACHE_HASHDIR=true CCACHE_BASEDIR=` | **`0` PASS**, rien n'est persisté |
| **3** | ⭐ **retirer le répertoire de *shims* du `PATH`**, ou `CXX=/usr/bin/g++` | **`77` SKIP propre** |

⚠️ **Et l'issue 1 ÉCHOUAIT hors conteneur** : `scripts/ccache-setup.sh` sans `CCACHE_DIR` rendait
`mkdir: cannot create directory '/ccache': Permission denied`, **`rc=1`** — `/ccache` n'existe que
dans le conteneur. ⇒ **Corrigé** : le script **demande à `ccache` où est son cache**
(`ccache -k cache_dir`) quand `CCACHE_DIR` n'est pas posé, vérifie que le répertoire est
inscriptible, et **rend `rc=0` hors conteneur** — mesuré **[C2]** dans les deux modes.

⚠️ **Taux de succès** : `ccache -s` **uniquement sur un `CCACHE_DIR` privé**, et le chiffre publié dit
d'où il vient. ⛔ **Jamais `ccache -z` sur un cache partagé.**

⚠️ **Ce qui ne change pas** : `rm -f` des objets **et** du binaire · `make` **à la racine** · `CXXLD`
exigé par **regex ancrée** · code de sortie **et** `# TOTAL` **recomptés** · restauration **sans**
préserver les dates · ⭐ **et tout RED→GREEN qui décide d'un ticket reconfirmé UNE FOIS SANS CACHE**
(condition 5 de [T3.51](T3.51.md)). **Le cache accélère la compilation ; il ne prouve rien.**

## 5. Le contrôle permanent — `tests/check-ccache-honesty.sh`

Entrée `TESTS`, appelle `scripts/ccache-honesty-probe.py`.

**Trois codes de sortie, et AUCUN mode d'échec ne rend PASS :**

| code | quand | ligne imprimée |
|---|---|---|
| **0** | cache en service, **toute** la configuration publiée auditée, **succès de cache constaté**, aller-retours honnêtes | `SONDE-CCACHE: PASS -- …` |
| **77** | ⭐ **SEUL motif restant** : aucun cache en service | `SONDE-CCACHE: SKIP -- AUCUN cache … ce n'est NI un PASS NI une preuve.` |
| **1** | **tout le reste** : cache menteur, **une seule** clef de configuration non auditée, **détecteur qui ne peut pas mordre**, piège non armé, sonde qui ne compile pas, résultat non concluant, **exception** | `SONDE-CCACHE: ECHEC -- …` |

⭐ **Ce que la sonde faisait avant, et qui est fermé — HUIT modes, en trois vagues.**
**Première vague** : ④ imprimait « aller-retour honnête » et `rc=0` sur un cache démontré menteur
(4/4) ; un build câblé par `CXX="ccache g++"` rendait `77` ; un `CXX` avec un argument la faisait
**planter** et elle **convertissait sa propre panne en `77`** ; ⭐ `obj(A) == obj(B)` — **exactement
le mensonge** — était classé « non concluant », `77` ; et **`SKIP` était indiscernable de `PASS`**.
**Deuxième vague** (2ᵉ revue) : ⭐ **la « liste blanche » ne couvrait que 4 des 44 réglages publiés**
(⑤ `ignore_options=-D*` ⇒ `rc=0` sur un mensonge prouvé au désassemblage) ; ⭐ **la sonde annonçait
« le piège est armé et il mord » sans jamais le vérifier** (`disable`/`recache`/`read_only` ⇒ `0` en
3/3, garde désarmée). **Troisième vague** (cette passe) : ⭐ **elle auditait le `ccache` du `PATH`,
pas celui que `CXX` emploie**.

**Ce qu'elle vérifie maintenant :**

1. ⭐ **AUDIT INTÉGRAL DE LA CONFIGURATION — toutes les clefs publiées, pas quatre.** ⚠️ La « liste
   blanche » de la passe précédente couvrait **4 réglages sur les 44** publiés par `ccache -p` :
   **une liste noire de 4 lignes déguisée**, qui laissait passer ⑤ (`ignore_options`) mais aussi
   `direct_mode`, `hard_link`, `prefix_command`, `compiler`, `disable`, `read_only`, `recache`,
   `remote_storage`, `path`… ⇒ **la sonde demande à l'outil ce qu'il publie** et exige que **chaque
   clef** soit couverte : **33 clefs à valeur exigée + 11 explicitement libres** (chemins, quotas,
   compression du **stockage**) en 4.12.3 ; **32 + 12** en 4.7.5 ; **0 inconnue** des deux côtés.
   ⛔ **Toute clef publiée absente de la table ⇒ `rc=1`, en la nommant** — une version ultérieure de
   `ccache` qui ajoute un réglage dangereux se signale au lieu de passer. Une **empreinte `sha256`**
   de la configuration publiée est imprimée à chaque PASS.
2. ⭐ **UN SUCCÈS DE CACHE CONSTATÉ.** ⚠️ La sonde **annonçait** « le piège est armé et il mord »
   **sans jamais le vérifier**. ⇒ si l'en-tête restauré à l'identique (3ᵉ compilation) et la source
   rejouée (6ᵉ) ne sont pas servis **par le cache**, elle rend **`1`**. *Un détecteur qui ne peut pas
   mordre doit échouer, pas réussir.*
3. ⭐ **LE PIÈGE S'AUTO-VÉRIFIE** : après réécriture, la sonde **constate** que `(taille, mtime)`
   sont identiques ; sinon `rc=1`.
4. ⭐ **LE CACHE AUDITÉ EST CELUI QUE `CXX` EMPLOIE**, pas celui du `PATH` : un enrobage nommé
   `ccache` qui exporte `CCACHE_IGNOREOPTIONS=-D*` faisait auditer un exemplaire **propre** pendant
   qu'un autre mentait (`rc=0`) ⇒ désormais `rc=1`.
5. **Aller-retour sur un EN-TÊTE**, à **taille et mtime égales** — le seul chemin par lequel ④ peut
   se manifester. ⚠️ **La sonde précédente n'avait AUCUN `#include`** : le mensonge ne pouvait pas
   s'y produire, et c'est **pour cela** qu'elle imprimait « honnête ». La mtime est figée **dans le
   passé** (2020-09-13) : laissée à « maintenant », le piège **ne mord pas** (voir plus bas).
6. **Aller-retour sur la SOURCE** A → B → A : `obj(A) != obj(B)` **et** `obj(A rejoué) == obj(A)`.
7. **Attribution du succès de cache par `CCACHE_STATSLOG`** (un journal **par invocation**),
   **jamais** `ccache -s`, **jamais** `ccache -z`.

**Vérifié [C2] — 27 cas rejoués DEUX FOIS sur `ccache` 4.12.3 (hôte Fedora) et 24 cas rejoués DEUX
FOIS sur 4.7.5 (conteneur) : 102 exécutions, toutes conformes** : `77` sans cache · `1` sans cache en mode strict · `1` sur le défaut
`compiler_check=mtime` · **`0` après `ccache-setup.sh`** (sans aucune variable `CCACHE_*`) · `1` sur
④, sur `none`, sur `string:CONST`, sur `hash_dir=false`, sur `base_dir` non vide, sur `time_macros` ·
⭐ **`1` sur `ignore_options=-D*`, `ignore_headers_in_manifest`, `prefix_command`, `compiler`,
`disable`, `read_only`, `recache`, `direct_mode=false`** (tous **`0`** auparavant) · ⭐ **`1` sur
l'enrobage `ccache` qui injecte `-D*`** · **`0` avec `CXX="ccache g++"`** · `1` avec `CXX="ccache"`
seul, avec un drapeau inexistant, avec un compilateur introuvable derrière `ccache` · `1` sur un
`CCACHE_DIR` non inscriptible.
⚠️ **Un cas rend `0`, et c'est JUSTE** : `max_size=1` sur un `CCACHE_DIR` neuf — mesuré, le cache
**sert encore** (l'éviction n'est pas immédiate), le 3ᵉ appel **est** un succès de cache, donc le
détecteur n'est pas mort et le PASS est légitime.

**Sous le harnais automake RÉEL [C2]** : `PASS: check-ccache-honesty.sh` avec cache actif, **et en
mode `CALAOS_CCACHE_PROBE_STRICT=1`** ; `SKIP: check-ccache-honesty.sh` sans cache, avec la phrase
entière dans `test-suite.log` (`SONDE-CCACHE: SKIP -- AUCUN cache … ce n'est NI un PASS NI une
preuve.`).

### ⚠️ Ce que la moitié empirique voit — et surtout ce qu'elle NE voit PAS, garde désarmée **[C2]**

| cas | obtenu | lecture |
|---|---|---|
| `sloppiness = file_stat_matches,file_stat_matches_ctime` | **`1` en 4/4** (4.12.3 **et** 4.7.5) | elle voit **le couple** |
| ⛔ `sloppiness = file_stat_matches` **SEUL** | **`0` en 4/4** des deux côtés | ⚠️ **elle ne le voit PAS** — `ccache` compare encore le `ctime`, qu'`os.utime` ne peut pas remettre en place |
| témoin honnête | `0` en 2/2 | — |
| `disable` / `recache` / `read_only` | **`1` en 3/3** (était `0` en 3/3) — `disable` **4/4** en 4.7.5 | c'est **le succès de cache exigé** qui les attrape |
| ⛔ `direct_mode=false` + `sloppiness` menteur | **`0` en 3/3** (4.12.3) et **4/4** (4.7.5) | ⚠️ **elle ne voit pas le mode préprocesseur** — c'est **la table** qui l'attrape |
| ⛔ mtime **non figée** | **`0` en 6/6** (4.12.3) | le piège **n'était pas armé** ; l'auto-vérification rend désormais `1` — 3/3 (4.12.3), 4/4 (4.7.5) |

⭐ **CORRECTION D'UNE PHRASE DE LA VERSION PRÉCÉDENTE** : « *elle voit `file_stat_matches` SEULE,
4/4* » est **fausse et infirmée en 4/4 des deux côtés**.

⚠️ ⭐ **Conclusion à écrire telle quelle** : la moitié empirique est un **détecteur par échantillon**
qui **ne couvre ni les options ignorées, ni `direct_mode=false`, ni `file_stat_matches` seul**, et
dont le piège **ne mord que grâce à une date figée**. ⇒ ⭐ **LA GARDE PRINCIPALE EST L'AUDIT INTÉGRAL
DE LA CONFIGURATION.** Les deux ne se remplacent pas, et elles ne pèsent pas le même poids.

`CALAOS_CCACHE_PROBE_STRICT=1` transforme le `77` en échec — condition (7) de [T3.51](T3.51.md).

**Coût mesuré [C2]** : **29–40 ms** sans cache (sortie 77) · **165–176 ms** avec cache — 5
répétitions sur `ccache` 4.7.5 **et** 4.12.3, machine 64 cœurs chargée ; **170 ms à froid**
(`CCACHE_DIR` neuf). ⚠️ Le coût est le même à froid et à chaud : ce sont les six compilations qui le
font, pas le cache. ⚠️ **La version précédente annonçait « ~0,3 s » dans le script et dans
`tests/Makefile.am`** alors que sa propre mesure disait 173 ms — **corrigé des deux côtés**.

## 6. ⭐ Ce que le cache rend POSSIBLE, et qui est le meilleur argument en sa faveur

`F-FLAKY-1` (`core/ShutterImpulse_test`, [T3.49](T3.49.md)) a flanché **1 fois sur 12**, puis **2 fois
de plus** pendant la revue de cette branche. **Le site est réel ; le taux est inconnu.**

⭐ **L'abstention est JUSTE** : 1/12 donne un IC 95 % de l'ordre de **[1,5 %, 35 %]**. On ne peut rien
en conclure.

⭐ **Mais borner le taux sous 1 % demande ~300 verts consécutifs :**

| | `make check` | 300 verts consécutifs | src |
|---|---|---|---|
| **sans cache** | **105 s** | **≈ 8,8 h** | **[R]** |
| **avec cache** | **29,8 s** | **≈ 2,5 h** | **[R]** |

*(⚠️ Les deux durées de « 300 verts » sont **arithmétiques** — 300 × la durée d'un `make check` —,
**pas mesurées**. Dit ici pour que personne ne les cite comme des mesures.)*

**8,8 h, c'est une campagne qu'on n'ouvre pas ; 2,5 h, c'en est une qu'on lance le soir.**
⇒ ⭐ **Le cache ne sert pas d'abord à aller plus vite : il rend faisable une mesure qu'on renonçait à
faire.** C'est l'argument le plus fort en faveur de la bascule, et il ne figurait nulle part.

## 7. Effet sur le compte de suites

⚠️ **Le compte se recompte, il ne se recopie pas.** La branche annonçait « 98 → 99 » ; ⛔ **c'était
vrai avant que `master` n'avance**.

**Recompté [C2]** sur la branche **rebasée sur `75ed9cb9`** (⚠️ `master` a de nouveau avancé : E4.1m
y a ajouté `core/JsonApiModelWireBytes_test`), sans cache, `PATH` canonique, `make` **à la racine** :

```
# TOTAL: 102   # PASS: 100   # SKIP: 2   # FAIL: 0   # XFAIL/XPASS/ERROR: 0
```
`MAKE_RC=0`, `CHECK_RC=0`, **7 `CXXLD`** au `make` racine (regex ancrée `^ *CXXLD `), **61** au
`make check`. ⚠️ **`master` = 101 entrées**, la branche en ajoute **une** ⇒ **102**.
⛔ **La version précédente annonçait `# TOTAL: 101` et « `master` = 100 » : c'était vrai contre
`7667838f`, c'est FAUX contre le `master` d'aujourd'hui.** Le compte **se recompte à chaque rebase**.
Les 2 `SKIP` sont `run-python-tests.sh` (`pytest` absent) et `check-ccache-honesty.sh` (pas de cache).
**95** binaires de test (**96** exécutables dans `tests/` − `StaticLogShutdown_helper`), **1658** cas
gtest — ⚠️ **recomptés** : c'était **94** et **1639** avant que `master` n'ajoute
`core/JsonApiModelWireBytes_test`.
Goldens : **145** fichiers, arbre `d4ebc61f` — **inchangés**.

⭐ **Preuve `base + queue` (préfixe strict), rejouée après RÉSOLUTION DE CONFLIT.** ⚠️ `master` a
**lui aussi** appendu en queue de `tests/Makefile.am` (un bloc `if HAVE_GTEST` complet) : le rebase a
donc **conflité**, et la résolution place le bloc de `master` **d'abord**, le nôtre **après le dernier
`endif`** — c'est le piège qui a mordu **trois fois** sur ce fichier. Vérifié en `python3` :
`tests/Makefile.am` de `master` est un **préfixe strict** de celui de la branche
(**+1708 octets, +29 lignes**, **1** seul `TESTS +=`, **0** `if`/`endif` dans la queue, queue =
`['check-ccache-honesty.sh']`). ⚠️ **Le « +877 o » de la version précédente était vrai contre
`7667838f` et ne l'est plus.**
