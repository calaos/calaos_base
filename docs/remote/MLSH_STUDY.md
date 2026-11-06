# Accès distant via MLSH : étude de faisabilité

État : étude seulement, **non planifiée**. Rien n'est lancé sans décision de Raoul.
Objectif à terme : déprécier calaos_ddns et donner à calaos_mobile un accès distant sans nom DNS ni ouverture de port, avec un VPN simple et transparent.
Lié à : `docs/admin/PLAN.md`. Le plan admin reste valable avec MLSH, voir la section « Compatibilité avec le plan admin ».

## Ce qu'est MLSH (état au 2026-10-18, HEAD 8dde9cb)
- **Architecture.** Mesh pair-à-pair en QUIC (quinn 0.11, rustls 0.23 avec aws-lc-rs, échange hybride post-quantique X25519Kyber768). Un serveur « signal » gère l'enregistrement, la découverte, la vérification des invitations et le relais de dernier recours. Le relais est chiffré de bout en bout : il ne voit que du chiffré.
- **NAT.** Méthode inspirée d'ICE (candidats, adresse réflexive, happy-eyeballs), avec repli sur le relais du signal. Aucun repli TCP si l'UDP est bloqué.
- **Adressage.** IPv4 overlay en `100.64.0.0/10`, attribuée de façon stable à chaque nœud. Pas d'IPv6. DNS par nœud : `<nom>.<cluster>`. La box voit l'IP overlay réelle du client, sans NAT de source.
- **Accès aux services.** Interface TUN noyau `mlsh0` (CAP_NET_ADMIN). Les pairs joignent `<ip-overlay>:<port>` directement, en HTTP clair dans le tunnel. MLSH ne fournit aucun certificat pour les noms overlay. `mlsh expose` publie un service sur Internet (SNI ingress, Let's Encrypt), mais il faut pour cela une zone DNS déléguée.
- **Identité.** Clé Ed25519 par appareil, dans un X.509 auto-signé, épinglée par empreinte. Les nœuds rejoignent le cluster par invitation (URL ou QR signés, TTL de 1 h par défaut). Révocation avec `mlsh revoke`.
- **Contrôle d'accès.** Aucune ACL réseau : tout nœud du cluster est un pair complet. Les rôles se limitent à node, admin et control.
- **Signal compromis.** Il peut injecter un pair, remapper des noms ou des IP, ou faire un DoS. Il ne peut pas lire le trafic relayé. Ce cas n'est pas documenté par le projet.
- **Clients.** Linux, macOS et Windows (systray Qt6 qui pilote le daemon par une socket JSON). **Aucun client iOS ou Android**, ni SDK ou FFI.
- **Licences et maturité.** Client, crypto et protocole sous MIT ; signal sous AGPL-3.0. Version v0.3.3, pré-1.0, deux auteurs (Nicolas Aguirre, Raoul Hecky), pas d'audit ni de SECURITY.md. Les changements à faire en amont sont donc réalistes.

## Structure du code et constats techniques
- **Quatre crates** :
  - `mlsh-protocol` : types et framing CBOR ;
  - `mlsh-crypto` : identité et invitations ;
  - `mlsh-cli` : CLI et daemon `mlshtund` ;
  - `mlsh-signal` : serveur.
- **Le cœur réutilisable** se trouve dans `mlsh-cli/src/tund/` : `signal_session.rs`, `overlay/{fsm,probe,quic,relay,peer_table}.rs` et `quic/verifier.rs`.
- **L'overlay travaille en L3.** Les paquets IP du TUN transitent en datagrammes QUIC (RFC 9221). Il n'existe **aucune API de flux vers un port**.
- **Le TUN est câblé dans `PeerConnectionContext.device`.** Pour l'extraire, il faut un trait de « puits de paquets ».
- **Test Docker** (rust 1.99, NDK r27c, `aarch64-linux-android`) :
  - `mlsh-protocol` et `mlsh-crypto` passent ;
  - `mlsh-cli` compile toutes ses dépendances et n'échoue que sur 3 erreurs dans `tunnel.rs:345-361`, parce que sur Android `tun_rs::DeviceBuilder` est remplacé par `from_fd` ;
  - iOS n'a pas pu être testé sous Linux, mais `cargo tree` résout.

## Voies étudiées pour calaos_mobile

| Voie | Principe | Effort | Risques |
|---|---|---|---|
| iOS, VPN système | NEPacketTunnelProvider avec une staticlib Rust et du Swift, un App Group et `NETunnelProviderManager` piloté depuis Qt | 5 à 7 sem. | entitlement Apple et revue App Store ; un second appex dans un projet qmake déjà fragile ; limite mémoire d'environ 50 Mo (réduire les fenêtres QUIC de 64 Mo) ; batterie |
| Android, VPN système | `VpnService.establish()`, puis le fd passé par JNI à `tun_rs::from_fd`, `protect()` du socket UDP et un service au premier plan | 3 à 4 sem. | targetSdk 29 à relever ; Doze et batterie |
| **Embarqué dans l'app (recommandé)** | lib sans TUN, plus une extension « stream dial » | voir le verdict | voir les risques |

### Voie recommandée : embarquée avec stream dial
- **Lib côté app.** L'app embarque l'identité, la session signal, la FSM, le probing et le relais, sans TUN.
- **Stream dial.**
  - Sur la connexion QUIC entre pairs, l'app ouvre un stream bidirectionnel avec un en-tête `{host, port}`.
  - Le daemon de la box fait `TcpStream::connect` puis `splice`, avec les primitives déjà présentes dans `ingress.rs:215-262`.
  - Le même mécanisme fonctionne en relais via `RelayOpen`.
- **Allowlist côté box.** Le daemon de la box n'autorise que `127.0.0.1:443`. C'est un vrai point d'ACL, qui compense l'absence d'ACL dans MLSH.
- **Connexion de l'app.** La lib ouvre un listener local, et `CalaosConnection` se connecte à `wss://127.0.0.1:<port>/api` sans changer `startConnection`.
- **Alternative écartée.** Une pile TCP userspace (smoltcp) qui fabrique des paquets IP serait lourde et fragile.

## Compatibilité avec le plan admin
- **Stream dial.** La box voit une connexion loopback vers haproxy sur le port 443, qui pose `X-Forwarded-Proto: https`. `secureTransport()` est satisfait sans aucune exception VPN, et l'invariant « haproxy seul terminateur TLS » tient. L'identité réseau du client se réduit à 127.0.0.1, la même limite que pour le MCP.
- **Client MLSH système sur un desktop** (navigateur). Il faut utiliser `https://<ip-overlay>`, qui passe par haproxy sur 443. Avec `http://…:5454`, l'admin est refusé, et c'est voulu.
- **Confiance.** On ne fait jamais confiance à `mlsh0` ni à 100.64/10 comme transport sûr. Il n'y a pas d'ACL, le signal peut injecter des pairs, et la plage est aussi celle du CGNAT des opérateurs.
- **Épinglage TOFU (CM2).** En mode embarqué, toutes les box apparaissent comme `127.0.0.1`. Il faut donc épingler **par identité de box**, par exemple l'empreinte du nœud MLSH, et non par nom d'hôte.
- **La webapp dans un navigateur ne bénéficie pas du mode embarqué.** Elle passe par le LAN, le DDNS ou le client MLSH desktop.

## Côté box
- **Paquet `calaos-mlsh`**, sur le modèle des autres (deb qui enveloppe un conteneur podman `--network=host`). En mode stream dial pur, la box peut se passer du TUN ; sinon, `--cap-add NET_ADMIN --device /dev/net/tun`. Ajout dans `calaos-meta`.
- **Signal.** Image `ghcr.io/mlsh-io/mlsh-signal`. Il faut UDP/443 et TCP/443 publics ainsi qu'un domaine. Le plafond de débit du relais est documenté mais introuvable dans le code (non vérifié). Décision à prendre : auto-hébergement par le projet Calaos (coût du relais pour les mobiles derrière CGNAT) ou SaaS `api.mlsh.io`.
- **Onboarding.**
  1. La box est le nœud admin.
  2. calaos_server génère une invitation avec `mlsh invite --json`.
  3. La webapp l'affiche en QR dans la vue Admin.
  4. L'app scanne le QR et appelle `Adopt`, lié à son certificat client.

## Verdict
**Faisable.** La voie recommandée est le mode embarqué avec stream dial, sur iOS et Android. Le VPN système reste possible plus tard si on veut un accès réseau complet.

| Brique | Effort (personne-semaines) |
|---|---|
| Extraction de `mlsh-core` sans TUN | 2 à 3 |
| Stream dial et allowlist côté daemon | 1 à 2 |
| FFI C (cbindgen), intégration Qt/qmake, cargo-ndk, xcframework | 2 à 3 |
| Adoption par QR dans l'app | 1 |
| Côté box (deb, invitation, webapp) | 1 à 2 |
| Signal hébergé | 1 |
| **Total** | **≈ 8 à 12** (contre 14 à 18 avec NetworkExtension et VpnService) |

Risques :
- réseaux qui bloquent l'UDP (pas de repli TCP) ;
- CGNAT mobile qui force le relais ;
- batterie, et coupure en arrière-plan (reconnexion au premier plan) ;
- hébergement du signal ;
- qmake et Xcode fragiles sur iOS ;
- jeunesse du projet.

## Premières étapes d'un prototype (à ne pas lancer)
1. Créer `mlsh-core` (identité, signal, FSM, quic, relay) derrière un trait de puits de paquets, avec un `cargo check` Android et iOS en CI.
2. Implémenter le stream dial entre deux nœuds Linux, allowlist `127.0.0.1:443`, test avec `curl`.
3. Exposer la FFI `mlsh_adopt/connect/listen_local`, la lier dans calaos_mobile Android via cargo-ndk et pointer `CalaosConnection` sur le port local.
4. Créer le deb `calaos-mlsh`, générer `mlsh invite --json` depuis calaos_server et afficher le QR dans la webapp.
5. Produire la même lib en xcframework pour iOS, sans NetworkExtension.
