# Outillage — cache de compilation (`ccache`) : mesures, honnêteté, recette

⚠️ **Branche `tooling/ccache`, non fusionnée. Rien n'est actif par défaut.**
Le cache ne s'allume que si `/usr/lib/ccache` est en tête du `PATH`. Le `Dockerfile` installe
l'outil, `devcontainer.json` monte le volume : les deux sont **inertes** tant que le `PATH` n'est pas
changé.

**Les conditions de bascule sont dans [T3.51](T3.51.md)** (7 conditions, dont 3 issues de la revue).
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

Il y en a **quatre**, pas un. Trois d'entre eux n'étaient **ni livrés ni empêchés** par la branche, et
⭐ **le deuxième était le défaut que la branche livrait**.

| # | réglage | ce qu'il fait | ancienne sonde |
|---|---|---|---|
| ① | `hash_dir = false` | l'objet du chemin A servi au chemin B ; **diffère** d'une compilation propre (`DW_AT_comp_dir`) | `rc=0` |
| ② | ⭐ `compiler_check = mtime` — **LE DÉFAUT LIVRÉ** | compilateur remplacé à taille et date égales ⇒ **objet périmé** | **`rc=0` par construction** |
| ③ | `compiler_check = string:CONST` | ≡ `none`, que la sonde refusait nommément | `rc=0`, **non listé** |
| ④ | `sloppiness = file_stat_matches[,_ctime]` | en-tête modifié à taille **et** dates égales ⇒ objet précédent servi | **`rc=0`** — voir §5 |

⭐ **La cause commune est la LISTE NOIRE** : elle laisse passer tout ce qu'on n'a pas pensé à y
écrire, et ③ en est la démonstration littérale. ⇒ **remplacée par une LISTE BLANCHE** (§5).

*(Note mesurée : **`base_dir` est SAIN.** C'est le **bon** levier si l'on veut un jour partager entre
chemins différents — ⛔ **pas** `hash_dir=false`, qui est le mensonge ①.)*

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
   conduit tout droit vers le réglage qui rend le cache menteur.

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
| **0** | cache en service, configuration auditée, aller-retours honnêtes | `SONDE-CCACHE: PASS -- …` |
| **77** | ⭐ **SEUL motif restant** : aucun cache en service | `SONDE-CCACHE: SKIP -- AUCUN cache … ce n'est NI un PASS NI une preuve.` |
| **1** | **tout le reste** : cache menteur, configuration non auditée, sonde qui ne compile pas, résultat non concluant, **exception** | `SONDE-CCACHE: ECHEC -- …` |

⭐ **Ce que la sonde faisait avant, et qui est fermé.** Elle était *fail-open* sur **tous** ses modes
d'échec : ④ imprimait « aller-retour honnête » et `rc=0` sur un cache démontré menteur (4/4) ; un
build câblé par `CXX="ccache g++"` rendait `77` ; un `CXX` avec un argument la faisait **planter** et
elle **convertissait sa propre panne en `77`** ; et ⭐ `obj(A) == obj(B)` — **exactement le
mensonge** — était classé « non concluant », `77`. ⚠️ **Et `SKIP` était indiscernable de `PASS` dans
le journal.**

**Ce qu'elle vérifie maintenant :**

1. ⭐ **Liste blanche de configuration** (et non liste noire — c'est ③ qui l'a imposée) :
   `sloppiness` **vide** · `compiler_check = content` · `hash_dir = true` · `base_dir` **vide**.
   Toute autre valeur ⇒ `rc=1`, **avec la raison**.
2. ⭐ **Aller-retour sur un EN-TÊTE**, à **taille et mtime égales** — le seul chemin par lequel ④ peut
   se manifester. ⚠️ **La sonde précédente n'avait AUCUN `#include`** : le mensonge ne pouvait pas
   s'y produire, et c'est **pour cela** qu'elle imprimait « honnête ».
   La mtime est figée **dans le passé** (2020-09-13) et non « maintenant » : `ccache` refuse de se
   fier à `(taille, mtime)` pour un fichier modifié trop récemment, si bien qu'une mtime courante
   rendrait le piège **intermittent**.
3. **Aller-retour sur la SOURCE** A → B → A : `obj(A) != obj(B)` **et** `obj(A rejoué) == obj(A)`.
4. **Attribution du succès de cache par `CCACHE_STATSLOG`** (par invocation), **jamais** `ccache -s`.

**Vérifié [C] — 15 cas, tous conformes** : `77` sans cache · `1` sans cache en mode strict · `1` sur
le défaut `compiler_check=mtime` · **`0` après `ccache-setup.sh`** · `1` sur ④, sur `none`, sur
`string:CONST`, sur `hash_dir=false`, sur `base_dir` non vide · **`0` avec `CXX="ccache g++"`** (au
lieu de `77`) · `1` avec `CXX="ccache"` seul (`ccache: invalid option -- 'g'`, au lieu de `77`) ·
`1` sur un `CXX` cassé (au lieu de `77`) · ⭐ **`1` en 4/4 avec la garde de configuration DÉSARMÉE et
`sloppiness` fautif** — la moitié empirique voit désormais ④ **seule** · `0` en 2/2 sur le témoin
honnête, garde désarmée.

⚠️ **Ce qui reste vrai** : la moitié empirique est un **détecteur par échantillon** — elle prouve
qu'un mensonge s'est produit, jamais qu'aucun ne peut se produire. **La liste blanche reste la garde
principale.** Les deux ne se remplacent pas.

`CALAOS_CCACHE_PROBE_STRICT=1` transforme le `77` en échec — condition (7) de [T3.51](T3.51.md).

**Coût mesuré [C]** : **34 ms** sans cache (sortie 77) · **173 ms** avec cache chaud · **168 ms** à
froid (`CCACHE_DIR` neuf, six compilations triviales) — ⚠️ le coût est le même à froid et à chaud, ce
sont les six compilations qui le font, pas le cache.

## 6. ⭐ Ce que le cache rend POSSIBLE, et qui est le meilleur argument en sa faveur

`F-FLAKY-1` (`core/ShutterImpulse_test`, [T3.49](T3.49.md)) a flanché **1 fois sur 12**, puis **2 fois
de plus** pendant la revue de cette branche. **Le site est réel ; le taux est inconnu.**

⭐ **L'abstention est JUSTE** : 1/12 donne un IC 95 % de l'ordre de **[1,5 %, 35 %]**. On ne peut rien
en conclure.

⭐ **Mais borner le taux sous 1 % demande ~300 verts consécutifs :**

| | `make check` | 300 verts consécutifs |
|---|---|---|
| **sans cache** | **105 s** | **≈ 8,8 h** |
| **avec cache** | **29,8 s** | **≈ 2,5 h** |

**8,8 h, c'est une campagne qu'on n'ouvre pas ; 2,5 h, c'en est une qu'on lance le soir.**
⇒ ⭐ **Le cache ne sert pas d'abord à aller plus vite : il rend faisable une mesure qu'on renonçait à
faire.** C'est l'argument le plus fort en faveur de la bascule, et il ne figurait nulle part.

## 7. Effet sur le compte de suites

⚠️ **Le compte se recompte, il ne se recopie pas.** La branche annonçait « 98 → 99 » ; ⛔ **c'était
vrai avant que `master` n'avance**.

**Recompté [C]** sur la branche **rebasée sur `7667838f`**, sans cache, `PATH` canonique :

```
# TOTAL: 101   # PASS: 99   # SKIP: 2   # FAIL: 0   # XFAIL/XPASS/ERROR: 0
```
`MAKE_RC=0`, `CHECK_RC=0`. `master` = **100** entrées, la branche en ajoute **une**.
Les 2 `SKIP` sont `run-python-tests.sh` (`pytest` absent) et `check-ccache-honesty.sh` (pas de cache).
**94** binaires de test (**95** `check_PROGRAMS` − `StaticLogShutdown_helper`), **1639** cas gtest.
Goldens : **145** fichiers, arbre `d4ebc61f` — **inchangés**.

⭐ **Preuve `base + queue` (préfixe strict).** L'entrée est ajoutée **en toute fin de
`tests/Makefile.am`, après le dernier `endif`** — c'est le piège qui a mordu trois fois sur ce
fichier. Vérifié en `python3` : `tests/Makefile.am` de `master` est un **préfixe strict** de celui de
la branche (**+877 octets, +14 lignes**, **1** seul `TESTS +=`, **0** `if`/`endif` dans la queue), et
dans le `Makefile` **généré** `check-ccache-honesty.sh` est la **dernière** entrée de `TESTS`, **hors
de tout `am__EXEEXT_n`** — donc hors de tout `if HAVE_GTEST`.
