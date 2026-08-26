#!/usr/bin/env python3
"""Sonde d'honnetete du cache de compilation (ccache).

Un cache malhonnete rend un objet qui ne correspond pas a la source compilee.
Il rendrait vertes des campagnes de mutations entieres et invaliderait tout
verdict tire d'un `make check`. Cette sonde est le controle permanent de cet
outil ; elle est cablee dans TESTS.

TROIS codes de sortie, et AUCUN mode d'echec ne rend 0 :

  0  PASS  -- un cache est en service, la configuration qu'il PUBLIE est
              EXACTEMENT celle qui a ete auditee (toutes les clefs, pas
              quelques-unes), un succes de cache a ete CONSTATE, et les deux
              aller-retours empiriques sont honnetes. La ligne imprimee
              commence par "SONDE-CCACHE: PASS".

  77 SKIP  -- aucun cache de compilation n'est en service. C'est le SEUL motif
              de SKIP qui subsiste. La ligne commence par "SONDE-CCACHE: SKIP"
              et le dit en toutes lettres, pour qu'un SKIP ne puisse plus etre
              lu comme un PASS dans un journal.
              CALAOS_CCACHE_PROBE_STRICT=1 transforme ce SKIP en ECHEC : c'est
              la condition (7) de T3.51 (un arbre qui sert a juger une campagne
              doit rendre 0, pas SKIP).

  1  ECHEC -- TOUT le reste. Cache malhonnete, configuration non auditee,
              detecteur qui ne peut pas mordre, sonde qui ne compile pas,
              resultat non concluant, exception imprevue.
              Fermeture par defaut : ce qui n'est pas prouve honnete est
              declare faux. La sonde n'a le droit de se taire que quand il n'y
              a rien a garder.

Ce que la sonde verifie, dans l'ordre :

  1. LA CONFIGURATION ENTIERE, telle que l'outil la publie. Ni liste noire, ni
     liste blanche de quelques clefs : les deux sont toujours en retard d'une
     clef. `ccache -p` ENUMERE lui-meme sa configuration ; la sonde exige que
     CHAQUE clef publiee soit couverte par la table ci-dessous. Une clef
     publiee que la table ne connait pas ⇒ ECHEC, en la nommant : c'est ainsi
     qu'une version ulterieure de ccache qui ajoute un reglage dangereux se
     signale toute seule, au lieu de passer.
     (C'est ce qui a manque : une "liste blanche" de quatre clefs laissait
     passer `ignore_options=-D*`, qui fait servir l'objet de -DVAL=1 pour
     -DVAL=2 -- verifie au desassemblage.)
  2. Un SUCCES DE CACHE CONSTATE. Le detecteur du point 3 ne peut mordre que
     si le cache sert vraiment quelque chose ; `disable`, `recache`,
     `read_only`, un `max_size` minuscule ou un CCACHE_DIR non ecrivable le
     rendent STRUCTURELLEMENT MORT. Un detecteur qui ne peut pas mordre doit
     ECHOUER, pas reussir : si l'en-tete restaure a l'identique n'est pas servi
     PAR LE CACHE, la sonde rend 1.
  3. Aller-retour sur un EN-TETE, taille et mtime preservees, puis aller-retour
     sur la SOURCE A -> B -> A. obj(A) == obj(B) n'est pas "non concluant" :
     c'est exactement le mensonge, et c'est un ECHEC.

⚠️ La moitie empirique (2+3) est un DETECTEUR PAR ECHANTILLON : elle prouve
qu'un mensonge s'est produit, jamais qu'aucun ne peut se produire. Elle ne voit
NI les options de compilation ignorees (`ignore_options`), NI le mode
preprocesseur (`direct_mode=false`), NI `sloppiness=file_stat_matches` SEUL
(ccache compare encore le ctime, que `os.utime` ne peut pas remettre en place ;
seul le couple `file_stat_matches,file_stat_matches_ctime` se voit ainsi).
Et elle depend d'un detail qui n'en est pas un : la mtime FIGEE DANS LE PASSE
(voir write_hdr) -- avec une mtime "maintenant", le piege ne mord PAS et la
sonde passerait en silence sur un cache menteur.
⇒ **LA GARDE PRINCIPALE EST LE POINT 1**, l'audit integral de la configuration.
Les deux ne se remplacent pas.

Le cache audite est CELUI QUE CXX EMPLOIE, pas celui que le PATH designe : la
sonde interroge le binaire ccache de la ligne CXX (ou la cible du lien pour un
`g++` qui est un shim). Sans cela, un enrobage `ccache` maison qui injecte
CCACHE_IGNOREOPTIONS ferait auditer un ccache propre pendant qu'un autre ment.

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
     "desassemblage). Aucun aller-retour sur les FICHIERS ne peut le voir"),
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
    ('max_size', "quota -- ne change pas ce qui est servi ; un quota trop "
                 "petit tue le detecteur, et c'est le point 2 qui le voit"),
    ('max_files', "idem"),
    ('limit_multiple', "politique d'eviction (ccache 4.7)"),
    ('umask', "permissions des fichiers du cache"),
    ('compression', "compression du STOCKAGE : l'objet rendu est identique"),
    ('compression_level', "idem"),
    ('msvc_dep_prefix', "chaine propre a MSVC, sans effet sur gcc/clang"),
)

AUDITED_MAP = dict((k, (vals, why)) for k, vals, why in AUDITED)
LIBRES_MAP = dict(LIBRES)

TROIS_ISSUES = """TROIS issues, au choix :
  (1) jouer scripts/ccache-setup.sh (il ecrit la configuration auditee dans le
      CCACHE_DIR ; sans CCACHE_DIR il emploie celui que ccache declare) ;
  (2) poser les variables CCACHE_* dans l'environnement -- elles ne persistent
      rien : CCACHE_SLOPPINESS= CCACHE_COMPILERCHECK=content
      CCACHE_HASHDIR=true CCACHE_BASEDIR= ... ;
  (3) retirer le repertoire de shims du PATH (ou CXX=/usr/bin/g++) : plus aucun
      cache en service, la sonde rend 77 -- un SKIP propre.
⚠️ Sur Fedora et Gentoo, /usr/lib64/ccache (resp. /usr/lib/ccache) est dans le
PATH PAR DEFAUT et `g++` y est un lien vers ccache : un arbre neuf y trouve donc
un cache EN SERVICE et NON CONFIGURE, et `make check` est ROUGE sans que
personne n'ait rien allume. C'est voulu -- le defaut de ccache n'est pas sur, il
est seulement courant -- mais il faut le savoir."""


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
    """Le binaire ccache que CXX emploie REELLEMENT, ou None.

    Ce n'est pas `which('ccache')` : un enrobage maison nomme `ccache` qui
    injecte CCACHE_IGNOREOPTIONS ferait auditer le ccache du PATH -- propre --
    pendant qu'un autre ment. On interroge donc le mot de CXX lui-meme (un
    enrobage repond a `-p` avec l'environnement qu'il injecte), et pour un
    `g++` qui est un shim, la cible du lien.
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
    r = run([driver, '-p'], env=env)
    if r.returncode != 0:
        die('CONFIGURATION NON AUDITABLE',
            "un cache est en service mais `%s -p` a echoue (%s). La sonde "
            "ne peut rien garantir." % (driver,
                                        r.stderr.strip()[:200] or r.returncode))
    conf = {}
    for line in r.stdout.splitlines():
        m = re.match(r'^(?:\([^)]*\)\s*)?([A-Za-z_]\w*)\s*=\s*(.*)$', line)
        if m:
            conf[m.group(1)] = m.group(2).strip()
    if not conf:
        die('CONFIGURATION NON AUDITABLE',
            "`%s -p` n'a publie aucun reglage lisible." % driver)
    return conf


def check_config(conf, driver):
    """Audit INTEGRAL : chaque clef publiee doit etre couverte."""
    inconnues = sorted(k for k in conf
                       if k not in AUDITED_MAP and k not in LIBRES_MAP)
    if inconnues:
        die('CONFIGURATION NON AUDITEE (clef inconnue de la table)',
            "`%s -p` publie %d reglage(s) que la table d'audit ne connait "
            "pas : %s\nUne table qui enumere des clefs est toujours en retard "
            "d'une clef -- c'est pourquoi la sonde demande a l'OUTIL ce qu'il "
            "publie et refuse tout ce qu'elle n'a pas audite. Auditer ces "
            "clefs (valeur exigee + motif) dans scripts/ccache-honesty-probe.py "
            "avant de conclure quoi que ce soit d'un `make check`."
            % (driver, len(inconnues), ', '.join(inconnues)))
    ecarts = []
    for key, (vals, why) in sorted(AUDITED_MAP.items()):
        if key not in conf:
            continue
        if conf[key] not in vals:
            ecarts.append((key, conf[key], vals, why))
    if ecarts:
        lignes = []
        for key, got, vals, why in ecarts:
            lignes.append("  %s = %r, attendu %s\n    %s"
                          % (key, got, ' ou '.join(repr(v) for v in vals), why))
        die('CONFIGURATION NON AUDITEE (%d ecart(s), 1er : %s)'
            % (len(ecarts), ecarts[0][0]),
            "la configuration PUBLIEE par `%s -p` n'est pas celle qui a ete "
            "auditee :\n%s\n%s" % (driver, '\n'.join(lignes), TROIS_ISSUES))


def fingerprint(conf):
    """Empreinte de la configuration PUBLIEE, dans son entier.

    Les clefs libres entrent par leur NOM seul (leur valeur est un chemin ou un
    quota, elle change legitimement d'un agent a l'autre) ; les clefs auditees
    entrent avec leur valeur. Deux arbres qui affichent la meme empreinte ont
    exactement la meme configuration de cache.
    """
    items = []
    for k in sorted(conf):
        items.append('%s=%s' % (k, '<libre>' if k in LIBRES_MAP else conf[k]))
    return hashlib.sha256('\n'.join(items).encode('utf-8')).hexdigest()


class Compiler(object):
    """Compile, et dit si CETTE invocation-la a ete servie par le cache."""

    def __init__(self, cxx, workdir):
        self.cxx, self.workdir, self.n = cxx, workdir, 0

    def __call__(self, src, out):
        self.n += 1
        statslog = os.path.join(self.workdir, 'stats-%d.log' % self.n)
        env = dict(os.environ)
        env['CCACHE_STATSLOG'] = statslog
        obj = os.path.join(self.workdir, out)
        r = run(self.cxx + ['-g', '-O2', '-c', src, '-o', obj],
                cwd=self.workdir, env=env)
        if r.returncode != 0:
            die('LA SONDE NE COMPILE PAS',
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
            "inapercu.\nCauses mesurees, toutes rendues 0 par la sonde "
            "precedente : CCACHE_DISABLE=1, recache, read_only, max_size "
            "minuscule, CCACHE_DIR non ecrivable.\n"
            "⚠️ Une sonde qui annonce \"le piege est arme\" sans jamais le "
            "verifier ne garde rien." % quoi)
    die('SUCCES DE CACHE NON ATTRIBUABLE',
        "%s : CCACHE_STATSLOG n'a rien ecrit, le succes de cache n'est pas "
        "attribuable et la sonde ne peut pas prouver que son detecteur est "
        "vivant. Fermeture par defaut." % quoi)


def main():
    cxx = shlex.split(os.environ.get('CXX') or '') or ['g++']
    strict = os.environ.get('CALAOS_CCACHE_PROBE_STRICT', '') not in ('', '0')

    if not ccache_in_use(cxx):
        msg = ("AUCUN cache de compilation en service (CXX=%s). Rien a garder, "
               "rien n'est garanti : ce n'est NI un PASS NI une preuve."
               % ' '.join(cxx))
        if strict:
            die('SKIP INTERDIT (CALAOS_CCACHE_PROBE_STRICT=1)',
                msg + "\nUn arbre qui sert a juger une campagne doit rendre 0, "
                      "pas SKIP (T3.51, condition 7).")
        emit('SKIP', msg)
        sys.exit(RC_SKIP)

    driver = cache_driver(cxx) or shutil.which('ccache')
    if not driver:
        die('CONFIGURATION NON AUDITABLE',
            "un cache est en service via %s mais le binaire `ccache` est "
            "introuvable : sa configuration ne peut pas etre lue."
            % ' '.join(cxx))

    conf = read_config(driver, dict(os.environ))
    check_config(conf, driver)
    empreinte = fingerprint(conf)

    with tempfile.TemporaryDirectory(prefix='ccache-probe-') as d:
        compile_ = Compiler(cxx, d)

        # --- Aller-retour n.1 : un EN-TETE, a taille ET mtime egales. -------
        # C'est le SEUL chemin par lequel file_stat_matches,
        # file_stat_matches_ctime se manifeste. Les deux corps font exactement
        # le meme nombre d'octets, et la mtime est remise a sa valeur
        # d'origine : un cache qui se fie a (taille, mtime) servira l'objet de
        # VAL=1 pour VAL=2.
        hdr = os.path.join(d, 'probe_hdr.h')
        hsrc = os.path.join(d, 'probe_hdr.cpp')
        with open(hsrc, 'w') as fh:
            fh.write('#include "probe_hdr.h"\n'
                     'int calaos_ccache_probe_hdr(){ return CALAOS_PROBE_VAL; }\n')

        def write_hdr(val):
            """Meme taille a l'octet pres, et TOUJOURS la meme mtime.

            La mtime est figee dans le PASSE (STAMP) et non "maintenant" :
            ccache refuse de se fier a (taille, mtime) pour un fichier modifie
            trop recemment. MESURE : avec une mtime "maintenant", le piege ne
            mord PAS (6/6 PASS silencieux sur un cache menteur) ; figee dans le
            passe, il mord 6/6. Ce n'est pas un detail, c'est TOUT le piege.
            """
            with open(hdr, 'w') as fh:
                fh.write('#define CALAOS_PROBE_VAL %d\n' % val)
            os.utime(hdr, (STAMP, STAMP))

        write_hdr(1)
        st1 = os.stat(hdr)
        h1, _ = compile_(hsrc, 'h1.o')
        write_hdr(2)
        st2 = os.stat(hdr)
        # ⭐ AUTO-VERIFICATION DU PIEGE : la sonde ne se contente pas d'annoncer
        # qu'il est arme, elle le CONSTATE. MESURE : sans mtime figee, les deux
        # mtimes different, le piege n'est PAS arme, et la sonde imprimait
        # "honnete" sur un cache menteur en 6/6.
        if st2.st_size != st1.st_size or st2.st_mtime != st1.st_mtime:
            die('SONDE INVALIDE (le piege n est pas arme)',
                "les deux en-tetes n'ont pas la meme (taille, mtime) : "
                "taille %d/%d, mtime %r/%r. Le piege file_stat_matches n'est "
                "pas arme et la sonde ne peut RIEN detecter -- elle rendrait "
                "PASS sur un cache menteur (mesure : 6/6)."
                % (st1.st_size, st2.st_size, st1.st_mtime, st2.st_mtime))
        h2, _ = compile_(hsrc, 'h2.o')
        if h2 == h1:
            die('CACHE DE COMPILATION MALHONNETE',
                "un EN-TETE modifie a taille et mtime EGALES a rendu le MEME "
                "objet (%s) : le cache a servi l'objet de CALAOS_PROBE_VAL=1 "
                "pour une source qui vaut 2. C'est exactement le faux vert "
                "qu'une campagne de mutations ne peut pas voir.\n"
                "Cause la plus probable : sloppiness contient "
                "file_stat_matches,file_stat_matches_ctime." % h1[:16])
        write_hdr(1)
        h3, hit3 = compile_(hsrc, 'h3.o')
        # ⭐ LE POINT (b) : l'en-tete restaure a l'identique DOIT etre servi par
        # le cache. Sinon le cache ne sert rien et les comparaisons ci-dessus
        # n'ont rien pu detecter.
        exiger_succes(hit3, "l'en-tete restaure a l'identique (3e compilation)")
        if h3 != h1:
            die('CACHE DE COMPILATION MALHONNETE',
                "l'en-tete restaure a l'identique rend un objet DIFFERENT "
                "(%s != %s) : le cache ne reproduit pas ce que le compilateur "
                "produit." % (h3[:16], h1[:16]))

        # --- Aller-retour n.2 : la SOURCE, A -> B -> A. ---------------------
        src = os.path.join(d, 'probe.cpp')
        A = 'int calaos_ccache_probe(){ return 1; }\n'
        B = 'int calaos_ccache_probe(){ return 2; }\n'

        def put(text):
            with open(src, 'w') as fh:
                fh.write(text)

        put(A)
        hA, _ = compile_(src, 'a.o')
        put(B)
        hB, _ = compile_(src, 'b.o')
        put(A)
        hA2, hitA2 = compile_(src, 'a2.o')

    exiger_succes(hitA2, "la source A rejouee apres B (6e compilation)")
    if hA == hB:
        die('CACHE DE COMPILATION MALHONNETE',
            "deux sources DIFFERENTES (`return 1` et `return 2`) rendent le "
            "MEME objet (%s). Ce n'est pas une sonde non concluante : c'est le "
            "mensonge lui-meme." % hA[:16])
    if hA2 != hA:
        die('CACHE DE COMPILATION MALHONNETE',
            "la source A recompilee apres B rend un objet DIFFERENT (%s != %s)."
            % (hA2[:16], hA[:16]))

    emit('PASS',
         'en-tete taille+mtime egales: %s != %s, restaure SERVI PAR LE CACHE '
         '(le piege est arme, et le cache a bien ete exerce) ; source A/B/A: '
         '%s != %s, rejoue %s SERVI PAR LE CACHE'
         % (h1[:12], h2[:12], hA[:12], hB[:12], hA2[:12]))
    print('SONDE-CCACHE: config auditee -- %d reglages publies par `%s -p`, '
          '%d audites par valeur, %d libres, 0 inconnu ; empreinte %s'
          % (len(conf), driver,
             len([k for k in conf if k in AUDITED_MAP]),
             len([k for k in conf if k in LIBRES_MAP]),
             empreinte[:16]))
    sys.exit(RC_PASS)


try:
    main()
except SystemExit:
    raise
except Exception as exc:  # fermeture par defaut : jamais un PASS silencieux
    emit('ECHEC', 'la sonde a leve une exception : %r' % (exc,))
    print("Une sonde qui plante ne garde rien. rc=1, jamais 77.")
    sys.exit(RC_FAIL)
