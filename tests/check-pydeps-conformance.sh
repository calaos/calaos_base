#!/bin/sh
# T3.67 -- l'environnement ou tourne `make check` porte-t-il le jeu Python que
# src/bin/calaos_mcp/pyproject.toml declare ?
#
# Rien dans le depot ne comparait l'image de developpement a sa propre recette.
# Mesure du 2026-09-04 dans l'image publiee : les huit paquets epingles par
# pyproject.toml ABSENTS, alors que .devcontainer/Dockerfile les installe
# depuis T3.23. Deux consequences : le sidecar MCP ne demarre pas la ou on le
# developpe, et run-python-tests.sh SKIPPE -- un correctif present dans le
# depot et absent de l'image, sans que rien ne le signale.
#
# La sonde compare deux choses independantes : la DECLARATION vient du
# pyproject (via scripts/pyproject-requirements.py, le meme expanseur que les
# Dockerfile et la CI), la REALITE vient des .dist-info poses sur le disque.
#
# Codes de sortie (protocole automake) :
#   0  -> PASS (tout le jeu declare est present, a la version epinglee)
#   77 -> SKIP (un ecart, nomme paquet par paquet)
#   1  -> FAIL (CALAOS_PYDEPS_STRICT=1, ou sonde qui plante)
#
# Le SKIP est le defaut voulu : un developpeur dont l'image est en retard doit
# etre averti, pas bloque -- meme motif que check-ccache-honesty.sh. La CI et
# le build des images posent CALAOS_PYDEPS_STRICT=1, ou "l'environnement n'est
# pas conforme" est une erreur de build.
#
# --extra test : c'est l'image ou `make check` tourne, et sans pytest, httpx et
# fastapi les suites python skippent de l'interieur.
#
# PYTHON et abs_top_srcdir sont exportes par AM_TESTS_ENVIRONMENT dans
# tests/Makefile.am ; valeurs de repli quand on lance le script a la main.

: "${PYTHON:=python3}"

# AM_PATH_PYTHON pose PYTHON=: quand aucun interprete n'a ete trouve.
if test "x$PYTHON" = "x:"; then
    if test "x$CALAOS_PYDEPS_STRICT" = "x1"; then
        echo "SONDE-PYDEPS: ECHEC -- aucun interprete python3 detecte au configure"
        exit 1
    fi
    echo "SONDE-PYDEPS: SKIP -- aucun interprete python3 detecte au configure"
    exit 77
fi

if test -z "$abs_top_srcdir"; then
    abs_top_srcdir=$(cd "$(dirname "$0")/.." && pwd)
fi

exec "$PYTHON" "$abs_top_srcdir/scripts/pydeps-conformance-probe.py" --extra test \
     "$abs_top_srcdir/src/bin/calaos_mcp/pyproject.toml"
