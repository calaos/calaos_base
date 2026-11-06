#!/bin/sh
# L'environnement ou tourne `make check` porte-t-il le jeu Python que
# src/bin/calaos_mcp/pyproject.toml declare ? Rien ne le comparait : une image
# en retard sur sa propre recette ne demarre pas le sidecar MCP et fait skipper
# run-python-tests.sh, en annoncant un correctif deja present dans le depot.
#
# Codes automake : 0 PASS, 77 SKIP (ecart, nomme paquet par paquet), 1 FAIL
# (CALAOS_PYDEPS_STRICT=1, ou sonde qui plante). Le SKIP est le defaut voulu --
# averti, pas bloque, meme motif que check-ccache-honesty.sh ; la CI et le
# build des images posent le mode strict, ou la derive est une erreur.
#
# --extra test : c'est l'image ou `make check` tourne, et sans pytest, httpx et
# fastapi les suites python skippent de l'interieur.
#
# PYTHON et abs_top_srcdir viennent d'AM_TESTS_ENVIRONMENT ; les replis
# ci-dessous servent au lancement a la main.

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
