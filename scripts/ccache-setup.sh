#!/bin/sh
# Seme dans $CCACHE_DIR la configuration de cache que scripts/ccache-honesty-probe.py
# AUDITE. A rejouer sans risque -- et a rejouer systematiquement, voir l'avertissement.
#
# Les quatre reglages ci-dessous sont ecrits EXPLICITEMENT, aucun n'est laisse
# a son defaut :
#   sloppiness      = (vide)   -- rien de l'entree n'a le droit d'etre ignore.
#   compiler_check  = content  -- le DEFAUT de ccache est `mtime`, et `mtime`
#                                 sert un objet perime des qu'un compilateur est
#                                 remplace a taille et date egales. Le defaut
#                                 n'est PAS sur ; il est seulement courant.
#   hash_dir        = true     -- le repertoire de compilation entre dans la
#                                 clef. Sans lui, l'objet du chemin A est servi
#                                 au chemin B et differe d'une compilation
#                                 propre (DW_AT_comp_dir).
#   base_dir        = (vide)   -- base_dir n'est pas malhonnete, mais ce n'est
#                                 pas la configuration auditee.
#
# ###########################################################################
# # $CCACHE_DIR/ccache.conf EST UN ETAT PARTAGE MUTABLE.                    #
# # Un `ccache -o` joue dans un conteneur PERSISTE dans le fichier, et TOUT #
# # conteneur ulterieur qui monte le meme repertoire en HERITE, sans trace  #
# # de qui l'a ecrit. Un `-M` joue par un agent change la taille maximale de #
# # tous les autres. C'est pourquoi ce script IMPRIME la configuration      #
# # HERITEE avant de la reecrire : un ecart non attribue doit se voir.      #
# # La parade sans effet de bord : un CCACHE_DIR PAR AGENT, ou les          #
# # variables d'environnement CCACHE_* (voir docs/refactoring/OUTILLAGE-CCACHE.md #
# # sect. 4), qui ne persistent rien.                                       #
# ###########################################################################
set -e
: "${CCACHE_DIR:=/ccache}"
export CCACHE_DIR
mkdir -p "$CCACHE_DIR"

echo "== configuration HERITEE de $CCACHE_DIR (avant reecriture) =="
if [ -f "$CCACHE_DIR/ccache.conf" ]; then
    cat "$CCACHE_DIR/ccache.conf"
    echo "-- ^ ecrit par un agent precedent, non attribuable. Un max_size qui ne"
    echo "--   correspond pas a CALAOS_CCACHE_SIZE vient de la, pas d'ici."
else
    echo "(aucun ccache.conf : repertoire neuf)"
fi

ccache -M "${CALAOS_CCACHE_SIZE:-10G}" >/dev/null
ccache -o sloppiness=
ccache -o compiler_check=content
ccache -o hash_dir=true
ccache -o base_dir=
ccache -o compression=true
ccache -o compression_level=1
ccache -o temporary_dir="$CCACHE_DIR/tmp"

echo "== configuration EFFECTIVE (celle que la sonde audite) =="
echo "CCACHE_DIR=$CCACHE_DIR  taille max=${CALAOS_CCACHE_SIZE:-10G}"
ccache -p | grep -E '(^|\)) *(sloppiness|compiler_check|hash_dir|base_dir|max_size) *=' || true
