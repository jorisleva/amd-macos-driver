# AMD RX 9070 XT sur macOS Tahoe

Projet expérimental de développement d'un pilote graphique pour une **AMD Radeon RX 9070 XT**, sur un PC **Ryzen 5 5600X** démarrant macOS Tahoe avec OpenCore.

L'objectif est de réutiliser la couche Metal du fork NVIDIA de NullMoth, puis de la raccorder à un backend AMD composé de **Mesa RADV** et d'un pilote noyau pour Navi 48.

**Statut : premier banc de validation Windows et compilation du traducteur.** Le CLI Rust compile, 10 tests logiciels ciblés passent et le banc Vulkan x64 compile avec 4 tests logiciels réussis. Le refus d'une Radeon absente est vérifié. Aucun calcul sur RX 9070 XT, aucune compilation macOS ni aucun essai matériel du futur assemblage AMD n'a été réalisé. Ce dossier ne fournit pas encore de pilote installable ; le fork contient toujours le pilote NVIDIA.

La progression et les conditions de passage entre étapes sont décrites dans [ROADMAP.md](ROADMAP.md).

Le [guide du banc Windows](docs/VALIDATION-WINDOWS.md) donne les commandes de compilation, d'inventaire et de test. L'[audit AMD](docs/AMD-INTEGRATION.md), le [manifeste des sources](dependencies/sources.lock.json) et le [premier rapport](docs/reports/2026-10-07-bootstrap.md) distinguent les résultats obtenus des dépendances et essais encore manquants.

Les prochaines actions et blocages sont résumés dans [Travail restant](docs/NEXT-STEPS.md).

## Configuration cible

| Élément | Configuration |
| --- | --- |
| Processeur | AMD Ryzen 5 5600X |
| Carte graphique | AMD Radeon RX 9070 XT, RDNA 4 / Navi 48 |
| Système actuel du PC | Windows |
| Système cible | macOS Tahoe 26.x, version et numéro de build à fixer |
| Démarrage cible | OpenCore avec les correctifs CPU AMD adaptés |
| Machine de compilation | Un autre Mac est disponible ; modèle et système à relever |
| Carte mère et BIOS | À renseigner |
| Fabricant de la carte et VBIOS | À relever sur le matériel |
| Premier affichage à valider | Un seul écran et un seul connecteur, à choisir |

La compatibilité du CPU avec les correctifs OpenCore ne garantit pas celle de l'ensemble du PC. Le démarrage de Tahoe devra être validé séparément du pilote graphique.

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
