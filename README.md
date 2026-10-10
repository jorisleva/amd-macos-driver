# AMD RX 9070 XT sur macOS Tahoe

Projet expérimental de développement d'un pilote graphique pour une **AMD Radeon RX 9070 XT**, sur un PC **Ryzen 5 5600X** démarrant macOS Tahoe avec OpenCore.

L'objectif est de réutiliser la couche Metal du fork NVIDIA de NullMoth, puis de la raccorder à un backend AMD composé de **Mesa RADV** et d'un pilote noyau pour Navi 48.

**Statut : calcul et rendu Apple AIR validés sur la RX 9070 XT sous Windows.** Les quatre shaders graphiques compilés sur Mac et traduits en SPIR-V passent les 48 cas du banc Radeon : 330 984 pixels RGBA exactement conformes, aucun écart de gardes/entrées et aucune erreur Vulkan/synchronisation. Leurs 96 fichiers RGBA sont identiques à ceux du contrôle GLSL rejoué. Le calcul Apple et le contrôle GLSL passent 36 cas chacun ; les trois rejets attendus, 15 tests Rust ciblés, 13 CTest et huit tests Python passent sous Windows. Preuves : [rapport Metal/Radeon](docs/reports/2026-10-07-metal-graphics-radeon.md). Tahoe installé démarre maintenant sur ce PC, sans accélération. Navi48Bringup est construit deux fois pour x86_64 et contrôlé statiquement ; son chargement, RADV Darwin et le rendu via Metal sous macOS restent à qualifier. Ce dossier ne fournit pas encore de pilote installable ; le fork contient toujours le pilote NVIDIA.

**Dernier état natif : `Navi48Native.kext` 0.2.3 sur PROBE1401, prêt pour le prochain boot.** Le service
appelle désormais l'initialisation PSP/GMC/SMU/IMU/RLC/CP/MES/GFX puis deux
shaders gfx1201, fences et comparaison de 64 résultats. **Premier boot observé
le 10 octobre : module 0.2.0 réellement chargé, UUID/arguments conformes, mais
service retiré, aucun rapport de calcul ni résultat GPU.** Capture privilégiée
lue : buffer de boot écrasé, code de refus inconnu. Correctif **0.2.1** avec
boot 0.2.2 réellement lu : `DescriptorMismatch` sur BAR0 (`MapCheck=3`),
autres propriétés BAR0 conformes, zéro hardware/DMA/compute. **0.2.3
déployé/vérifié, ancienne EFI sauvegardée, non encore chargé** : le prochain
boot tranchera substitution provider vs mapping partagé. Aucun calcul Radeon,
init ou fence validé ([boot 0.2.2](docs/reports/2026-10-10-native-0.2.2-boot.md) ·
[déploiement 0.2.3](docs/reports/2026-10-10-native-descriptor-deployment.md)).
Étapes matérielles 1/2 non terminées, pas de Metal/WindowServer ni API de calcul libre. Essai
matériel explicitement risqué, ressources conservées jusqu'au reboot ; OPENCORE
intact et ancien essai 0.1.2 sauvegardé. **Procédure actuelle :
[essai initialisation/calcul et diagnostic 0.2.1](docs/NATIVE-COMPUTE-ESSAI.md)** ·
[préparation](docs/reports/2026-10-09-native-compute.md) ·
[résultat du premier boot](docs/reports/2026-10-10-native-compute-boot.md).

Les sections datées ci-dessous conservent l'historique, pas l'état actuel de la
clé. La progression et les conditions de passage sont dans [ROADMAP.md](ROADMAP.md).

**9 octobre 2026 : travail effectué sur le Hackintosh Tahoe 26.7.1 / 25G241.**
L'EFI active est sauvegardée sur OPENCORE et localement, avec 98 fichiers
vérifiés. Navi48Bringup 0.0.620 compile ; les dix firmwares intégrés, 2 905
contrôles logiciels sous ASan/UBSan et 160 mutations sont vérifiés. **À cette phase,
aucun kext expérimental installé ou chargé**, aucun périphérique Metal. L'audit relève des
écritures même en mode d'inventaire et des personnalités Apple à isoler avant
un essai. Voir le [rapport de build et d'audit](docs/reports/2026-10-09-navi48-build-audit.md).
Historique du démarrage : [rapport RapidEFI](docs/reports/2026-10-09-rapidefi.md).

**Isolation PCI réalisée :** [Navi48PciProbe 0.1.0](kexts/Navi48PciProbe/)
observe uniquement les propriétés IORegistry de notre carte, sans accès PCI
ou GPU direct. Deux bundles x86_64 identiques, 1 048 contrôles ASan/UBSan,
25 mutations détectées et 31 tests Python. **Non chargé à cette phase** ; l'essai du secours
est différé à la demande de l'utilisateur. Un écart OpenCore distinct du build
est conservé et documenté dans le [rapport d'isolation](docs/reports/2026-10-09-pci-probe-isolation.md).

**EFI d'essai préparées, pas activées :** référence actuelle + variantes OFF/ON,
315 fichiers vérifiés localement et dans `OPENCORE/PROFILS-TAHOE`, sans modifier
l'EFI active. `ocvalidate` réussi, 294 noms d'imports présents dans le BootKC,
44 tests Python ; chargement toujours non qualifié.
[Mode d'emploi](docs/PCI-PROBE-EFI-ESSAI.md) · [Rapport](docs/reports/2026-10-09-pci-probe-efi.md).

**Premier démarrage OFF réussi sur PROBE1401** : module 0.1.0 réellement chargé
(`kmutil showloaded`, UUID conforme), argument `navi48-pci-probe=0` reçu, aucun
nœud attaché. Les 104 fichiers de la clé et les 101 fichiers d'OPENCORE sont
inchangés à cette vérification. Ces résultats portent sur le cas OFF.
Ce chargement ne valide pas l'accélération ou le pilote Navi48Bringup complet.
[Déploiement USB](docs/reports/2026-10-09-probe1401-deployment.md) ·
[Résultat OFF](docs/reports/2026-10-09-pci-probe-off-boot.md).

**Premier boot ON conforme à 11:05 UTC :** module attaché à la Radeon, six
champs d'identité et cinq ressources identiques au relevé PCI du même boot.
Les flags signés de l'export XML sont interprétés sur 32 bits ; les adresses
64 bits sont préservées. 52 tests Python passent. **Observation IORegistry
validée pour ce boot, pas d'initialisation GPU ni d'accélération.**
[Passage à ON](docs/reports/2026-10-09-pci-probe-on-deployment.md) ·
[Résultat ON](docs/reports/2026-10-09-pci-probe-on-boot.md).

**Retour OPENCORE vérifié à 11:31 UTC ; première coupe native construite :**
la session n'a plus l'argument, le module ni le nœud d'observation. Les deux
EFI restent intactes ; PROBE1401 conserve son profil ON. Le bloc
[Navi48FirmwareCore](native/Navi48FirmwareCore/) compile en bibliothèque
**non chargeable**, sans hooks Apple ni client utilisateur : deux builds
identiques, dix firmwares vérifiés, zéro avertissement, 1 582 contrôles locaux
ASan/UBSan, 22 mutations détectées et 61 tests Python. À cette première phase,
le modèle de refus n'est pas raccordé aux accès ; aucun firmware envoyé ni GPU
initialisé. [Rapport et limites](docs/reports/2026-10-09-native-isolation.md).

**Accès natifs maintenant raccordés :** interface BAR0/BAR2/MMIO remplacée par
des accès bornés, liaison des préconditions aux mappings déclarés, code PSP
adapté pour le layout, la copie firmware et la soumission GPCOM. Le banc exécute
ce même code sur RAM et vérifie les données/trames, avec réponse PSP simulée :
398 contrôles ASan/UBSan, 63 tests Python, deux builds identiques sans avertissement.
**Pas d'exécution GPU** à cette étape : preuves et contrôleur IOKit réels, DMA et
arrêt restent à réaliser. EFI intactes. [Code et résultat](docs/reports/2026-10-09-native-access.md).

**Contrôleur de mappings IOKit maintenant codé, séparément :** acquisition et
conservation des ressources BAR0/BAR2/BAR5, contrôles PCI/descripteur/mapping,
placement console rapporté et nettoyage partiel sérialisé. Deux archives
identiques sans avertissement ; 2 241 contrôles ASan/UBSan sur doubles IOKit,
66 tests Python. **Aucun mapping réel exécuté, aucun firmware autorisé** :
bail IOKit et cache demandé ne prouvent ni réservation VRAM ni cohérence HDP.
Le contexte reste privé et désactivé. OPENCORE/PROBE1401 inchangés.
[Contrat](native/Navi48FirmwareCore/IOKIT-CONTROLLER.md) ·
[Résultat et limites](docs/reports/2026-10-09-native-platform.md).

**Premier vrai kext natif créé : `Navi48Native.kext` 0.1.0.** Service IOKit,
personnalité PCI, points d'entrée et contrôleur raccordés ; le code PSP/SMU/IMU
et dix firmwares sont liés dans le binaire final. Bundle x86_64 compilé sans
avertissement, signé ad hoc, non installé/non chargé. L'acquisition est opt-in ;
**l'initialisation GPU reste bloquée** sur les conditions matérielles manquantes.
L'étape 1 et le bureau accéléré ne sont pas déclarés réussis.
[Livrable et prochaine cible matérielle](docs/reports/2026-10-09-native-kext.md) ·
[Code et compilation](kexts/Navi48Native/).

**Suite 0.1.1 : adaptateur de mémoire DMA codé et lié.** Les adresses IOVM
proviennent d'IODMACommand, sans hypothèse CPU physique = adresse GPU.
**À cette phase, pas encore appelé par le service, pas de transfert Radeon ni commande GPU** :
les étapes matérielles 1 et 2 restent ouvertes. [État précis](docs/reports/2026-10-09-native-dma.md).

**Suite 0.1.2 : DMA raccordée et EFI native déployée sur PROBE1401.** `start()`
prépare 64 Kio de RAM via IODMACommand hors gate, gère l'annulation et revalide
le provider ; nettoyage DMA avant fermeture PCI. 234 contrôles service/DMA,
2 271 contrôleur et 76 tests Python passent ; bundle final sans avertissement,
signé, dix firmwares vérifiés. À ce checkpoint, la clé devait injecter le kext au prochain démarrage :
112 fichiers conformes, ancienne EFI sauvegardée, 101 fichiers OPENCORE intacts.
**Pas encore chargé dans le noyau courant, aucun firmware envoyé ni calcul GPU.**
[Boot à effectuer et vérification](docs/NATIVE-KEXT-ESSAI.md) ·
[Rapport](docs/reports/2026-10-09-native-load-preparation.md).

Le [guide du banc Windows](docs/VALIDATION-WINDOWS.md) donne les commandes d'inventaire et de test ; la [procédure Mac](docs/VALIDATION-MACOS.md) décrit la compilation AIR et le transfert. Le [rapport Apple AIR sur Radeon](docs/reports/2026-10-07-apple-air-radeon.md) conserve la nouvelle validation matérielle. Le [rapport Mac/AIR](docs/reports/2026-10-07-apple-air.md), le [corpus Apple](tests/shaders/apple/) et le [rapport Radeon initial](docs/reports/2026-10-07-radeon-windows.md) conservent les artefacts et résultats précédents. L'[audit AMD](docs/AMD-INTEGRATION.md), le [manifeste des sources](dependencies/sources.lock.json) et le [premier rapport](docs/reports/2026-10-07-bootstrap.md) décrivent les dépendances et l'historique de validation.

Le [banc graphique hors écran](docs/VALIDATION-GRAPHICS.md) et son [rapport Radeon avec images](docs/reports/2026-10-07-offscreen-radeon.md) décrivent les 48 cas, les tolérances et les rejets de résultats volontairement faux. Le [rapport graphique Metal/Mac](docs/reports/2026-10-07-metal-graphics.md) et le [nouveau corpus](tests/shaders/apple/graphics/) conservent les quatre équivalents AIR et leur ABI.

Les prochaines actions et blocages sont résumés dans [Travail restant](docs/NEXT-STEPS.md).

La [préparation OpenCore pour ce PC](docs/OPENCORE-TAHOE.md) fournit maintenant
un générateur d'EFI de référence sans accélération et un profil de secours.
Le support USB est préparé avec récupération Apple et paquet complet séparé ;
les configurations copiées passent `ocvalidate` 1.0.8. Le bureau installé est
observé, mais les démarrages répétés et le secours restent à essayer. Le SDK
et les dix firmwares sont épinglés, vérifiés et utilisés dans le build Navi48 : [préparation des dépendances AMD](docs/AMD-DEPENDENCIES.md),
[rapport de préparation](docs/reports/2026-10-07-boot-preparation.md).

## Configuration cible

| Élément | Configuration |
| --- | --- |
| Processeur | AMD Ryzen 5 5600X |
| Carte graphique | AMD Radeon RX 9070 XT, RDNA 4 / Navi 48 |
| Système actuel du PC | macOS Tahoe 26.7.1 / 25G241, x86_64, sans accélération |
| Banc Windows de référence | Windows 11 Professionnel, build 26300 ; validations Radeon conservées |
| Démarrage cible | OpenCore avec les correctifs CPU AMD adaptés |
| Compilation Navi48 actuelle | Hackintosh cible, Apple Clang 21.0.0, SDK macOS 26.5, MacKernelSDK épinglé |
| Compilation AIR de référence | MacBookAir7,2 x86_64, 8 Gio, macOS 15.7.8 / 24G824, Xcode 26.3 / 17C529, SDK macOS 26.2 |
| Carte mère et BIOS | Gigabyte B550M DS3H, BIOS FD du 22 mars 2024 ; révision matérielle à préciser |
| RAM relevée | 16 Gio, 2 × 8 Gio à 3200 MT/s |
| Identité PCI relevée | `1002:7550`, sous-système `1849:5417` |
| Pilote Windows relevé | `32.0.31041.1004`, Vulkan AMD `26.8.1 (LLPC)`, API `1.4.349` |
| Fabricant de la carte et VBIOS | ASRock, `023.008.000.068.000001`, part number `113-APM107819-101` selon ACPI VFCT ; modèle commercial à préciser |
| Stockage et réseau relevés | Crucial P3 Plus 1 To NVMe ; Realtek Ethernet `10EC:8168` |
| Affichage observé sous Tahoe | 1920 × 1080, IONDRVFramebuffer ; DisplayPort selon le relevé précédent |

La session Tahoe observée ne qualifie ni les redémarrages répétés ni la récupération. Ces essais restent distincts de la validation du pilote graphique.

## Objectif et périmètre

Le premier objectif matériel est un calcul dont le résultat est correctement produit par la RX 9070 XT sous Tahoe. Le premier objectif Metal est ensuite une application de test qui effectue ce calcul et un rendu hors écran sur cette même carte.

Le bureau accéléré, les applications réelles et les fonctions avancées seront intégrés progressivement.

Le périmètre initial se limite à la configuration cible. La prise en charge d'autres Radeon, le multi-écran, le ray tracing, les fonctions avancées de Metal et l'accélération vidéo ne constituent pas des prérequis au premier prototype.

## Bases techniques retenues

### Fork NVIDIA

[jorisleva/amd-macos-driver](https://github.com/jorisleva/amd-macos-driver) est un fork de [nullmoth/nvidia-macos-driver](https://github.com/nullmoth/nvidia-macos-driver).

Le projet amont annonce une couche Metal, un traducteur AIR vers SPIR-V, un port macOS de NVK et des extensions noyau NVIDIA. Les tests indiqués par son auteur concernent une RTX 5060 sous Sequoia 15.7.x. Ces déclarations ne constituent pas une validation de la RX 9070 XT ni de Tahoe.

Les parties candidates à la réutilisation sont le traducteur de shaders, les objets Metal et les opérations déjà exprimées avec Vulkan. Les chemins NVIDIA spécifiques devront être adaptés, remplacés ou désactivés.

### Base AMD

[Almosst-DEV/Navi48-MacOS](https://github.com/Almosst-DEV/Navi48-MacOS) publie un pilote noyau Navi 48, des adaptations de RADV pour Darwin et une couche Metal expérimentale.

Son auteur annonce un bureau composé par le GPU à 60 images/s sur une RX 9070 XT, un écran DisplayPort et Tahoe 26.6.2. Le projet indique également que l'installation n'est pas encore fiable et que le rendu natif général des applications reste à développer. Ces résultats devront être reproduits sur notre matériel.

Cette base servira à étudier et intégrer les fonctions AMD. La branche native RADV sera privilégiée ; le prototype initial ne dépendra pas des caches de shaders Apple absents du dépôt public pour son ancienne voie de substitution.

### Pourquoi Vulkan ne suffit pas à lui seul

Vulkan offre une interface commune aux applications. Son implémentation et la communication avec le GPU restent propres au matériel et au système.

RADV dépend normalement du pilote Linux amdgpu. Sur macOS, il faut un backend qui communique avec le pilote noyau Darwin. Il faut également adapter le partage des images avec macOS, la synchronisation et l'affichage.

## Architecture proposée

```text
Application Metal / WindowServer
               |
      Couche Metal du fork
               |
     Appels Vulkan + shaders SPIR-V
               |
      Mesa RADV + compilateur ACO
               |
      Backend Darwin / interface N48N
               |
      Pilote noyau AMD pour Navi 48
               |
          AMD RX 9070 XT

Shaders Metal -> AIR -> traducteur du fork -> SPIR-V -> RADV / ACO
```

| Composant | Stratégie |
| --- | --- |
| Traduction AIR vers SPIR-V | Réutiliser, tester sur AMD et corriger les incompatibilités |
| Objets et opérations Metal | Conserver ce qui est indépendant de NVIDIA |
| Chargement du pilote Vulkan | Ajouter un backend RADV explicite |
| Allocation et partage de mémoire | Adapter au pilote AMD et aux images IOSurface |
| Synchronisation et soumission | Vérifier les contrats Metal, Vulkan et noyau ensemble |
| Initialisation du GPU et affichage | S'appuyer sur le travail Navi48-MacOS, après audit et reproduction |
| Compilation ou fonctions vidéo NVIDIA | Remplacer ou désactiver explicitement au début |

Le code du fork importe notamment des fonctions ajoutées à NVK pour partager les surfaces. Changer uniquement le nom de la bibliothèque Vulkan ne remplacera pas ces contrats.

## Environnements de développement

### PC sous Windows

- Édition du code et suivi du projet avec Git.
- Tests de shaders SPIR-V sur la RX 9070 XT avec le pilote Vulkan AMD Windows.
- Relevé de l'identité matérielle, du BIOS et des informations disponibles sur la carte.
- Outils proposés : compilateur C/C++, Vulkan SDK et outils SPIR-V, Rust pour le traducteur.

Ces essais valident la traduction et certains comportements GPU. Ils ne valident pas le pilote AMD pour macOS.

### Autre Mac

- Compilation des shaders Metal de référence et des composants macOS.
- Construction des extensions noyau, du backend RADV et du bundle Metal pour **x86_64**.
- Tests logiciels et comparaison des résultats avec le GPU du Mac, lorsque les fonctions testées y sont disponibles.
- Outils : Xcode et ses outils de ligne de commande, MacKernelSDK, Rust, Meson/Ninja et dépendances de Mesa.

Les scripts AMD prévoient une compilation x86_64 depuis un Mac Apple Silicon. Les dépendances devront elles aussi être construites pour x86_64. Compiler un binaire ne prouve pas son fonctionnement sur le PC.

### PC sous Tahoe

- Démarrage OpenCore et validation du système de base.
- Essais réels d'identification PCI, d'initialisation GPU, de mémoire et de commandes.
- Validation de Vulkan, de Metal, puis du bureau et des applications.

Un disque de test et un démarrage de secours permettant de désactiver le pilote seront préparés avant les essais noyau. Le 5600X nécessite une carte graphique dédiée : l'affichage de secours ne peut pas reposer sur un GPU intégré au processeur.

## Méthode de validation

Un test doit vérifier son résultat, pas seulement l'absence de plantage.

- Sélectionner explicitement la RX 9070 XT et relever l'identité du périphérique utilisé.
- Refuser un périphérique logiciel ou un remplacement silencieux par le CPU.
- Comparer les données et les images à une référence connue.
- Définir les tolérances numériques pour les calculs en virgule flottante.
- Vérifier les limites de buffers, la synchronisation et la libération des ressources.
- Rejouer les tests après une modification du traducteur, de Mesa ou du pilote.
- Distinguer compilation, test logiciel, exécution GPU et validation d'une application.

Une déclaration « Metal pris en charge » ou un périphérique présent dans les informations système ne suffit pas à démontrer une accélération fonctionnelle.

Chaque essai conservera au minimum : révision du code, versions des outils, build de macOS ou de Windows, identité GPU, firmwares et empreintes, options du pilote, test exécuté, résultat et journaux utiles. Les rapports publics devront être débarrassés des identifiants personnels et des secrets.

## Premier livrable prévu

1. Une compilation reproductible à partir d'un dépôt propre.
2. Un petit programme Vulkan Windows sélectionnant la RX 9070 XT.
3. Des shaders Metal de référence compilés sur le Mac, puis traduits en SPIR-V.
4. Un rapport de comparaison pour un calcul simple et un triangle hors écran.

Ce livrable permet de commencer les validations matérielles pendant la préparation de Tahoe.

## Dépendances et licences

Conserver les licences, notices et origines des composants intégrés. Le fork NVIDIA indique notamment PolyForm Noncommercial pour plusieurs composants, LGPL-3.0-or-later pour le traducteur et MIT pour ses modifications de Mesa. Les composants AMD et les firmwares possèdent leurs propres conditions.

Le choix d'une licence pour ce travail ne remplace pas celles des dépendances. Les conditions de redistribution devront être vérifiées avant de publier un paquet contenant du code ou des firmwares tiers.

## Références et état des sources

Cadrage établi le **7 octobre 2026**. Révisions consultées :

| Dépôt | Révision |
| --- | --- |
| Fork NVIDIA | [de83e37760e300f53727e56a8ef7e028f1e810f5](https://github.com/jorisleva/amd-macos-driver/commit/de83e37760e300f53727e56a8ef7e028f1e810f5) |
| Navi48-MacOS | [696959753070e9a16677be485580dc53b06a7ab5](https://github.com/Almosst-DEV/Navi48-MacOS/commit/696959753070e9a16677be485580dc53b06a7ab5) |

Ces révisions sont des points de départ pour l'audit. Une mise à jour amont doit faire l'objet d'une nouvelle qualification.

- [Architecture du pilote NVIDIA](https://github.com/jorisleva/amd-macos-driver/blob/de83e37760e300f53727e56a8ef7e028f1e810f5/docs/HOW-IT-WORKS.md)
- [Documentation du traducteur](https://github.com/jorisleva/amd-macos-driver/blob/de83e37760e300f53727e56a8ef7e028f1e810f5/translator/translator/README.md)
- [État du prototype AMD](https://github.com/Almosst-DEV/Navi48-MacOS/blob/696959753070e9a16677be485580dc53b06a7ab5/README.txt)
- [Compilation du pilote AMD](https://github.com/Almosst-DEV/Navi48-MacOS/blob/696959753070e9a16677be485580dc53b06a7ab5/BUILDING.txt)
- [Port de RADV pour Darwin](https://github.com/Almosst-DEV/Navi48-MacOS/blob/696959753070e9a16677be485580dc53b06a7ab5/mesa-patches/README.txt)
- [Architecture et responsabilités de RADV](https://docs.mesa3d.org/drivers/radv.html)
- [Correctifs CPU AMD pour OpenCore](https://github.com/AMD-OSX/AMD_Vanilla)
- [Caractéristiques du Ryzen 5 5600X](https://www.amd.com/en/support/downloads/drivers.html/processors/ryzen/ryzen-5000-series/amd-ryzen-5-5600x.html)
