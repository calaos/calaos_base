#!/bin/sh
# Controle permanent : si un cache de compilation est en service, il doit etre
# HONNETE, c'est-a-dire ne jamais rendre un objet qui ne correspond pas a la
# source compilee. Un cache malhonnete rendrait vertes toutes les campagnes de
# mutations et invaliderait le dispositif de qualite tout entier.
#
# Trois codes de sortie, et AUCUN mode d'echec ne rend PASS :
#   0  PASS  -- cache en service, configuration auditee, aller-retours honnetes.
#   77 SKIP  -- AUCUN cache en service. Seul motif de SKIP restant. La ligne
#               imprimee commence par "SONDE-CCACHE: SKIP" et le dit : un SKIP
#               ne doit pas pouvoir se lire comme un PASS dans un journal.
#   1  ECHEC -- tout le reste, y compris "la sonde ne compile pas" et
#               "resultat non concluant". Fermeture par defaut.
#
# CALAOS_CCACHE_PROBE_STRICT=1 transforme le SKIP en ECHEC : a poser dans tout
# arbre qui sert a juger une campagne (T3.51, condition 7).
#
# Cout : ~40 ms sans cache (sortie 77), ~0,3 s avec (six compilations triviales).
#
# Il ne remplace PAS la discipline "rm -f objets + binaire puis make a la
# racine" : il verifie l'outil, pas le protocole.
set -e
srcdir=${abs_top_srcdir:-$(cd "$(dirname "$0")/.." && pwd)}
exec "${PYTHON:-python3}" "$srcdir/scripts/ccache-honesty-probe.py"
