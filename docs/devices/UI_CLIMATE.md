# Climate — design UI validé (2026-10-14)

Résultat de cinq rounds de maquettes. Les maquettes de référence sont hors dépôt, dans
`/home/raoul/repos/calaos/climate-mockups/` (HTML/CSS/JS statiques, captures PNG). Une sélection est
publiée dans le gist secret https://gist.github.com/raoulh/53a744f13815a93c00c58ce188929fad.

| Plateforme | Liste / widget | Réglage |
|---|---|---|
| calaos_mobile (téléphone) | `round5/final/mobile.*` | `round4/final/mobile.*` (bottom sheet) |
| calaos_mobile (desktop) | `round5/final/desktop.*` (même item que le mobile) | `round4/final/desktop.*` (dialogue) |
| calaos-web-app | `round4/final/web.*` | `round4/final/web.*` |
| calaos_remote_ui | `round5/final/remote.*`, sauf l'en-tête des grands widgets (voir plus bas) | `round4/final/remote.*` |

Le thème est celui des clients, relevé dans `round5/THEME.md` : cyan `#3ab4d7`, jaune `#ffda5a`,
fond `#171717`, ambre `#e0a356` (attente), rouge `#ff5555` (hors ligne). Roboto sur mobile et
desktop. Sur le panneau, seulement les tailles de police déjà compilées ; les icônes sont dessinées,
jamais des glyphes absents de la police.

## Principes

- L'écran principal montre la consigne, la température de la pièce, le mode et un statut. Il n'offre
  qu'un seul moyen de régler la consigne.
- Le mode est une pastille arrondie. Un tap ouvre un choix (sheet, popup ou dialogue selon la
  plateforme). Ventilation, oscillation et humidité sont rangées sous « Plus de réglages » ou `⋯`, et
  n'apparaissent que si les capacités les annoncent.
- Le cadran est un arc de 300° presque fermé : une piste neutre, la portion jusqu'à la consigne dans
  la couleur de l'action, un curseur, et la mesure en simple repère. La consigne s'affiche en grand
  sur la ligne « − 22° + » sous l'arc. En `auto`, l'arc porte deux curseurs et il y a deux paires −/+.
  En butée `min_range`, les boutons intérieurs sont refusés et le texte dit « Écart minimum 2° ».
- Couleurs de l'action : chauffe en jaune, froid en cyan, attente du relais en ambre, hors ligne en
  rouge ; `off`, inconnu et veille sont neutres.

## Statut affiché

Le statut combine l'action rapportée et l'état du device (`state`, spec §5.3) :

| Cas | Texte |
|---|---|
| `action` heating / cooling / drying / fan | Chauffe / Refroidit / Déshumidifie / Ventile |
| `action` idle | En veille |
| `lockout` présent | Démarre dans N min (secondes dans la vue de réglage) |
| `connected` faux | Hors ligne |
| `fault` présent | Sonde muette, Humidité trop haute, Sonde absente… |
| mode `off` | Arrêté |
| mode `""` ou absent | pastille grise « Mode inconnu » |

`lockout`, `connected`, `fault` et `humidity` viennent de CLIM-6.

## Par plateforme

**calaos_mobile, téléphone et desktop** : le même item QML.
- Rangée 1 : le nom (en jaune quand l'appareil agit) ; la température de la pièce alignée à droite.
- Rangée 2 : la pastille de mode (icône et mot, en gris si `off`, inconnu ou hors ligne), le statut
  coloré juste après, et la consigne alignée à droite (« 21 – 23° » en `auto`).
- L'icône à arc occupe la hauteur des deux rangées. Un chevron `›` à droite. Pas de −/+.
- Un tap ouvre une bottom sheet sur le téléphone, un dialogue sur le desktop. Le choix du mode et
  « Plus de réglages » s'empilent par-dessus.

**calaos-web-app** : une rangée `IoRowFrame` avec un mini-arc, le nom, le statut, la consigne, la
pièce et un chevron, plus un effet de survol. Le réglage s'ouvre dans un panneau latéral, ou dans un
dialogue centré sous 1024 px. Le mode et les réglages s'ouvrent dans des `BaseDialog`.

**calaos_remote_ui** : le contenu dépend de la taille du widget.
- 1×1 : la température de la pièce, « › consigne », l'icône et le mot de statut, le nom sur 2
  lignes. Pas de −/+.
- 2×1 et 1×2 : on ajoute un mini-arc et la jauge d'attente du relais. Pas de −/+.
- Pour ces petits widgets, un tap ouvre la vue plein écran (cadran, « − 22° + », pastille de mode,
  « Plus de réglages »). Le mode et les réglages se modifient dans des popups.
- 2×2, 3×3 et plus : le climate complet dans le widget, sans vue intermédiaire. La pastille de mode
  et `⋯` sont en haut, le cadran et « − 22° + » au milieu, et le **titre en bas**, comme les autres
  widgets du panneau. Seuls le mode et les réglages s'ouvrent en popups.
