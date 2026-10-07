# Audit initial de l'intégration AMD

Audit du 7 octobre 2026, sur les révisions de `dependencies/sources.lock.json`.
Cette architecture est un contrat de travail ; aucun raccordement RADV/Metal macOS
n'est implémenté ou qualifié par ce livrable.

## Découpage du fork

| Code existant | Décision initiale | Travail restant |
| --- | --- | --- |
| `translator/translator` | Conserver le CLI Rust et l'émission SPIR-V | Qualifier de vrais AIR compilés sur le Mac et leurs résultats AMD |
| `translator/wrapper` | Auditer séparément | Adaptations de bandes de descripteurs, adresses et spécialisations propres au plugin ; pas utilisé par le banc actuel |
| `plugin/NVMTLObjects.m`, `NVMTLEventSync.m`, contrats `nvmtl_*.h` | Candidats à réutiliser | Ressources/commandes exprimées avec Vulkan, mais hypothèses de mémoire et de synchronisation à vérifier |
| `plugin/nvmtl_vk.c` | Séparer les fonctions Vulkan des extensions NVK | Chargement ICD, partage mémoire, imports, capacités et chemins SASS |
| `plugin/NVMTLVendorCompiler.m`, `NVMTLLibraryLoad.m` | Désactiver la voie NVIDIA dans le futur backend AMD | `NVIDIAShared`, NVVM/cubins et ciblage SM ne produisent pas de code AMD |
| `plugin/NVMTLGL.m` | Hors prototype de calcul | Chemins LLVM/plugin externes à qualifier après Metal |
| `plugin/NVMTLDevice.m` | Adapter identification et capacités | Enregistrement H.264/HEVC NVENC, liens IOSurface/VRAM et propriétés Metal |
| `kexts/NVRM*`, `nvk/nvk-macos.patch` | Remplacer pour AMD | Aucun de ces composants ne doit piloter Navi 48 |
| `app/`, `package/` | Garder hors du paquet AMD initial | Installation, paramètres OpenCore et récupération actuellement NVIDIA |

### Dépendances directes repérées

Dans `plugin/nvmtl_vk.c`, le loader/ICD sont sous `/Library/GPUBundles/nvmtl`
et pointent sur `nvk_icd.json` / `libvulkan_nouveau.dylib`. Les exports suivants
viennent des modifications de NVK, pas de Vulkan standard :

- `nvmtl_nvk_stage_import`, `nvmtl_nvk_surface_dirty` : import de VRAM IOSurface et suivi des écritures.
- `nvmtl_nvk_sample_depth_contract_v1`, `nvmtl_nvk_sample_depth_image_v1` : contrat de profondeur/MSAA.
- `nvmtl_nvk_sparse_bind_v1` : admission des allocations clairsemées.
- `nvmtl_nvk_dispatch_sass_v1` : exécution de code machine NVIDIA.

Le partage actuel combine aussi `VK_KHR_external_memory_fd`, des modifiers
linéaires DRM, un enregistrement privé `nvmtl_vram_rec` et les ABI NVRM.
Les chemins IOSurface de `NVMTLDevice.m` et `NVMTLObjects.m` utilisent seeds,
plans, pitch, copies et génération VRAM. Ces fonctions ne deviennent pas AMD
par changement du nom de la bibliothèque.

Les scripts historiques sous `build/` supposent `$HOME/nvmtl-build`, des arbres
externes `LIVE`, des listes d'exports et `shipcheck.py` absents. Les nouveaux
scripts `tools/build-translator.*` utilisent le manifeste livré, son lockfile
et un répertoire de sortie paramétrable. Ils ne construisent pas les kexts.

## Contrat proposé pour RADV Darwin / N48N

Le backend Metal devra choisir un backend explicite et refuser l'absence de ses
contrats obligatoires. Le prototype AMD suit ce périmètre :

| Domaine | Contrat requis et preuve attendue |
| --- | --- |
| Identité | Vendor AMD `0x1002`, ID et sous-système relevés sur le PC ; correspondance avec Navi 48 ; aucun choix implicite d'un autre GPU |
| Connexion | `IOServiceOpen` avec le type N48N `0x4E34384E`, puis `Hello` ; major 1 et minor demandé via `N48N_HELLO_F_MINOR` ; exiger 1.9 pour l'import mémoire hôte |
| Buffers | Handles appartenant au client, taille réelle, placement VRAM/GTT, mapping CPU explicite et durée de vie jusqu'à la dernière complétion |
| Mémoire GPU | `BOCREATE`, `GEMVA` et protections R/W/X ; pas de conversion d'un pointeur CPU en adresse GPU sans mapping ; zéro dans les champs réservés |
| Import hôte | Sélecteur 21 : pages de 4 KiB, 1 page à 64 MiB par BO ; vérifier limites réellement annoncées et libération après complétion |
| Commandes | Contexte valide, IB et références BO contrôlés, nombre/tailles bornés ; échec explicite si une opération n'est pas implémentée |
| Synchronisation | `SUBMIT` puis `WAITSEQ` borné ; résultat relu ; barrières Vulkan et cohérence CPU/GPU ; ne pas annoncer les événements Metal avant qualification |
| Images | Allocation/format/pitch/tiling validés par RADV ; rendu hors écran et copie de lecture avant import IOSurface |
| Partage IOSurface | Contrat AMD versionné couvrant propriété, planes, offsets, pitch, visibilité et synchronisation interprocessus ; les exports NVK sont remplacés ou indisponibles |
| Présentation | Différée après le rendu hors écran ; scanout selectors 9..14 et restauration de la console à qualifier sur un seul connecteur |
| Capacités | Calcul entier et buffers seulement pour le premier banc ; profondeur/MSAA, sparse, SASS, vidéo et fonctions Metal avancées ne sont pas déduites des annonces amont |

Le header ABI public est en 1.9. Ses commentaires contiennent des limites anciennes
et des ajouts plus récents ; les capacités et plafonds devront être confirmés sur
la révision effectivement construite. `notes/design/NATIVE-S1C-ABI.md`, annoncé
comme normatif par ce header, est absent à cette révision publique (HTTP 404).
Il faut résoudre ce manque avant de déclarer le transport qualifié.

Sources AMD consultées : [instructions RADV](https://github.com/Almosst-DEV/Navi48-MacOS/blob/696959753070e9a16677be485580dc53b06a7ab5/mesa-patches/README.txt),
[ABI N48N](https://github.com/Almosst-DEV/Navi48-MacOS/blob/696959753070e9a16677be485580dc53b06a7ab5/src/navi48-bringup/src/Navi48NativeABI.h),
[compilation](https://github.com/Almosst-DEV/Navi48-MacOS/blob/696959753070e9a16677be485580dc53b06a7ab5/BUILDING.txt).

## Licences et provenance

Inventaire des notices, sans conclusion juridique sur un futur paquet :

| Composant | Notice à conserver / source |
| --- | --- |
| Couche Metal, nouveaux outils et code du fork hors exceptions | `LICENSE` : PolyForm Noncommercial 1.0.0 ; en-têtes d'origine conservés |
| Traducteur Rust et wrapper | `translator/LICENSE`, `translator/COPYING` : LGPL-3.0-or-later ; origine metal2vulkan et modifications NullMoth dans `NOTICE` |
| Patch Mesa NVK et futurs patches RADV | Notices Mesa et en-têtes des fichiers ; vérifier les licences fichier par fichier |
| Navi48-MacOS | MIT, licence racine et notices propres aux sous-composants |
| RDNA4FB / découverte IP | BSD-3-Clause, notice de Sunneva N. Mariu |
| Séquences mac-amdgpu | MIT, notice de Geramy Loveless |
| Registres AMD dérivés | En-têtes MIT AMD ; conserver leur provenance |
| Firmwares AMD | `LICENSE.amdgpu`, empreintes et révision de récupération ; aucun binaire firmware ajouté ici |
| MacKernelSDK | Révision, licence et notices à inspecter au moment du choix du checkout |

Voir les [notices AMD](https://github.com/Almosst-DEV/Navi48-MacOS/blob/696959753070e9a16677be485580dc53b06a7ab5/third-party/THIRD-PARTY-LICENSES.txt)
et la [liste des firmwares et empreintes](https://github.com/Almosst-DEV/Navi48-MacOS/blob/696959753070e9a16677be485580dc53b06a7ab5/tools/fetch-firmware.sh).
Les révisions SDK/firmware restent `null` dans le manifeste : les empreintes
attendues ne prouvent pas que ces firmwares ont été obtenus ou testés.
