#!/usr/bin/env python3
"""Sonde d'honnetete du cache de compilation (ccache).

Un cache malhonnete rend un objet qui ne correspond pas a la source compilee.
Il rendrait vertes des campagnes de mutations entieres et invaliderait tout
verdict tire d'un `make check`. Cette sonde est le controle permanent de cet
outil ; elle est cablee dans TESTS.

TROIS codes de sortie, et AUCUN mode d'echec ne rend 0 :

  0  PASS  -- un cache est en service, sa configuration est EXACTEMENT celle
              qui a ete auditee, et les deux aller-retours empiriques sont
              honnetes. La ligne imprimee commence par "SONDE-CCACHE: PASS".

  77 SKIP  -- aucun cache de compilation n'est en service. C'est le SEUL motif
              de SKIP qui subsiste. La ligne commence par "SONDE-CCACHE: SKIP"
              et le dit en toutes lettres, pour qu'un SKIP ne puisse plus etre
              lu comme un PASS dans un journal.
              CALAOS_CCACHE_PROBE_STRICT=1 transforme ce SKIP en ECHEC : c'est
              la condition (7) de T3.51 (un arbre qui sert a juger une campagne
              doit rendre 0, pas SKIP).

  1  ECHEC -- TOUT le reste. Cache malhonnete, configuration non auditee, sonde
              qui ne compile pas, resultat non concluant, exception imprevue.
              Fermeture par defaut : ce qui n'est pas prouve honnete est
              declare faux. La sonde n'a le droit de se taire que quand il n'y
              a rien a garder.

Ce que la sonde verifie, dans l'ordre :

  1. Configuration, par LISTE BLANCHE et non par liste noire. Une liste noire
     laisse passer tout reglage qu'on n'a pas pense a y ecrire -- c'est ainsi
     que `compiler_check = string:CONST` (equivalent de `none`) passait.
     Exige : sloppiness VIDE, compiler_check = content, hash_dir = true,
     base_dir vide.
  2. Aller-retour sur un EN-TETE, taille et mtime preservees. C'est le seul
     chemin par lequel `sloppiness = file_stat_matches[,_ctime]` se manifeste :
     une unite de traduction sans `#include` ne peut pas le voir, et la sonde
     precedente n'en avait pas -- sa moitie empirique imprimait "honnete" sur
     un cache demontre menteur.
  3. Aller-retour sur la SOURCE : A -> B -> A. obj(A) != obj(B) et
     obj(A rejoue) == obj(A). obj(A) == obj(B) n'est pas "non concluant" :
     c'est exactement le mensonge, et c'est un ECHEC.

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

# LISTE BLANCHE : (reglage, valeur exigee, pourquoi un autre valeur ment).
REQUIRED = (
    ('sloppiness', '',
     "un cache autorise a ignorer une partie de son entree peut servir "
     "l'objet d'une source differente (file_stat_matches, time_macros, "
     "include_file_mtime...)"),
    ('compiler_check', 'content',
     "le defaut `mtime` sert un objet perime des qu'un compilateur est "
     "remplace a taille et date egales ; `none` et `string:...` ne "
     "regardent pas le compilateur du tout"),
    ('hash_dir', 'true',
     "sans le repertoire de compilation dans la clef, l'objet compile au "
     "chemin A est servi au chemin B -- il differe de ce qu'une compilation "
     "propre aurait produit (DW_AT_comp_dir)"),
    ('base_dir', '',
     "base_dir reecrit les chemins avant le hachage ; il n'est pas malhonnete "
     "en soi, mais ce n'est pas la configuration auditee et la sonde ne "
     "prononce que sur celle-la"),
)


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


def ccache_in_use(cxx):
    """cxx est la ligne de commande COMPLETE, pas son premier mot."""
    for word in cxx:
        if os.path.basename(word) == 'ccache':
            return True
    prog = shutil.which(cxx[0])
    if prog:
        real = os.path.realpath(prog)
        if os.path.basename(real) == 'ccache' or real.endswith('/ccache'):
            return True
    r = run(cxx + ['--version'])
    return 'ccache' in (r.stdout + r.stderr).lower()


def read_config():
    r = run(['ccache', '-p'])
    if r.returncode != 0:
        die('CONFIGURATION NON AUDITABLE',
            "un cache est en service mais `ccache -p` a echoue (%s). La sonde "
            "ne peut rien garantir." % (r.stderr.strip()[:200] or r.returncode))
    conf = {}
    for line in r.stdout.splitlines():
        m = re.match(r'^(?:\([^)]*\)\s*)?([A-Za-z_]\w*)\s*=\s*(.*)$', line)
        if m:
            conf[m.group(1)] = m.group(2).strip()
    return conf


def check_config(conf):
    for key, want, why in REQUIRED:
        got = conf.get(key)
        if got is None:
            die('CONFIGURATION NON AUDITEE',
                "`ccache -p` ne publie pas `%s` : la configuration ne peut pas "
                "etre auditee." % key)
        if got != want:
            die('CONFIGURATION NON AUDITEE (liste blanche)',
                "%s = %r, attendu %r.\n%s\nCorriger avec "
                "scripts/ccache-setup.sh (ou CCACHE_%s dans l'environnement)."
                % (key, got, want, why, key.replace('_', '').upper()))


class Compiler(object):
    def __init__(self, cxx, workdir, statslog):
        self.cxx, self.workdir, self.statslog = cxx, workdir, statslog

    def __call__(self, src, out):
        env = dict(os.environ)
        env['CCACHE_STATSLOG'] = self.statslog
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
            return hashlib.sha256(fh.read()).hexdigest()


def last_invocation_hit(statslog):
    """True/False/None -- lu dans le journal PAR INVOCATION, jamais dans -s."""
    try:
        with open(statslog) as fh:
            blocks = fh.read().split('#')
    except OSError:
        return None
    if len(blocks) < 2:
        return None
    return 'cache_hit' in blocks[-1]


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

    if not shutil.which('ccache'):
        die('CONFIGURATION NON AUDITABLE',
            "un cache est en service via %s mais le binaire `ccache` est "
            "introuvable : sa configuration ne peut pas etre lue."
            % ' '.join(cxx))

    conf = read_config()
    check_config(conf)

    with tempfile.TemporaryDirectory(prefix='ccache-probe-') as d:
        statslog = os.path.join(d, 'stats.log')
        compile_ = Compiler(cxx, d, statslog)

        # --- Aller-retour n.1 : un EN-TETE, a taille ET mtime egales. -------
        # C'est le SEUL chemin par lequel file_stat_matches se manifeste. Les
        # deux corps font exactement le meme nombre d'octets, et la mtime est
        # remise a sa valeur d'origine : un cache qui se fie a (taille, mtime)
        # servira l'objet de VAL=1 pour VAL=2.
        hdr = os.path.join(d, 'probe_hdr.h')
        hsrc = os.path.join(d, 'probe_hdr.cpp')
        with open(hsrc, 'w') as fh:
            fh.write('#include "probe_hdr.h"\n'
                     'int calaos_ccache_probe_hdr(){ return CALAOS_PROBE_VAL; }\n')

        def write_hdr(val):
            """Meme taille a l'octet pres, et TOUJOURS la meme mtime.

            La mtime est figee dans le PASSE (STAMP) et non "maintenant" :
            ccache refuse de se fier a (taille, mtime) pour un fichier modifie
            trop recemment, si bien qu'une mtime courante rendrait le piege
            intermittent. Figee dans le passe, il mord a tous les coups.
            """
            with open(hdr, 'w') as fh:
                fh.write('#define CALAOS_PROBE_VAL %d\n' % val)
            os.utime(hdr, (STAMP, STAMP))

        write_hdr(1)
        size1 = os.stat(hdr).st_size
        h1 = compile_(hsrc, 'h1.o')
        write_hdr(2)
        if os.stat(hdr).st_size != size1:
            die('SONDE INVALIDE',
                "les deux en-tetes n'ont pas la meme taille : le piege "
                "file_stat_matches n'est pas arme.")
        h2 = compile_(hsrc, 'h2.o')
        if h2 == h1:
            die('CACHE DE COMPILATION MALHONNETE',
                "un EN-TETE modifie a taille et mtime EGALES a rendu le MEME "
                "objet (%s) : le cache a servi l'objet de CALAOS_PROBE_VAL=1 "
                "pour une source qui vaut 2. C'est exactement le faux vert "
                "qu'une campagne de mutations ne peut pas voir.\n"
                "Cause la plus probable : sloppiness contient file_stat_matches "
                "(et/ou file_stat_matches_ctime)." % h1[:16])
        write_hdr(1)
        h3 = compile_(hsrc, 'h3.o')
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
        hA = compile_(src, 'a.o')
        put(B)
        hB = compile_(src, 'b.o')
        put(A)
        hA2 = compile_(src, 'a2.o')
        hit = last_invocation_hit(statslog)

    if hA == hB:
        die('CACHE DE COMPILATION MALHONNETE',
            "deux sources DIFFERENTES (`return 1` et `return 2`) rendent le "
            "MEME objet (%s). Ce n'est pas une sonde non concluante : c'est le "
            "mensonge lui-meme." % hA[:16])
    if hA2 != hA:
        die('CACHE DE COMPILATION MALHONNETE',
            "la source A recompilee apres B rend un objet DIFFERENT (%s != %s)."
            % (hA2[:16], hA[:16]))

    if hit is True:
        served = ', 3e appel servi PAR LE CACHE (journal par invocation)'
    elif hit is False:
        served = ", 3e appel NON servi par le cache (chemin du cache non exerce)"
    else:
        served = ", attribution du succes de cache indisponible (CCACHE_STATSLOG muet)"
    emit('PASS',
         'en-tete taille+mtime egales: %s != %s (le piege est arme et il mord) ; '
         'source A/B/A: %s != %s, rejoue %s%s'
         % (h1[:12], h2[:12], hA[:12], hB[:12], hA2[:12], served))
    print('SONDE-CCACHE: config auditee -- ' + ' '.join(
        '%s=%r' % (k, conf.get(k)) for k, _, _ in REQUIRED))
    sys.exit(RC_PASS)


try:
    main()
except SystemExit:
    raise
except Exception as exc:  # fermeture par defaut : jamais un PASS silencieux
    emit('ECHEC', 'la sonde a leve une exception : %r' % (exc,))
    print("Une sonde qui plante ne garde rien. rc=1, jamais 77.")
    sys.exit(RC_FAIL)
