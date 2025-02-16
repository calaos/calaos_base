#!/bin/env python3

from calaos_extern_proc import ExternProcClient
from roonapi import RoonDiscovery
import time

# Création d'une instance de découverte
discovery = RoonDiscovery(None)

# Attente de la découverte des serveurs
print("Recherche des serveurs Roon...")
time.sleep(2)

# Affichage des serveurs trouvés
servers = discovery.all()
if servers:
    print("\nServeurs Roon trouvés :")
    for server in servers:
        print(f"Serveur : {server}")
else:
    print("Aucun serveur Roon trouvé")

# Nettoyage
discovery.stop()

# import time

# from roonapi import RoonApi, RoonDiscovery

# appinfo = {
#     "extension_id": "calaos_roon_extension",
#     "display_name": "Calaos Roon Extension",
#     "display_version": "1.0.0",
#     "publisher": "Calaos",
#     "email": "team@calaos.fr",
# }

# discover = RoonDiscovery(None)
# servers = discover.all()

# print("Shutdown discovery")
# discover.stop()

# print("Found the following servers")
# print(servers)
# apis = [RoonApi(appinfo, None, server[0], server[1], False) for server in servers]

# auth_api = []
# while len(auth_api) == 0:
#     print("Waiting for authorisation")
#     time.sleep(1)
#     auth_api = [api for api in apis if api.token is not None]

# api = auth_api[0]

# print("Got authorisation")
# print(api.host)
# print(api.core_name)
# print(api.core_id)

# print("Shutdown apis")
# for api in apis:
#     api.stop()

# # This is what we need to reconnect
# core_id = api.core_id
# token = api.token

# with open("my_core_id_file", "w") as f:
#     f.write(api.core_id)
# with open("my_token_file", "w") as f:
#     f.write(api.token)