#!/bin/sh
# Controle permanent : si un cache de compilation est en service, il doit etre
# HONNETE, c'est-a-dire ne jamais rendre un objet qui ne correspond pas a la
# source compilee. Un cache malhonnete rendrait vertes toutes les campagnes de
# mutations et invaliderait le dispositif de qualite tout entier.
#
# Trois codes de sortie, et AUCUN mode d'echec ne rend PASS :
#   0  PASS  -- cache en service, TOUTE sa configuration publiee est celle qui
#               a ete auditee, un succes de cache a ete CONSTATE, aller-retours
#               honnetes.
#   77 SKIP  -- AUCUN cache en service. Seul motif de SKIP restant. La ligne
#               imprimee commence par "SONDE-CCACHE: SKIP" et le dit : un SKIP
#               ne doit pas pouvoir se lire comme un PASS dans un journal.
#   1  ECHEC -- tout le reste, y compris "la sonde ne compile pas", "le
#               detecteur ne peut pas mordre" et "resultat non concluant".
#               Fermeture par defaut.
#
# CALAOS_CCACHE_PROBE_STRICT=1 transforme le SKIP en ECHEC : a poser dans tout
# arbre qui sert a juger une campagne (T3.51, condition 7).
#
# ⚠️ SUR FEDORA ET GENTOO, UN ARBRE NEUF REND 1, PAS 77 : /usr/lib64/ccache
# (resp. /usr/lib/ccache) est dans le PATH par defaut et `g++` y est un lien
# vers ccache, donc un cache EST en service -- non configure. TROIS issues :
#   (1) scripts/ccache-setup.sh ; (2) les variables CCACHE_* dans
#   l'environnement ; (3) retirer le repertoire de shims du PATH (ou
#   CXX=/usr/bin/g++) => plus aucun cache, SKIP propre.
#
# Cout mesure : 29-40 ms sans cache (sortie 77), 165-176 ms avec (six
# compilations triviales) -- ccache 4.7.5 et 4.12.3, machine 64 coeurs chargee.
#
# Il ne remplace PAS la discipline "rm -f objets + binaire puis make a la
# racine" : il verifie l'outil, pas le protocole.
set -e
srcdir=${abs_top_srcdir:-$(cd "$(dirname "$0")/.." && pwd)}
exec "${PYTHON:-python3}" "$srcdir/scripts/ccache-honesty-probe.py"
