#!/bin/sh
# Seme dans $CCACHE_DIR la configuration de cache que scripts/ccache-honesty-probe.py
# AUDITE. A rejouer sans risque -- et a rejouer systematiquement, voir l'avertissement.
#
# La sonde n'audite PAS quatre reglages : elle audite TOUTE la configuration que
# `ccache -p` publie (44 clefs en 4.12.3, 43 en 4.7.5), et refuse toute clef
# qu'elle ne connait pas. Ce script ecrit EXPLICITEMENT les reglages dont le
# defaut de ccache n'est pas celui qui a ete audite ; tous les autres sont deja
# a la bonne valeur par defaut, et la sonde le verifie clef par clef.
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
#                                 pas la configuration auditee. TRANCHE : la
#                                 sonde l'EXIGE vide, et le partage entre
#                                 chemins differents n'est donc PAS une option
#                                 ouverte tant qu'il n'a pas ete audite.
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

command -v ccache >/dev/null 2>&1 || {
    echo "ccache introuvable dans le PATH : rien a configurer." >&2
    exit 1
}

# HORS CONTENEUR il n'y a pas de /ccache : le defaut cable ici echouait avec
# `mkdir: /ccache: Permission denied`. On DEMANDE A L'OUTIL ou est son cache
# plutot que de le deviner -- c'est aussi ce que la sonde auditera.
if [ -z "${CCACHE_DIR:-}" ]; then
    CCACHE_DIR=$(ccache -k cache_dir)
    echo "CCACHE_DIR n'est pas pose : on emploie celui que ccache declare"
    echo "  ($CCACHE_DIR). Pour un cache PAR AGENT -- recommande --, poser"
    echo "  CCACHE_DIR avant d'appeler ce script."
fi
export CCACHE_DIR

mkdir -p "$CCACHE_DIR" 2>/dev/null || {
    echo "impossible de creer $CCACHE_DIR. Poser CCACHE_DIR sur un repertoire" >&2
    echo "inscriptible (hors conteneur, /ccache n'existe pas)." >&2
    exit 1
}
[ -w "$CCACHE_DIR" ] || {
    echo "$CCACHE_DIR n'est pas inscriptible." >&2
    exit 1
}

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

echo "== configuration EFFECTIVE =="
echo "CCACHE_DIR=$CCACHE_DIR  taille max=${CALAOS_CCACHE_SIZE:-10G}"
ccache -p
echo "-- ^ CETTE SORTIE ENTIERE est ce que la sonde audite, clef par clef :"
echo "--   scripts/ccache-honesty-probe.py refuse toute valeur non auditee ET"
echo "--   toute clef qu'il ne connait pas. Une liste de clefs a surveiller"
echo "--   serait toujours en retard d'une clef ; c'est l'outil qui enumere."
