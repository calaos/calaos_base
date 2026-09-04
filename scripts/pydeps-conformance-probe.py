#!/usr/bin/env python3
"""Sonde de conformite : l'environnement porte-t-il ce que sa recette declare ?

Compare les distributions Python REELLEMENT installees pour l'interprete qui
execute cette sonde au jeu que src/bin/calaos_mcp/pyproject.toml DECLARE.

Ce n'est pas circulaire : la declaration vient du pyproject (via
scripts/pyproject-requirements.py, le meme expanseur que les Dockerfile et la
CI), la realite vient de importlib.metadata, c'est-a-dire des .dist-info
reellement poses sur le disque. Rien du cote realite n'est deduit de la recette.

Codes de sortie :

  0  PASS  -- chaque paquet declare est present, a la version epinglee.

  77 SKIP  -- ecart, ou declaration illisible. Chaque ecart est NOMME : un SKIP
              ne doit pas pouvoir se lire comme un PASS dans un journal. Defaut
              voulu -- un developpeur dont l'image est en retard doit etre
              AVERTI, pas bloque.

  1  ECHEC -- sous CALAOS_PYDEPS_STRICT=1, ou sur exception imprevue (une sonde
              qui plante ne garde rien). A poser la ou l'environnement DOIT
              porter le jeu : la CI, et le build des images.

Ce que la sonde ne voit PAS, et qui reste nu : les paquets apt du Dockerfile ;
qu'un paquet enregistre s'IMPORTE vraiment ; les contraintes autres que `==` et
les requirements a marqueur d'environnement, audites en PRESENCE seulement et
declares tels quels sur leur propre ligne.
"""

import os
import re
import subprocess
import sys

RC_PASS, RC_FAIL, RC_SKIP = 0, 1, 77

HERE = os.path.dirname(os.path.abspath(__file__))
EXPANDER = os.path.join(HERE, 'pyproject-requirements.py')
DEFAULT_PYPROJECT = os.path.join(HERE, os.pardir, 'src', 'bin', 'calaos_mcp',
                                 'pyproject.toml')


def emit(tag, msg):
    print('SONDE-PYDEPS: %s -- %s' % (tag, msg))


def give_up(msg, details=()):
    """Tout mode non-PASS passe par ici : 77 par defaut, 1 en mode strict."""
    strict = os.environ.get('CALAOS_PYDEPS_STRICT') == '1'
    emit('ECHEC' if strict else 'SKIP', msg)
    for line in details:
        print('SONDE-PYDEPS: %s' % line)
    if strict:
        print('SONDE-PYDEPS: CALAOS_PYDEPS_STRICT=1 -- ici l environnement DOIT '
              'porter ce que sa recette declare ; un ecart est une erreur.')
        sys.exit(RC_FAIL)
    print('SONDE-PYDEPS: SKIP n est pas un PASS : rien n a ete garanti sur ce '
          'jeu de paquets. Reconstruire l image, ou installer le jeu comme le '
          'Dockerfile le fait.')
    sys.exit(RC_SKIP)


def normalize(name):
    return re.sub(r'[-_.]+', '-', name).strip().lower()


def declared(pyproject, extras):
    argv = [sys.executable, EXPANDER]
    for extra in extras:
        argv += ['--extra', extra]
    argv.append(pyproject)
    proc = subprocess.run(argv, capture_output=True, text=True)
    if proc.returncode != 0:
        give_up('la declaration n a pas pu etre lue depuis %s' % pyproject,
                [l for l in proc.stderr.splitlines() if l])
    return [l.strip() for l in proc.stdout.splitlines() if l.strip()]


def installed():
    """Les .dist-info poses sur le disque, pas ce que la recette promet."""
    import importlib.metadata as md
    found = {}
    for dist in md.distributions():
        name = dist.metadata['Name']
        if name:
            found.setdefault(normalize(name), dist.version)
    return found


def main(argv):
    extras, args = [], []
    rest = list(argv[1:])
    while rest:
        arg = rest.pop(0)
        if arg == '--extra' and rest:
            extras.append(rest.pop(0))
        elif arg.startswith('--extra='):
            extras.append(arg.split('=', 1)[1])
        else:
            args.append(arg)
    pyproject = args[0] if args else DEFAULT_PYPROJECT

    if not os.path.isfile(EXPANDER):
        give_up('%s introuvable : la sonde ne peut pas lire la declaration'
                % EXPANDER)
    if not os.path.isfile(pyproject):
        give_up('%s introuvable' % pyproject)

    reqs = declared(pyproject, extras)
    have = installed()

    absents, mismatched, presence_only = [], [], []
    for req in reqs:
        # `nom==version` uniquement : tout le reste est audite en presence
        # seule et declare tel quel plutot que mal interprete.
        m = re.match(r'^([A-Za-z0-9._-]+)(\[[^\]]*\])?==([^\s;,]+)$', req)
        name = m.group(1) if m else re.split(r'[\[<>=!~;\s]', req, 1)[0]
        version = have.get(normalize(name))
        if version is None:
            absents.append('%s (declare: %s)' % (name, req))
        elif m and version != m.group(3):
            mismatched.append('%s installe en %s, epingle en %s'
                              % (name, version, m.group(3)))
        elif not m:
            presence_only.append('%s present en %s, contrainte "%s" non evaluee'
                                 % (name, version, req))

    where = '%s (%s)' % (sys.executable, '.'.join(map(str, sys.version_info[:3])))
    origin = os.path.relpath(pyproject)
    if origin.startswith(os.pardir):
        origin = pyproject
    extra_txt = (' + extra(s) %s' % ', '.join(extras)) if extras else ''

    if absents or mismatched:
        give_up('l environnement ne porte pas ce que %s declare : %d ABSENT(S), '
                '%d a une AUTRE version, sur %d declares -- interprete audite %s'
                % (origin, len(absents), len(mismatched), len(reqs), where),
                ['ABSENT %s' % a for a in absents]
                + ['VERSION %s' % v for v in mismatched])

    emit('PASS', '%d paquet(s) declare(s) par %s%s, tous presents a la version '
                 'epinglee -- interprete audite %s'
         % (len(reqs), origin, extra_txt, where))
    for line in presence_only:
        print('SONDE-PYDEPS: presence seule -- %s' % line)
    return RC_PASS


if __name__ == '__main__':
    try:
        sys.exit(main(sys.argv))
    except SystemExit:
        raise
    except Exception as exc:
        emit('ECHEC', 'la sonde a leve une exception : %r' % (exc,))
        print('Une sonde qui plante ne garde rien. rc=1, jamais 77.')
        sys.exit(RC_FAIL)
