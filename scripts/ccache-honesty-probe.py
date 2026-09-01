#!/usr/bin/env python3
"""Sonde d'honnetete du cache de compilation (ccache).

Un cache malhonnete rend un objet qui ne correspond pas a la source compilee.
Il rendrait vertes des campagnes de mutations entieres et invaliderait tout
verdict tire d'un `make check`. Cette sonde est le controle permanent de cet
outil ; elle est cablee dans TESTS.

L'enumeration complete de ce que la sonde voit et de ce qu'elle ne voit pas
est dans docs/refactoring/T3.51.md ; ce preambule ne garde que ce qui ne se
deduit pas du code.

TROIS codes de sortie, et AUCUN mode d'echec ne rend 0 :

  0  PASS  -- un cache est en service sur au moins un des DEUX canaux audites
              (CXX et CC), la configuration qu'il PUBLIE est EXACTEMENT celle
              qui a ete auditee (toutes les clefs, pas quelques-unes), un
              succes de cache a ete CONSTATE, et les TROIS aller-retours
              empiriques sont honnetes. La ligne commence par
              "SONDE-CCACHE: PASS".

  77 SKIP  -- aucun cache de compilation n'est en service, NI sur CXX NI sur
              CC. C'est le SEUL motif de SKIP qui subsiste. La ligne commence
              par "SONDE-CCACHE: SKIP" et le dit en toutes lettres, pour qu'un
              SKIP ne puisse plus etre lu comme un PASS dans un journal.
              CALAOS_CCACHE_PROBE_STRICT=1 transforme ce SKIP en ECHEC : un
              arbre qui sert a juger une campagne doit rendre 0, pas SKIP.

  1  ECHEC -- TOUT le reste. Cache malhonnete, configuration non auditee,
              clef exigee DISPARUE de `ccache -p`, detecteur qui ne peut pas
              mordre, sonde qui ne compile pas, resultat non concluant,
              exception imprevue.
              Fermeture par defaut : ce qui n'est pas prouve honnete est
              declare faux. La sonde n'a le droit de se taire que quand il n'y
              a rien a garder.

Ce que la sonde verifie, dans l'ordre, POUR CHACUN des canaux CXX et CC :

  1. LA CONFIGURATION ENTIERE, telle que l'outil la publie. Ni liste noire,
     ni liste blanche de quelques clefs : les deux sont toujours en retard
     d'une clef. `ccache -p` ENUMERE lui-meme sa configuration ; la sonde
     exige que CHAQUE clef publiee soit couverte par la table, et que CHAQUE
     clef exigee par la table soit PUBLIEE -- une clef exigee qui cesse
     d'etre publiee cesserait sinon d'etre auditee, sans que rien ne le dise.
  2. Un SUCCES DE CACHE CONSTATE. Le detecteur du point 3 ne peut mordre que
     si le cache sert vraiment quelque chose ; `disable`, `recache`,
     `read_only` ou un CCACHE_DIR non ecrivable le rendent STRUCTURELLEMENT
     MORT. Un detecteur qui ne peut pas mordre doit ECHOUER, pas reussir.
     Un `max_size` minuscule, lui, ne le tue PAS : l'eviction n'est pas
     immediate et ce point ne le voit pas.
  3. TROIS aller-retours : (0) la meme source compilee avec DEUX MACROS
     DIFFERENTES en ligne de commande doit rendre DEUX objets differents ;
     (1) un EN-TETE modifie a taille et mtime preservees ; (2) la SOURCE
     A -> B -> A. obj(A) == obj(B) n'est pas "non concluant" : c'est
     exactement le mensonge, et c'est un ECHEC.

⚠️ CE QUE LA MOITIE EMPIRIQUE NE PEUT PAS VOIR -- elle est un DETECTEUR PAR
ECHANTILLON : elle prouve qu'un mensonge s'est produit, jamais qu'aucun ne
peut se produire. Lui echappent le mode preprocesseur (`direct_mode=false` /
CCACHE_NODIRECT), `sloppiness=file_stat_matches` SEUL (ccache compare encore
le ctime, qu'`os.utime` ne peut pas remettre en place ; seul le COUPLE avec
`file_stat_matches_ctime` se voit ainsi), et les options ignorees autres que
les macros (`ignore_options=-O*`, `-I*`...). L'aller-retour n. 0 ne ferme que
la famille `-D*`. Et le piege tient a un detail qui n'en est pas un : la mtime
FIGEE DANS LE PASSE (voir write_hdr) -- avec une mtime "maintenant", il ne
mord pas du tout.
⇒ **LA GARDE PRINCIPALE EST LA TABLE (point 1)**, l'audit integral de la
configuration. Les deux ne se remplacent pas et ne pesent pas le meme poids.

⛔ Restent invisibles, par construction : un cache qui n'est pas ccache
(sccache, distcc avec cache, icecream) ; tout ce qui change entre la lecture
de `-p` et les compilations reelles de `src/` ; et surtout, LA SONDE AUDITE
L'ENVIRONNEMENT DU TEST, PAS CELUI QUI A COMPILE `src/` -- `make check` tourne
apres `make`, et rien ne garantit que les recettes de compilation ont vu la
meme configuration de cache. C'est la limite la plus large et elle ne se ferme
pas par une sonde.

⛔ TODO -- FAIL-OPEN OUVERT : `CXX` ET `CC` N'ARRIVENT PAS JUSQU'AU TEST.
`tests/Makefile.am` n'exporte que `abs_top_srcdir`, `abs_top_builddir` et
`PYTHON`, et aucun `Makefile` genere n'exporte `CXX` ni `CC`. Sous
`./configure CXX="ccache g++"`, la sonde ne voit donc pas ce `CXX` : elle
retombe sur le `g++` du PATH et audite un exemplaire potentiellement propre
pendant qu'un autre compile. Les canaux qui FONCTIONNENT aujourd'hui sont le
PATH (`g++` lien vers ccache) et `make CXX="ccache g++"` en ligne de commande,
que GNU make exporte de lui-meme. Pour fermer ce trou il faudrait ajouter
`CXX='$(CXX)'; export CXX;` et son equivalent `CC` a AM_TESTS_ENVIRONMENT --
non fait, donc non prouve : ne pas ecrire ici que c'est ferme avant de l'avoir
mesure sur un cache menteur.

Le cache audite est CELUI QUE CXX/CC EMPLOIENT, pas celui que le PATH designe :
la sonde interroge le binaire ccache de la ligne CXX (ou la cible du lien pour
un `g++` qui est un shim). Sans cela, un enrobage `ccache` maison ferait
auditer un ccache propre pendant qu'un autre ment. ⚠️ Un enrobage qui DELEGUE
`-p` honnetement et n'injecte son mensonge qu'a la compilation repond
honnetement a cette interrogation : seule la moitie empirique peut le prendre,
et elle ne prend que ce qu'elle sait faire varier.

Le succes de cache est attribue par CCACHE_STATSLOG (journal PAR INVOCATION),
jamais par `ccache -s` : sur un CCACHE_DIR partage les compteurs de `ccache -s`
sont l'agregat de tous les agents et ne sont attribuables a personne. La sonde
n'appelle JAMAIS `ccache -z` (elle remettrait a zero les compteurs de tous).
"""
import hashlib
import os
import re
import shlex
import shutil
import subprocess
import sys
import tempfile

RC_PASS, RC_FAIL, RC_SKIP = 0, 1, 77

# mtime figee dans le passe : voir write_hdr(). 2020-09-13.
STAMP = 1600000000

# L'extension decide de la langue reellement compilee : un `.c` passe a
# `gcc`, un `.cpp` a `g++`.
CANAUX = (
    ('CXX', ['g++'], '.cpp'),
    ('CC', ['gcc'], '.c'),
)

# --------------------------------------------------------------------------
# LA CONFIGURATION AUDITEE, CLEF PAR CLEF, EN ENTIER.
#
# AUDITED : valeurs EXIGEES. Plusieurs valeurs acceptees quand ccache lui-meme
#           change de defaut d'une version a l'autre, et seulement dans ce cas.
# LIBRES  : clefs dont la VALEUR ne peut pas rendre le cache menteur (chemins,
#           tailles, permissions, compression du stockage). La clef reste
#           ENUMEREE : ce qui est refuse, c'est l'inconnu, pas le variable.
#
# Toute clef publiee par `ccache -p` et absente des DEUX tables ⇒ rc=1.
# Toute clef d'AUDITED absente de `ccache -p` ⇒ rc=1 AUSSI, sauf les trois
# clefs de VERSION_DEPENDANTES ci-dessous (mesure : 4.7.5 et 4.12.3 publient
# 44 clefs chacune, mais pas exactement les memes -- 42 communes).
# --------------------------------------------------------------------------
AUDITED = (
    ('sloppiness', ('',),
     "un cache autorise a ignorer une partie de son entree peut servir "
     "l'objet d'une source differente (file_stat_matches, time_macros, "
     "include_file_mtime...)"),
    ('compiler_check', ('content',),
     "le defaut `mtime` sert un objet perime des qu'un compilateur est "
     "remplace a taille et date egales ; `none` et `string:...` ne "
     "regardent pas le compilateur du tout"),
    ('hash_dir', ('true',),
     "sans le repertoire de compilation dans la clef, l'objet compile au "
     "chemin A est servi au chemin B -- il differe de ce qu'une compilation "
     "propre aurait produit (DW_AT_comp_dir)"),
    ('base_dir', ('',),
     "base_dir reecrit les chemins avant le hachage ; il n'est pas malhonnete "
     "en soi, mais ce n'est pas la configuration auditee et la sonde ne "
     "prononce que sur celle-la"),
    ('ignore_options', ('',),
     "MESURE : `ignore_options=-D*` fait servir l'objet de -DVAL=1 pour une "
     "compilation -DVAL=2 (objets identiques, direct_cache_hit, `mov $0x1` au "
     "desassemblage). Seul l'aller-retour n. 0 le voit sans la table"),
    ('ignore_headers_in_manifest', ('',),
     "un en-tete retire du manifeste n'invalide plus rien quand il change"),
    ('direct_mode', ('true',),
     "en mode preprocesseur seul, la clef ne porte plus sur les fichiers "
     "inclus ; ce n'est pas la configuration auditee, et le piege de l'en-tete "
     "de cette sonde n'y mord plus (il passerait en silence)"),
    ('depend_mode', ('false',),
     "le mode depend hache la ligne de commande et les dependances declarees "
     "plutot que la source preprocessee : autre discipline, non auditee ici"),
    ('hard_link', ('true', 'false'),
     "hard_link relie l'objet du cache au lieu de le copier ; ccache le "
     "documente comme risque si un tiers modifie l'objet en place. La sonde "
     "accepte les deux valeurs mais les EXIGE explicitement"),
    ('prefix_command', ('',),
     "une commande interposee devant le compilateur n'entre pas dans la clef"),
    ('prefix_command_cpp', ('',), "idem pour la passe de preprocesseur"),
    ('compiler', ('',),
     "`compiler` remplace le compilateur reellement invoque : l'objet ne vient "
     "alors pas du compilateur que le build croit employer"),
    ('compiler_type', ('auto',),
     "forcer le type de compilateur change les regles d'analyse de la ligne "
     "de commande"),
    ('disable', ('false',),
     "cache desactive : plus rien n'est servi, le detecteur ne peut plus "
     "mordre (il rendrait PASS en croyant garder quelque chose)"),
    ('read_only', ('false',),
     "en lecture seule rien n'est stocke : le detecteur ne peut plus mordre"),
    ('read_only_direct', ('false',), "idem, restreint au mode direct"),
    ('recache', ('false',),
     "recache force le manque : plus aucun succes de cache, donc plus aucun "
     "detecteur"),
    ('remote_storage', ('',),
     "un stockage distant est un TIERS qui sert des objets ; il n'est pas "
     "audite par cette sonde"),
    ('remote_only', ('false',), "idem : le local n'est plus la source"),
    ('reshare', ('false',), "renvoi vers le stockage distant, non audite"),
    ('path', ('',),
     "`path` remplace le PATH utilise pour trouver le compilateur : le "
     "compilateur reellement employe n'est plus celui du build"),
    ('extra_files_to_hash', ('',),
     "des fichiers supplementaires dans la clef : ce n'est pas la "
     "configuration auditee"),
    ('cpp_extension', ('',), "extension de preprocesseur imposee, non auditee"),
    ('keep_comments_cpp', ('false',),
     "garder les commentaires change ce qui est hache"),
    ('run_second_cpp', ('true',),
     "ccache 4.7 : ne PAS rejouer le preprocesseur est le reglage que ccache "
     "lui-meme signale comme source de resultats faux avec certains "
     "compilateurs"),
    ('pch_external_checksum', ('false',),
     "somme de controle externe pour les en-tetes precompiles : non auditee"),
    ('file_clone', ('false',),
     "clonage COW de l'objet du cache : non audite"),
    ('inode_cache', ('true', 'false'),
     "cache d'empreintes indexe par inode. Le DEFAUT change avec la version "
     "(false en 4.7.5, true en 4.12.3) : les deux valeurs sont acceptees, "
     "mais la clef est EXIGEE dans la table"),
    ('namespace', ('',), "cloisonnement du cache : non audite"),
    ('debug', ('false',), "traces de mise au point : non auditees"),
    ('debug_level', ('2',), "idem"),
    ('stats', ('true',),
     "sans compteurs, CCACHE_STATSLOG est muet et le succes de cache n'est "
     "plus attribuable : la sonde ne pourrait plus prouver que le detecteur "
     "est vivant"),
    ('absolute_paths_in_stderr', ('false',),
     "reecriture des chemins dans stderr : non auditee"),
    ('response_file_format', ('auto',),
     "format des fichiers de reponse : non audite"),
)

LIBRES = (
    ('cache_dir', "chemin du cache -- un CCACHE_DIR par agent est RECOMMANDE"),
    ('temporary_dir', "chemin de travail"),
    ('debug_dir', "chemin des traces"),
    ('log_file', "chemin du journal"),
    ('stats_log', "chemin du journal par invocation"),
    ('max_size', "quota du STOCKAGE : il ne change pas ce qui est servi. "
                 "⚠️ MESURE : meme a `1 kB` la sonde rend 0 avec un succes de "
                 "cache attribue -- l'eviction n'est pas immediate, un quota "
                 "minuscule NE TUE PAS le detecteur et le point 2 ne le voit "
                 "pas. Ce qu'il attrape est `disable`, `recache`, `read_only`"),
    ('max_files', "idem : quota du stockage"),
    ('limit_multiple', "politique d'eviction (ccache 4.7)"),
    ('umask', "permissions des fichiers du cache"),
    ('compression', "compression du STOCKAGE : l'objet rendu est identique"),
    ('compression_level', "idem"),
    ('msvc_dep_prefix', "chaine propre a MSVC, sans effet sur gcc/clang"),
)

# Les clefs d'AUDITED que ccache ne publie PAS dans toutes les versions
# supportees (4.7.5 et 4.12.3 publient 44 clefs chacune, 42 communes). Une
# clef d'AUDITED absente de `-p` ET absente de cette table rend 1 : c'est
# ainsi qu'une clef EXIGEE qui DISPARAIT se signale.
VERSION_DEPENDANTES = {
    'run_second_cpp':
        "publiee par ccache 4.7.5, RETIREE en 4.12.3 (le second passage de "
        "preprocesseur n'est plus optionnel)",
    'debug_level':
        "absente de ccache 4.7.5, publiee a partir de 4.8",
    'response_file_format':
        "absente de ccache 4.7.5, publiee a partir de 4.10",
}

AUDITED_MAP = dict((k, (vals, why)) for k, vals, why in AUDITED)
LIBRES_MAP = dict(LIBRES)

TROIS_ISSUES = """TROIS issues, au choix :
  (1) jouer scripts/ccache-setup.sh (il ecrit la configuration auditee dans le
      CCACHE_DIR ; sans CCACHE_DIR il emploie celui que ccache declare) ;
  (2) poser les variables CCACHE_* dans l'environnement -- elles ne persistent
      rien : CCACHE_SLOPPINESS= CCACHE_COMPILERCHECK=content
      CCACHE_HASHDIR=true CCACHE_BASEDIR= ... ;
  (3) retirer le repertoire de shims du PATH (ou CXX=/usr/bin/g++ CC=/usr/bin/gcc) :
      plus aucun cache en service, la sonde rend 77 -- un SKIP propre.
⚠️ Sur Fedora et Gentoo, /usr/lib64/ccache (resp. /usr/lib/ccache) est dans le
PATH PAR DEFAUT et `g++` y est un lien vers ccache : un arbre neuf y trouve donc
un cache EN SERVICE et NON CONFIGURE, et `make check` est ROUGE sans que
personne n'ait rien allume. C'est voulu -- le defaut de ccache n'est pas sur, il
est seulement courant -- mais il faut le savoir."""

# Le compromis de la table figee, ecrit sur l'ECART DE VALEUR et non sur la
# clef inconnue (la, le remede est evident : auditer la clef neuve). Quand
# l'ecart porte une origine `(default)`, personne n'a rien regle -- c'est
# ccache qui a change de defaut, et aucune des trois issues n'y peut rien.
COMPROMIS_TABLE = """⭐ AVANT LES TROIS ISSUES, LIRE L'ORIGINE DE L'ECART -- elle est imprimee
ci-dessus, telle que `ccache -p` la publie :
  * origine `(environment)` ou un chemin de `ccache.conf` : QUELQU'UN A REGLE
    cette valeur. Les trois issues ci-dessous la corrigent.
  * ⭐ origine `(default)` : PERSONNE n'a rien regle -- c'est le DEFAUT de
    cette version de ccache qui n'est pas celui qui a ete audite. Aucune des
    trois issues ne corrige cela, et il ne faut PAS chercher qui a mal
    configure la machine. Le remede est la TABLE de ce fichier : verifier ce
    que le nouveau defaut fait vraiment, puis soit l'ajouter aux valeurs
    acceptees de la clef (comme `hard_link` et `inode_cache`, dont le defaut
    change deja d'une version a l'autre), soit le refuser SCIEMMENT en
    l'ecrivant. C'est le prix assume d'une table figee : elle NOMME la clef et
    survit a un changement de version, mais elle demande une relecture humaine
    quand ccache bouge."""


def emit(tag, msg):
    print('SONDE-CCACHE: %s -- %s' % (tag, msg))


def run(args, **kw):
    try:
        return subprocess.run(args, capture_output=True, text=True, **kw)
    except OSError as exc:
        return subprocess.CompletedProcess(args, 127, '', str(exc))


def die(title, msg):
    emit('ECHEC', title)
    print(msg)
    print("Ne pas conclure d'une campagne de mutations, ni d'aucun `make "
          "check`, tant que ceci n'est pas corrige (rc=1).")
    sys.exit(RC_FAIL)


def cache_driver(cxx):
    """Le binaire ccache que CXX (ou CC) emploie REELLEMENT, ou None.

    Ce n'est pas `which('ccache')` : un enrobage maison nomme `ccache` qui
    injecte CCACHE_IGNOREOPTIONS ferait auditer le ccache du PATH -- propre --
    pendant qu'un autre ment. On interroge donc le mot de la ligne de commande
    lui-meme, et pour un `g++` qui est un shim, la cible du lien.

    ⚠️ Un enrobage qui DELEGUE `-p` au vrai ccache repond honnetement a cette
    interrogation et n'injecte son mensonge qu'a la compilation : lui, seule
    la moitie empirique peut le prendre, et seulement sur ce qu'elle sait
    faire varier.
    """
    for word in cxx:
        if os.path.basename(word) == 'ccache':
            return shutil.which(word) or word
    prog = shutil.which(cxx[0])
    if prog:
        real = os.path.realpath(prog)
        if os.path.basename(real) == 'ccache':
            return real
    return None


def ccache_in_use(cxx):
    """cxx est la ligne de commande COMPLETE, pas son premier mot."""
    if cache_driver(cxx):
        return True
    r = run(cxx + ['--version'])
    return 'ccache' in (r.stdout + r.stderr).lower()


def read_config(driver, env):
    """Rend {clef: (valeur, origine)} -- l'ORIGINE est publiee par `-p`."""
    r = run([driver, '-p'], env=env)
    if r.returncode != 0:
        die('CONFIGURATION NON AUDITABLE',
            "un cache est en service mais `%s -p` a echoue (%s). La sonde "
            "ne peut rien garantir." % (driver,
                                        r.stderr.strip()[:200] or r.returncode))
    conf = {}
    for line in r.stdout.splitlines():
        m = re.match(r'^(?:\((?P<orig>[^)]*)\)\s*)?([A-Za-z_]\w*)\s*=\s*(.*)$',
                     line)
        if m:
            conf[m.group(2)] = (m.group(3).strip(), m.group('orig') or '?')
    if not conf:
        die('CONFIGURATION NON AUDITABLE',
            "`%s -p` n'a publie aucun reglage lisible." % driver)
    return conf


def check_config(conf, driver, canal):
    """Audit INTEGRAL, dans les DEUX SENS.

    (a) chaque clef PUBLIEE doit etre couverte par la table ;
    (b) chaque clef EXIGEE par la table doit etre PUBLIEE -- passer en
        silence sur une clef absente laisserait une clef qui DISPARAIT cesser
        d'etre auditee sans que rien ne le dise ;
    (c) chaque clef publiee ET exigee doit avoir une valeur auditee.
    """
    ou = '(canal %s, %s)' % (canal, driver)

    inconnues = sorted(k for k in conf
                       if k not in AUDITED_MAP and k not in LIBRES_MAP)
    if inconnues:
        die('CONFIGURATION NON AUDITEE (clef inconnue de la table) %s' % ou,
            "`%s -p` publie %d reglage(s) que la table d'audit ne connait "
            "pas : %s\nUne table qui enumere des clefs est toujours en retard "
            "d'une clef -- c'est pourquoi la sonde demande a l'OUTIL ce qu'il "
            "publie et refuse tout ce qu'elle n'a pas audite. Auditer ces "
            "clefs (valeur exigee + motif) dans scripts/ccache-honesty-probe.py "
            "avant de conclure quoi que ce soit d'un `make check`."
            % (driver, len(inconnues), ', '.join(inconnues)))

    disparues = sorted(k for k in AUDITED_MAP
                       if k not in conf and k not in VERSION_DEPENDANTES)
    if disparues:
        lignes = ["  %s -- exigee %s\n    %s"
                  % (k, ' ou '.join(repr(v) for v in AUDITED_MAP[k][0]),
                     AUDITED_MAP[k][1])
                  for k in disparues]
        die('CONFIGURATION NON AUDITEE (%d clef(s) EXIGEE(S) DISPARUE(S)) %s'
            % (len(disparues), ou),
            "la table exige ces clefs, et `%s -p` ne les publie PLUS :\n%s\n"
            "⚠️ Une clef qui disparait de `-p` n'est plus auditee : la sonde "
            "passait dessus en silence, ce qui rendait la table fail-open sur "
            "tout reglage retire ou masque. Deux causes possibles, et il faut "
            "trancher AVANT de conclure quoi que ce soit :\n"
            "  * une version de ccache qui a retire la clef ⇒ l'ajouter a "
            "VERSION_DEPENDANTES dans scripts/ccache-honesty-probe.py, en "
            "disant a partir de QUELLE version, apres avoir verifie ce que "
            "son retrait change ;\n"
            "  * un enrobage ou un filtre qui MASQUE la clef dans la sortie "
            "de `-p` ⇒ c'est un mensonge, et rien ne doit etre conclu."
            % (driver, '\n'.join(lignes)))

    ecarts = []
    for key, (vals, why) in sorted(AUDITED_MAP.items()):
        if key not in conf:
            continue
        got, orig = conf[key]
        if got not in vals:
            ecarts.append((key, got, orig, vals, why))
    if ecarts:
        lignes = []
        for key, got, orig, vals, why in ecarts:
            lignes.append("  %s = %r  [origine : %s], attendu %s\n    %s"
                          % (key, got, orig,
                             ' ou '.join(repr(v) for v in vals), why))
        die('CONFIGURATION NON AUDITEE (%d ecart(s), 1er : %s) %s'
            % (len(ecarts), ecarts[0][0], ou),
            "la configuration PUBLIEE par `%s -p` n'est pas celle qui a ete "
            "auditee :\n%s\n%s\n%s"
            % (driver, '\n'.join(lignes), COMPROMIS_TABLE, TROIS_ISSUES))


def fingerprint(conf):
    """Empreinte de la configuration PUBLIEE, dans son entier.

    Les clefs libres entrent par leur NOM seul (leur valeur est un chemin ou un
    quota, elle change legitimement d'un agent a l'autre) ; les clefs auditees
    entrent avec leur valeur. Deux arbres qui affichent la meme empreinte ont
    exactement la meme configuration de cache.
    """
    items = []
    for k in sorted(conf):
        items.append('%s=%s' % (k, '<libre>' if k in LIBRES_MAP else conf[k][0]))
    return hashlib.sha256('\n'.join(items).encode('utf-8')).hexdigest()


class Compiler(object):
    """Compile, et dit si CETTE invocation-la a ete servie par le cache."""

    def __init__(self, canal, cxx, workdir):
        self.canal, self.cxx, self.workdir, self.n = canal, cxx, workdir, 0

    def __call__(self, src, out, extra=()):
        self.n += 1
        statslog = os.path.join(self.workdir, 'stats-%d.log' % self.n)
        env = dict(os.environ)
        env['CCACHE_STATSLOG'] = statslog
        obj = os.path.join(self.workdir, out)
        r = run(self.cxx + ['-g', '-O2'] + list(extra) + ['-c', src, '-o', obj],
                cwd=self.workdir, env=env)
        if r.returncode != 0:
            die('LA SONDE NE COMPILE PAS (canal %s)' % self.canal,
                "un cache de compilation EST en service, mais `%s` ne compile "
                "pas une unite triviale :\n%s\nUne sonde qui ne tourne pas ne "
                "garde rien ; ce n'est PAS un SKIP."
                % (' '.join(self.cxx), r.stderr.strip()[:400]))
        with open(obj, 'rb') as fh:
            digest = hashlib.sha256(fh.read()).hexdigest()
        return digest, self._hit(statslog)

    @staticmethod
    def _hit(statslog):
        """True/False/None -- journal PAR INVOCATION, jamais `ccache -s`."""
        try:
            with open(statslog) as fh:
                lignes = [l.strip() for l in fh
                          if l.strip() and not l.startswith('#')]
        except OSError:
            return None
        if not lignes:
            return None
        return any('cache_hit' in l for l in lignes)


def exiger_succes(hit, quoi):
    """Un detecteur qui ne peut pas mordre doit ECHOUER, pas reussir."""
    if hit is True:
        return
    if hit is False:
        die('DETECTEUR DE CACHE MORT',
            "%s n'a PAS ete servi par le cache (journal par invocation : "
            "aucun cache_hit). Le cache ne sert donc rien, et l'aller-retour "
            "qui suit ne peut RIEN detecter : un cache menteur passerait "
            "inapercu.\nCauses mesurees : CCACHE_DISABLE=1, recache, "
            "read_only.\n"
            "⚠️ Deux causes que ce point NE voit PAS, et il faut le savoir : "
            "un CCACHE_DIR non ecrivable est pris AVANT, par `la sonde ne "
            "compile pas` ; et un `max_size` minuscule n'est PAS pris du tout "
            "-- mesure : a `max_size=1k` la sonde rend 0 avec un cache_hit "
            "attribue, l'eviction n'etant pas immediate.\n"
            "⚠️ Une sonde qui annonce \"le piege est arme\" sans jamais le "
            "verifier ne garde rien." % quoi)
    die('SUCCES DE CACHE NON ATTRIBUABLE',
        "%s : CCACHE_STATSLOG n'a rien ecrit, le succes de cache n'est pas "
        "attribuable et la sonde ne peut pas prouver que son detecteur est "
        "vivant. Fermeture par defaut." % quoi)


def auditer_canal(canal, cmd, ext, workdir):
    """Les TROIS aller-retours sur un canal (CXX ou CC). Rend un resume."""
    compile_ = Compiler(canal, cmd, workdir)
    j = lambda n: os.path.join(workdir, n)

    # --- Aller-retour n.0 : une MACRO EN LIGNE DE COMMANDE. -----------------
    # Seul chemin empirique par lequel `ignore_options=-D*` se manifeste, et
    # seul qui prenne un enrobage qui delegue `-p` honnetement mais injecte
    # CCACHE_IGNOREOPTIONS a la compilation. Aucun aller-retour sur les
    # FICHIERS ne peut le voir.
    osrc = j('probe_opt' + ext)
    with open(osrc, 'w') as fh:
        fh.write('int calaos_ccache_probe_opt(void){ return CALAOS_PROBE_OPT; }\n')
    o1, _ = compile_(osrc, 'o1.o', ['-DCALAOS_PROBE_OPT=1'])
    o2, _ = compile_(osrc, 'o2.o', ['-DCALAOS_PROBE_OPT=2'])
    if o1 == o2:
        die('CACHE DE COMPILATION MALHONNETE (canal %s)' % canal,
            "la MEME source compilee avec -DCALAOS_PROBE_OPT=1 puis =2 rend le "
            "MEME objet (%s) : le cache ignore une option qui change le code "
            "produit et sert l'objet de 1 pour une compilation qui vaut 2.\n"
            "Cause mesuree : `ignore_options=-D*` dans la configuration, OU "
            "CCACHE_IGNOREOPTIONS=-D* injecte a la compilation par un enrobage "
            "nomme `ccache` qui repond honnetement a `-p`." % o1[:16])

    # --- Aller-retour n.1 : un EN-TETE, a taille ET mtime egales. -----------
    # C'est le SEUL chemin par lequel file_stat_matches,
    # file_stat_matches_ctime se manifeste. Les deux corps font exactement le
    # meme nombre d'octets, et la mtime est remise a sa valeur d'origine : un
    # cache qui se fie a (taille, mtime) servira l'objet de VAL=1 pour VAL=2.
    hdr = j('probe_hdr.h')
    hsrc = j('probe_hdr' + ext)
    with open(hsrc, 'w') as fh:
        fh.write('#include "probe_hdr.h"\n'
                 'int calaos_ccache_probe_hdr(void){ return CALAOS_PROBE_VAL; }\n')

    def write_hdr(val):
        """Meme taille a l'octet pres, et TOUJOURS la meme mtime.

        La mtime est figee dans le PASSE (STAMP) et non "maintenant" :
        ccache refuse de se fier a (taille, mtime) pour un fichier modifie
        trop recemment : avec une mtime "maintenant", le piege ne mord PAS et
        la sonde rendrait PASS sur un cache menteur. Ce n'est pas un detail,
        c'est TOUT le piege.
        """
        with open(hdr, 'w') as fh:
            fh.write('#define CALAOS_PROBE_VAL %d\n' % val)
        os.utime(hdr, (STAMP, STAMP))

    write_hdr(1)
    st1 = os.stat(hdr)
    h1, _ = compile_(hsrc, 'h1.o')
    write_hdr(2)
    st2 = os.stat(hdr)
    # AUTO-VERIFICATION DU PIEGE : sans mtime figee, les deux mtimes
    # different, le piege n'est PAS arme, et la sonde imprime "honnete" sur un
    # cache menteur. Elle le CONSTATE plutot que de l'annoncer.
    if st2.st_size != st1.st_size or st2.st_mtime != st1.st_mtime:
        die('SONDE INVALIDE (le piege n est pas arme, canal %s)' % canal,
            "les deux en-tetes n'ont pas la meme (taille, mtime) : "
            "taille %d/%d, mtime %r/%r. Le piege file_stat_matches n'est "
            "pas arme et la sonde ne peut RIEN detecter -- elle rendrait "
            "PASS sur un cache menteur (mesure : 6/6)."
            % (st1.st_size, st2.st_size, st1.st_mtime, st2.st_mtime))
    h2, _ = compile_(hsrc, 'h2.o')
    if h2 == h1:
        die('CACHE DE COMPILATION MALHONNETE (canal %s)' % canal,
            "un EN-TETE modifie a taille et mtime EGALES a rendu le MEME "
            "objet (%s) : le cache a servi l'objet de CALAOS_PROBE_VAL=1 "
            "pour une source qui vaut 2. C'est exactement le faux vert "
            "qu'une campagne de mutations ne peut pas voir.\n"
            "Cause la plus probable : sloppiness contient "
            "file_stat_matches,file_stat_matches_ctime." % h1[:16])
    write_hdr(1)
    h3, hit3 = compile_(hsrc, 'h3.o')
    # ⭐ LE POINT (b) : l'en-tete restaure a l'identique DOIT etre servi par le
    # cache. Sinon le cache ne sert rien et les comparaisons ci-dessus n'ont
    # rien pu detecter.
    exiger_succes(hit3, "canal %s : l'en-tete restaure a l'identique" % canal)
    if h3 != h1:
        die('CACHE DE COMPILATION MALHONNETE (canal %s)' % canal,
            "l'en-tete restaure a l'identique rend un objet DIFFERENT "
            "(%s != %s) : le cache ne reproduit pas ce que le compilateur "
            "produit." % (h3[:16], h1[:16]))

    # --- Aller-retour n.2 : la SOURCE, A -> B -> A. -------------------------
    src = j('probe' + ext)
    A = 'int calaos_ccache_probe(void){ return 1; }\n'
    B = 'int calaos_ccache_probe(void){ return 2; }\n'

    def put(text):
        with open(src, 'w') as fh:
            fh.write(text)

    put(A)
    hA, _ = compile_(src, 'a.o')
    put(B)
    hB, _ = compile_(src, 'b.o')
    put(A)
    hA2, hitA2 = compile_(src, 'a2.o')
    exiger_succes(hitA2, "canal %s : la source A rejouee apres B" % canal)
    if hA == hB:
        die('CACHE DE COMPILATION MALHONNETE (canal %s)' % canal,
            "deux sources DIFFERENTES (`return 1` et `return 2`) rendent le "
            "MEME objet (%s). Ce n'est pas une sonde non concluante : c'est le "
            "mensonge lui-meme." % hA[:16])
    if hA2 != hA:
        die('CACHE DE COMPILATION MALHONNETE (canal %s)' % canal,
            "la source A recompilee apres B rend un objet DIFFERENT (%s != %s)."
            % (hA2[:16], hA[:16]))

    return ('%s=%s : macro -D %s != %s ; en-tete taille+mtime egales %s != %s, '
            'restaure SERVI PAR LE CACHE ; source A/B/A %s != %s, rejoue %s '
            'SERVI PAR LE CACHE'
            % (canal, ' '.join(cmd), o1[:12], o2[:12], h1[:12], h2[:12],
               hA[:12], hB[:12], hA2[:12]))


def main():
    strict = os.environ.get('CALAOS_CCACHE_PROBE_STRICT', '') not in ('', '0')

    # Les DEUX canaux : `configure.ac` appelle AC_PROG_CC autant qu'AC_PROG_CXX
    # et l'arbre porte 12 fichiers `.c` -- auditer CXX seul laisse la moitie du
    # build sans garde.
    canaux = []
    for nom, defaut, ext in CANAUX:
        cmd = shlex.split(os.environ.get(nom) or '') or list(defaut)
        canaux.append((nom, cmd, ext))

    actifs = [(nom, cmd, ext) for nom, cmd, ext in canaux
              if ccache_in_use(cmd)]

    if not actifs:
        msg = ("AUCUN cache de compilation en service, ni sur CXX ni sur CC "
               "(%s). Rien a garder, rien n'est garanti : ce n'est NI un PASS "
               "NI une preuve."
               % ', '.join('%s=%s' % (n, ' '.join(c)) for n, c, _ in canaux))
        if strict:
            die('SKIP INTERDIT (CALAOS_CCACHE_PROBE_STRICT=1)',
                msg + "\nUn arbre qui sert a juger une campagne doit rendre 0, "
                      "pas SKIP (T3.51, condition 7).")
        emit('SKIP', msg)
        sys.exit(RC_SKIP)

    resumes, confs = [], []
    with tempfile.TemporaryDirectory(prefix='ccache-probe-') as racine:
        for nom, cmd, ext in actifs:
            driver = cache_driver(cmd) or shutil.which('ccache')
            if not driver:
                die('CONFIGURATION NON AUDITABLE (canal %s)' % nom,
                    "un cache est en service via %s mais le binaire `ccache` "
                    "est introuvable : sa configuration ne peut pas etre lue."
                    % ' '.join(cmd))
            conf = read_config(driver, dict(os.environ))
            check_config(conf, driver, nom)
            confs.append((nom, driver, conf))

            workdir = os.path.join(racine, nom)
            os.makedirs(workdir)
            resumes.append(auditer_canal(nom, cmd, ext, workdir))

    emit('PASS', ' | '.join(resumes))
    for nom, driver, conf in confs:
        print('SONDE-CCACHE: config auditee (canal %s) -- %d reglages publies '
              'par `%s -p`, %d audites par valeur, %d libres, 0 inconnu, '
              '0 exigee disparue ; empreinte %s'
              % (nom, len(conf), driver,
                 len([k for k in conf if k in AUDITED_MAP]),
                 len([k for k in conf if k in LIBRES_MAP]),
                 fingerprint(conf)[:16]))
    inactifs = [n for n, _, _ in canaux if n not in [a[0] for a in actifs]]
    if inactifs:
        print('SONDE-CCACHE: canal(aux) SANS cache en service, donc non '
              'audite(s) : %s -- ce n est PAS une garantie a leur sujet.'
              % ', '.join(inactifs))
    sys.exit(RC_PASS)


try:
    main()
except SystemExit:
    raise
except Exception as exc:  # fermeture par defaut : jamais un PASS silencieux
    emit('ECHEC', 'la sonde a leve une exception : %r' % (exc,))
    print("Une sonde qui plante ne garde rien. rc=1, jamais 77.")
    sys.exit(RC_FAIL)
