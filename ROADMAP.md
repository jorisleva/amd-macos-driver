# Roadmap — RX 9070 XT sur macOS Tahoe

Cette feuille de route décrit le développement pour le PC **Ryzen 5 5600X + RX 9070 XT**, désormais démarré sous Tahoe. Windows reste le banc de référence des shaders ; un autre Mac a produit le corpus AIR.

L'architecture, le périmètre et les références sont détaillés dans [README.md](README.md).

**État au 7 octobre 2026 : calcul et rendu Apple AIR validés sur la Radeon sous Windows.** Le nouveau corpus graphique passe 48 cas, avec 330 984 pixels exacts, zéro écart et zéro erreur Vulkan/synchronisation ; ses 96 fichiers RGBA sont identiques au contrôle GLSL rejoué. Les deux calculs passent 36 cas chacun, les trois rejets attendus passent, ainsi que 15 tests Rust ciblés, 13 CTest et huit tests Python. Le support USB OpenCore/Tahoe est préparé et vérifié. Le boot physique, Navi48, RADV Darwin et la couche Metal sous macOS restent ouverts ; aucune étape globale macOS n’est déclarée complète. Preuves : [rapport Metal/Radeon et USB](docs/reports/2026-10-07-metal-graphics-radeon.md), [rapport Metal/Mac](docs/reports/2026-10-07-metal-graphics.md) et [rapport Apple AIR/Radeon](docs/reports/2026-10-07-apple-air-radeon.md).

**Mise à jour du 9 octobre :** Tahoe 26.7.1 / 25G241 est exécuté sur le PC,
sans accélération. L'EFI est sauvegardée sur OPENCORE et localement. Deux
builds Navi48 x86_64, les dix firmwares liés, 2 905 contrôles logiciels et 160
mutations sont vérifiés ; aucun chargement. Le secours et les démarrages
répétés restent à qualifier : l'étape 3 n'est pas clôturée. L'audit refuse
l'usage du bundle amont comme module PCI strictement passif.
Voir le [rapport Navi48](docs/reports/2026-10-09-navi48-build-audit.md).

## Principes de progression

- Chaque étape doit produire un résultat mesurable et un rapport reproductible.
- Une compilation réussie ne valide pas le matériel.
- Les tests GPU refusent les périphériques logiciels et les remplacements silencieux par le CPU.
- Les premiers tests graphiques rendent hors écran ; l'intégration du bureau vient ensuite.
- Le pilote cible d'abord l'identité matérielle effectivement relevée sur le PC.
- Une fonction non implémentée doit signaler son indisponibilité et ne doit pas être annoncée comme fonctionnelle.
- Les délais seront réévalués après reproduction des briques AMD. Aucun calendrier ferme n'est fixé à ce stade.

## Vue d'ensemble

| Étape | Résultat attendu | Lieu principal |
| --- | --- | --- |
| 0 | Inventaire, architecture et dépendances fixés | Windows / Mac |
| 1 | Compilation reproductible et tests logiciels | Mac |
| 2 | Shaders traduits correctement exécutés sur la Radeon | Windows + Mac |
| 3 | Tahoe démarre et la carte est correctement identifiée | PC sous Tahoe |
| 4 | Mémoire et commandes GPU natives fonctionnelles | PC sous Tahoe |
| 5 | RADV exécute des calculs et rend hors écran | PC sous Tahoe |
| 6 | La couche Metal du fork fonctionne avec AMD | PC sous Tahoe |
| 7 | Bureau accéléré et partage des images corrects | PC sous Tahoe |
| 8 | Version alpha qualifiée pour la configuration cible | PC sous Tahoe |

La préparation de Tahoe peut avancer en parallèle des étapes 1 et 2. L'intégration Metal dépend de la validation du backend AMD sous Tahoe.

## Étape 0 — Cadrer la configuration et l'assemblage

- [ ] Relever le modèle de carte mère, la version du BIOS, la RAM et les périphériques nécessaires au démarrage (B550M DS3H, BIOS FD, 16 Gio, Crucial P3 Plus 1 To, Realtek Ethernet et deux contrôleurs USB relevés ; révision PCB et supports d'installation à choisir).
- [ ] Relever le fabricant de la Radeon, son VBIOS, ses identifiants PCI et de sous-système (`1002:7550` / `1849:5417`, ASRock et VBIOS `023.008.000.068.000001` relevés via ACPI VFCT ; modèle commercial et confirmation sur l'étiquette à compléter).
- [ ] Choisir l'écran et le connecteur utilisés pour la première qualification.
- [x] Relever le modèle du Mac, son système, Xcode et les SDK disponibles (MacBookAir7,2 x86_64, 8 Gio, macOS 15.7.8 / 24G824, Xcode 26.3 / 17C529 et SDK macOS 26.2 ; [inventaire](docs/reports/2026-10-07-apple-air.md)).
- [x] Créer une branche de développement dédiée dans le fork (`codex/amd-validation-bootstrap`).
- [x] Fixer les révisions du fork, du projet AMD, de Mesa, de MacKernelSDK et des firmwares (SDK et dix blobs vérifiés jusque dans le build Navi48 ; [préparation](docs/AMD-DEPENDENCIES.md), Mesa et essais matériels encore ouverts).
- [x] Inventorier les fonctions Metal génériques et les dépendances directes à NVK, NVRM, NVIDIAShared et NVENC ([audit initial](docs/AMD-INTEGRATION.md), wrapper à approfondir).
- [x] Définir les contrats du backend AMD : buffers, images, mémoire partagée, commandes, synchronisation et présentation ([contrat proposé](docs/AMD-INTEGRATION.md), implémentation et qualification à faire).
- [x] Inventorier les licences et notices à conserver ([inventaire](docs/AMD-INTEGRATION.md), MacKernelSDK APSL 2.0 et notices firmware désormais récupérés ; redistribution du futur paquet à vérifier).

**Livrable :** inventaire matériel et logiciel, architecture d'intégration et liste des dépendances figées.

**Critère de passage :** chaque composant à conserver ou à remplacer est identifié ; les informations matérielles manquantes sont explicitement suivies.

## Étape 1 — Rendre la compilation reproductible

- [x] Compiler le traducteur Rust et lancer ses tests logiciels pertinents (Mac x86_64 et Windows : 15 tests réussis dont quatre régressions graphiques ; suite complète toujours bloquée par corpus absent).
- [x] Installer et qualifier les outils de compilation Metal et de validation SPIR-V pour le premier calcul (Apple Metal 32023.864, LLVM 20.1.8 et SPIRV-Tools v2026.2 essayés sur Mac ; SDK Vulkan 1.4.350.0 utilisé sur Windows ; [procédure Mac](docs/VALIDATION-MACOS.md)).
- [x] Compiler le pilote AMD pour x86_64 avec MacKernelSDK (Navi48 0.0.620 construit deux fois depuis des exports propres, signature et firmwares liés vérifiés ; [rapport](docs/reports/2026-10-09-navi48-build-audit.md), aucun chargement).
- [ ] Construire Mesa à la révision attendue et appliquer les adaptations RADV Darwin dans l'ordre prévu.
- [ ] Construire les outils de test et les composants Metal pour x86_64.
- [ ] Remplacer les chemins propres à la machine de l'auteur par des paramètres documentés.
- [ ] Traiter les fichiers et caches absents du dépôt public : générer ceux nécessaires à nos propres tests et identifier ceux qui restent nécessaires à l'intégration du bureau.
- [ ] Vérifier l'architecture des binaires, les bibliothèques requises, les symboles et la signature des bundles (contrôles statiques Navi48 faits ; imports noyau Tahoe et autres composants ouverts).
- [ ] Refaire la compilation depuis une copie propre, sans dépendre de fichiers non déclarés (deux builds Navi48 faits ; code/données identiques, binaires complets différents par métadonnées de debug/signature ; autres composants ouverts).
- [ ] Préparer des paquets versionnés avec leurs empreintes et leur provenance.

Les éléments produits ici sont des artefacts de test. Ils ne sont pas encore qualifiés pour être installés sur le PC.

**Livrable :** procédure de compilation, versions d'outils fixées, artefacts identifiés et rapport de tests logiciels.

**Critère de passage :** la compilation depuis une copie propre est reproductible ; chaque erreur, test ignoré ou dépendance manquante est expliqué. Aucun résultat matériel n'est déduit de cette étape.

## Étape 2 — Vérifier les shaders AMD sous Windows

- [x] Développer un petit programme Vulkan Windows sélectionnant explicitement la RX 9070 XT (exécuté sur la cible relevée, refus d'une identité différente vérifié).
- [x] Enregistrer l'identité du GPU, le pilote et les capacités utilisées (API, file, local size, Int64 et types/heaps mémoire dans le rapport Radeon).
- [x] Compiler sur le Mac les shaders Metal écrits pour le projet et conserver leurs sources et AIR (`vector_add` et quatre shaders graphiques, [corpus Apple](tests/shaders/apple/)).
- [x] Traduire ces shaders avec le traducteur du fork et valider le SPIR-V (LLVM 20.1.8, Vulkan 1.2 et contrat statique vérifiés ; calcul : 36 cas Radeon réussis ; graphiques Apple AIR : 48 cas réussis).
- [x] Exécuter une addition de tableaux et comparer tous les résultats à une référence CPU (SPIR-V issu d'Apple, contrôle GLSL et IR synthétique ; tolérance entière zéro).
- [x] Tester des tailles non multiples des groupes de travail et vérifier les zones de garde des buffers (1, 63, 64, 65, 257, 4097 éléments ; vérification des allocations entières).
- [x] Tester les transferts, barrières, lectures après écriture et commandes répétées (host/compute et staging/VRAM/compute/readback, 3 répétitions par taille et chemin ; une file et une fence).
- [x] Tester une texture, un triangle, puis le mélange de couleurs hors écran (48 cas de contrôle GLSL sur Radeon, copies et échantillonnage compris ; [rapport](docs/reports/2026-10-07-offscreen-radeon.md)).
- [x] Comparer les images à une référence et documenter les tolérances numériques (tous les canaux RGBA, tolérance 0 pour les textures et 1/255 pour les couleurs ; erreur maximale constatée 0).
- [x] Produire les équivalents graphiques Metal sur le Mac, conserver AIR/SPIR-V/réflexion et adapter le banc à leur ABI (image 32, sampler 160, DrawParams 0 ; profil direct explicite ; admission testée sans GPU).
- [x] Exécuter ces shaders graphiques issus d'Apple dans le banc Radeon adapté et comparer les 48 cas au contrôle GLSL/référence CPU (shaderInt8 exercé, 330 984 pixels exacts, 96 fichiers RGBA identiques au contrôle, zéro erreur Vulkan ; [rapport](docs/reports/2026-10-07-metal-graphics-radeon.md)).
- [x] Vérifier qu'un GPU absent, un shader non pris en charge ou un résultat incorrect produit un échec explicite (ID PCI absent, mauvaise entrée SPIR-V, résultat GPU volontairement incorrect).

La référence CPU sert uniquement à vérifier le résultat. Elle ne remplace pas l'exécution GPU dans le chemin testé.

**Livrable :** banc de tests Windows, corpus de shaders et résultats numériques ou images de référence.

**Critère de passage :** les tests choisis produisent les résultats attendus sur la RX 9070 XT et peuvent être rejoués. Leur réussite ne valide pas RADV Darwin ni le pilote noyau AMD.

## Étape 3 — Préparer Tahoe et identifier la carte

- [ ] Préparer un disque de test et une configuration OpenCore adaptée au Ryzen et à la carte mère (support USB GPT préparé, récupération et deux EFI copiés/vérifiés, `ocvalidate` 1.0.8 réussi ; [guide](docs/OPENCORE-TAHOE.md). EFI RapidEFI désormais sauvegardée ; répétabilité et secours à qualifier).
- [x] Fixer les versions/builds des supports Tahoe (récupération Apple 26.6.2 / 25G83 ; système installé observé : 26.7.1 / 25G241).
- [x] Obtenir un démarrage de référence sans notre accélération graphique (bureau IONDRVFramebuffer 1920 × 1080 observé ; zéro périphérique Metal).
- [ ] Vérifier clavier, stockage, réseau et possibilité de récupérer les diagnostics.
- [ ] Préparer et essayer le démarrage de secours permettant de désactiver le pilote expérimental (copie vérifiée ; essai différé à la demande de l'utilisateur, non déclaré réussi).
- [x] Développer ou extraire un module minimal limité à l'identification PCI et aux dimensions des plages mémoire, sans initialisation GPU ni changement de mode d'affichage ([Navi48PciProbe 0.1.0](kexts/Navi48PciProbe/), propriétés IORegistry seulement ; deux builds identiques, 1 048 contrôles et 25 mutations, aucun chargement à cette phase de développement).
- [x] Préparer les configurations EFI d'essai OFF/ON et une référence exacte de l'EFI actuelle (copies vérifiées, `ocvalidate`, 44 tests Python ; [rapport](docs/reports/2026-10-09-pci-probe-efi.md)). Aucun boot exécuté pendant cette préparation.
- [x] Préparer le support d'essai PROBE1401 avec le profil OFF (formatage autorisé sans sauvegarde ; 104 fichiers, signature et plist vérifiés après remontage ; OPENCORE intact ; [rapport](docs/reports/2026-10-09-probe1401-deployment.md)).
- [x] Vérifier l'identité et les ressources rapportées par le module minimal : premier boot ON, six champs et cinq ressources conformes au provider du même boot, dont `1002:7550 / 1849:5417`, BAR0 256 Mio, BAR2 2 Mio, BAR5 512 Kio ([rapport ON](docs/reports/2026-10-09-pci-probe-on-boot.md)). Comparaison IORegistry seulement, pas validation d'accès aux BAR/VRAM.
- [ ] Vérifier que le module refuse les périphériques qui ne correspondent pas à la cible autorisée (filtres et rejets validés en logiciel, pas encore dans le noyau).
- [x] Vérifier un premier démarrage avec `navi48-pci-probe=0` : module réellement chargé, UUID conforme, aucun nœud attaché ([rapport OFF](docs/reports/2026-10-09-pci-probe-off-boot.md)). Pas de trace de chaque appel de probe ; répétabilité et stabilité prolongée restent ouvertes.
- [x] Vérifier un premier retour à OPENCORE après ON : boot du 9 octobre à 11:31 UTC sans argument d'essai, module ou nœud d'observation, deux EFI inchangées ([rapport](docs/reports/2026-10-09-native-isolation.md)). Pas un test de restauration d'EFI endommagée.

Le pilote existant doit être audité avant d'être utilisé pour ce premier relevé. Un commentaire « lecture seule » ne suffit pas à prouver l'absence d'écritures matérielles sur tous les chemins.

**Livrable :** configuration de démarrage qualifiée, procédure de récupération et rapport d'identification de la Radeon.

**Critère de passage :** démarrages répétés réussis, identification conforme et récupération essayée sur le PC.

## Étape 4 — Valider le pilote noyau AMD

- [x] Extraire un premier sous-ensemble firmware/IP/PSP/SMU/IMU en bibliothèque non chargeable, sans hooks Apple ni client utilisateur : deux builds identiques, dix firmwares liés vérifiés, 1 582 contrôles du modèle local, 22 mutations détectées ([rapport](docs/reports/2026-10-09-native-isolation.md)). Aucun chemin matériel exécuté.
- [x] Raccorder les accès bornés BAR0/BAR2/MMIO, le binder et les chemins PSP layout/staging/GPCOM ; tester le même code sur RAM et vérifier les octets/trames, sans accès matériel (398 contrôles ASan/UBSan, [rapport](docs/reports/2026-10-09-native-access.md)).
- [x] Coder le contrôleur de ressources IOKit : acquisition BAR0/BAR2/BAR5, références conservées, concordance PCI/descripteur/mapping, intervalle console rapporté, cache RO/UC demandé, revalidation et nettoyage partiel hors verrou ; 2 241 contrôles ASan/UBSan sur doubles IOKit ([rapport](docs/reports/2026-10-09-native-platform.md)). Aucun mapping Radeon exécuté, contexte désactivé.
- [x] Produire le premier `Navi48Native.kext` : service IOKit possédant le contrôleur, personnalité PCI/entrées kmod, code firmware et dix blobs réellement liés ; compilé/signé, non installé/non chargé ([livrable](docs/reports/2026-10-09-native-kext.md)). GPU non initialisé ; ce livrable ne termine pas l'étape 1 matérielle.
- [x] Raccorder l'allocation RAM DMA au service natif hors gate et préparer son injection OpenCore : 0.1.2, 234 contrôles service/DMA et 2 271 contrôleur sur doubles ; EFI réellement déployée sur PROBE1401, ancienne EFI sauvegardée et OPENCORE intact ([rapport](docs/reports/2026-10-09-native-load-preparation.md)).
- [x] Raccorder au service un chemin expérimental complet d'initialisation/compute : 0.2.0, PSP/GMC/SMU/IMU/RLC/CP/MES/GFX, deux shaders publics gfx1201, fences et 64 comparaisons ; PTEs depuis les vraies pages IOVM. Compilé/signé/déployé sur PROBE1401, double opt-in et ressources retenues jusqu'au reboot, sans fabriquer les Claims RO ([rapport](docs/reports/2026-10-09-native-compute.md)).
- [x] Observer le premier chargement noyau de 0.2.0 : boot PROBE1401 du 10 octobre, module/version/UUID et cinq arguments conformes. Service retiré, zéro instance native et deux fermetures PCI ; capture privilégiée lue mais buffer de boot écrasé, cause exacte inconnue ([rapport](docs/reports/2026-10-10-native-compute-boot.md)). Pas une validation d'acquisition DMA/init GPU.
- [x] Construire/tester hors ligne le correctif diagnostic 0.2.1 : valeurs IOResources conservées après retrait dans le modèle, 419 contrôles service et 80 tests Python, zéro warning. Persistance noyau à observer ([rapport](docs/reports/2026-10-10-native-boot-diagnostics.md)).
- [x] Déployer 0.2.1 sur PROBE1401 : remplacement exact 0.2.0 vérifié/sauvegardé, 112 fichiers/signature/ocvalidate conformes, 101 OPENCORE et tous backups conservés, 86 tests outils. Non encore chargé/boot testé ; noyau courant inchangé en 0.2.0 ([rapport](docs/reports/2026-10-10-native-diagnostics-deployment.md)).
- [x] Observer le boot 0.2.1 et son diagnostic IOResources : module/UUID/arguments conformes, refus exact `InvalidMap` BAR0 `0.2.1` réellement lu après retrait, zéro instance, DMA/compute non observés et aucun hardware. Persistance noyau démontrée ([rapport](docs/reports/2026-10-10-native-0.2.1-boot.md)).
- [x] Détailler le refus BAR0 au champ près en 0.2.2 : mêmes gardes, `MapCheck`/valeurs observées ajoutés au diagnostic, 2 731 contrôles contrôleur et 1 041 service, 92 tests outils, zéro warning. Déployé/vérifié sur PROBE1401, ancienne EFI sauvegardée, non encore chargé ([rapport](docs/reports/2026-10-10-native-mapping-deployment.md)).
- [x] Observer le boot 0.2.2 : module/UUID/arguments conformes, champ rejeté `DescriptorMismatch` BAR0 réellement lu, autres propriétés BAR0 conformes, zéro instance, aucun hardware ([rapport](docs/reports/2026-10-10-native-0.2.2-boot.md)).
- [x] Comparer les identités d'objets descripteur en 0.2.3 : relecture provider au moment du check, `RereadMatch`/`DeclaredIsReread` ajoutés au diagnostic, 3 028 contrôles contrôleur et 1 290 service, 86 tests outils, zéro warning. Déployé/vérifié sur PROBE1401, ancienne EFI sauvegardée, non encore chargé ([rapport](docs/reports/2026-10-10-native-descriptor-deployment.md)).
- [x] Observer le boot 0.2.3 : module/UUID/arguments conformes, scénario tranché (mapping partagé, provider stable), zéro instance, aucun hardware ([rapport](docs/reports/2026-10-10-native-0.2.3-boot.md)).
- [x] Coder la voie 1 en 0.2.4 : adoption de l'objet déclaré après revalidation totale, provider stable exigé, `DescriptorOrigin` au diagnostic, 3 202 contrôles contrôleur et 1 311 service, 86 tests outils, zéro warning. Déployé/vérifié sur PROBE1401, ancienne EFI sauvegardée, non encore chargé ([rapport](docs/reports/2026-10-10-native-adoption-deployment.md)).
- [x] Observer le boot 0.2.4 : adoption des 3 BAR (`DescriptorOrigin=1`) + DMA préparée, puis refus logiciel au préflight accélérateur (`PreflightCheck=7`, itérateur null = cas nominal traité en erreur). Aucun hardware ([rapport](docs/reports/2026-10-10-native-0.2.4-boot.md)).
- [x] Corriger le préflight en 0.2.5 : fonction exacte extraite et couverte sur host (null/vide/concurrent/allocation), itérateur null = vide présumé marqué, `ExclusiveAccess` maintenu, 3 207 contrôles contrôleur et 1 311 service, 86 tests outils, zéro warning. Déployé/vérifié sur PROBE1401, ancienne EFI sauvegardée, non encore chargé ([rapport](docs/reports/2026-10-10-native-preflight-deployment.md)).
- [x] Observer le boot 0.2.5 : préflight accélérateur passé (`AcceleratorIteratorNull=1`), puis refus au mapping RW BAR0 du compute (`PreflightCheck=12`, `BadArgument`). Aucun hardware ([rapport](docs/reports/2026-10-10-native-0.2.5-boot.md)).
- [x] Porter le diagnostic au champ près dans le préflight RW en 0.2.6 : `RWBar0Map`/`RWBar2Map`/`RWBar5Map` dans BootDiagnostics et Compute, mêmes gardes attribuées, 3 207 contrôles contrôleur et 1 319 service, 86 tests outils, zéro warning. Déployé/vérifié sur PROBE1401, ancienne EFI sauvegardée, non encore chargé ([rapport](docs/reports/2026-10-10-native-rw-deployment.md)).
- [x] Observer le boot 0.2.6 : socle/DMA/préflight OK, `RWBar0Map/MapCheck=3` (`DescriptorMismatch`), partage confirmé, zéro hardware ([rapport](docs/reports/2026-10-10-native-0.2.6-boot.md)).
- [x] Porter l'adoption voie 1 au chemin RW en 0.2.7 : objet déclaré + revalidation totale, `Origin` au diagnostic, 3 207 contrôles contrôleur et 1 323 service, 86 tests outils, zéro warning. Déployé/vérifié sur PROBE1401, ancienne EFI sauvegardée, non encore chargé ([rapport](docs/reports/2026-10-10-native-rw-adoption-deployment.md)).
- [x] Observer le boot 0.2.7 : adoption RW réussie (`Origin=1`), firmware chargé, stage 16 timeout, service conservé avec rapports ([rapport](docs/reports/2026-10-10-native-0.2.7-boot.md)).
- [x] Diagnostiquer finement en 0.2.8 : `Stage16Step` + `RingTestPassed`/`FetchProven`/`RingTestValue` publiés, 3 207 contrôles contrôleur et 1 323 service, 86 tests outils, zéro warning. Déployé/vérifié depuis le secours, ancienne EFI sauvegardée, non encore chargé ([rapport](docs/reports/2026-10-10-native-stage16-deployment.md)).
- [x] Observer le boot 0.2.8 : `Stage16Step=5` (`RingTest`), `RingTestValue=0xFFFFFFFF` (SCRATCH illisible), service conservé ([rapport](docs/reports/2026-10-10-native-0.2.8-boot.md)).
- [x] Instrumenter en 0.2.9 : bases GC, SCRATCH avant/après, RPTR et halts PFP/ME/MEC en lecture seule, 3 207 contrôles contrôleur et 1 323 service, 87 tests outils, zéro warning. Déployé par copie forcée explicite depuis le boot conservé (fichiers seuls, tout vérifié), ancienne EFI sauvegardée, non encore chargé ([rapport](docs/reports/2026-10-10-native-gc-deployment.md)).
- [x] Observer le boot 0.2.9 : bases GC résolues, SCRATCH=0 avant puis `0xFFFFFFFF` après le test, autres registres lisibles, service conservé ([rapport](docs/reports/2026-10-10-native-0.2.9-boot.md)).
- [ ] Découper le ring-test en trois phases (write/read, kick, poll) avec relevés intermédiaires, sans modifier l'amont. Puis remplacement revu et nouveau boot ; aucun unload/reload/veille dans le noyau courant.
- [ ] Observer l'exécution GPU réelle : aucun résultat Radeon encore validé, étapes matérielles 1/2 non terminées.
- [ ] Faire fonctionner/qualifier ce kext sur la plateforme : propriété GPU exclusive, origine/taille/base MC et réservations VRAM, console, cohérence HDP, DMA applicable, puissance/arrêt/restauration. L'orchestration est désormais appelée dans un essai explicitement autorisé, pas qualifiée par des preuves inventées. Un bail IOKit ou un bundle signé ne qualifie pas l'initialisation GPU.
- [ ] Qualifier les firmwares nécessaires à la carte et enregistrer leurs empreintes.
- [ ] Activer progressivement l'initialisation des blocs nécessaires aux essais mémoire et calcul.
- [ ] Vérifier les allocations, transferts, protections et limites de mémoire.
- [ ] Soumettre une première commande simple et vérifier sa complétion.
- [ ] Exécuter un shader natif qui écrit un résultat connu, puis relire ce résultat.
- [ ] Vérifier les signaux de complétion et les délais d'attente.
- [ ] Tester les erreurs d'allocation, les requêtes invalides et l'arrêt d'un client de test.
- [ ] Vérifier que les ressources sont libérées et qu'une panne produit un diagnostic exploitable.
- [ ] Répéter les essais avant d'ajouter Vulkan ou Metal.

Les commandes doivent utiliser le chemin noyau AMD réel. Les tests ne peuvent pas être considérés comme réussis sur la seule base d'un code de retour ou d'une capacité annoncée.

**Livrable :** rapport de mémoire et de commandes GPU natives, avec résultats relus et vérifiés.

**Critère de passage :** les résultats calculés par la carte sont corrects, les répétitions ne montrent pas de corruption et les scénarios d'erreur retenus échouent proprement.

## Étape 5 — Qualifier RADV sous Tahoe

- [ ] Raccorder RADV au pilote AMD et vérifier la concordance des versions de l'interface N48N.
- [ ] Énumérer le GPU et contrôler les capacités réellement disponibles.
- [ ] Exécuter les shaders SPIR-V du corpus de l'étape 2.
- [ ] Comparer les résultats à la référence CPU et aux résultats Windows, avec les tolérances définies.
- [ ] Valider les buffers, textures, copies, barrières, files et synchronisation.
- [ ] Rendre un triangle et les scènes de référence hors écran.
- [ ] Tester les limites retenues et des créations/destructions répétées de ressources.
- [ ] Vérifier les erreurs de compilation de shaders et les fonctionnalités non prises en charge.

**Livrable :** backend Vulkan AMD sous Tahoe et rapport de comparaison Windows / macOS.

**Critère de passage :** le corpus minimal passe sur la carte réelle sous Tahoe, sans périphérique logiciel ni remplacement CPU. Les divergences et capacités manquantes sont documentées.

## Étape 6 — Adapter la couche Metal du fork

- [ ] Séparer les appels Vulkan génériques des extensions propres à NVK.
- [ ] Raccorder l'allocation, les ressources et les commandes au backend AMD.
- [ ] Remplacer ou désactiver les chemins NVIDIA de compilation et de vidéo.
- [ ] Faire correspondre les capacités Metal annoncées aux fonctions effectivement testées.
- [ ] Adapter la liaison entre le bundle Metal et l'accélérateur macOS.
- [ ] Exposer le périphérique aux outils de test autorisés, sans généraliser prématurément l'accès à toutes les applications.
- [ ] Sélectionner explicitement la Radeon dans l'outil de test Metal.
- [ ] Exécuter le calcul de référence via Metal et vérifier les résultats.
- [ ] Rendre les scènes hors écran via Metal et comparer leurs images.
- [ ] Tester plusieurs commandes, les événements et la libération des ressources.
- [ ] Comparer shaders précompilés et compilation à l'exécution lorsque ces chemins sont disponibles.

Le projet AMD propose déjà mtlprobe, avec des tests de buffers, de calcul et de rendu. Il pourra être adapté en conservant des critères de résultat explicites.

**Livrable :** application Metal de test exécutant un calcul et un rendu corrects sur la RX 9070 XT sous Tahoe.

**Critère de passage :** les résultats sont conformes, l'identité GPU est vérifiée et les fonctionnalités non implémentées ne sont pas annoncées comme prises en charge.

## Étape 7 — Intégrer le bureau et les applications

- [ ] Implémenter et tester le partage IOSurface et les images entre processus.
- [ ] Vérifier la cohérence mémoire CPU / GPU et la présentation des images.
- [ ] Intégrer WindowServer sur l'écran et le connecteur retenus.
- [ ] Tester le curseur, les couleurs, les rafraîchissements et les redimensionnements.
- [ ] Tester plusieurs applications utilisant le GPU simultanément.
- [ ] Identifier précisément les chemins graphiques utilisés par chaque application de qualification.
- [ ] Vérifier des sessions prolongées, les allocations répétées et le redémarrage des applications.
- [ ] Ajouter ensuite les changements de résolution et les scénarios d'écran éteint / rallumé.
- [ ] Qualifier la veille et le réveil séparément ; les laisser explicitement non pris en charge tant que ces essais échouent.

**Livrable :** bureau accéléré et première matrice d'applications testées.

**Critère de passage :** le bureau et les applications choisies restent corrects pendant une durée documentée, avec journaux, contrôle des ressources et absence de corruption constatée. Un compteur d'images élevé ne suffit pas à lui seul.

## Étape 8 — Préparer une version alpha

- [ ] Figer les versions de Tahoe, OpenCore, Mesa, du pilote et des firmwares qualifiés.
- [ ] Produire un paquet versionné avec empreintes et notices.
- [ ] Préparer une installation qui vérifie la cible et expose les changements prévus.
- [ ] Tester la désinstallation et le retour à la configuration de référence.
- [ ] Tester une mise à jour du pilote et le retour à une version précédente.
- [ ] Documenter les écrans, connecteurs et applications effectivement testés.
- [ ] Publier les limites connues, erreurs restantes et fonctions désactivées.
- [ ] Vérifier les conditions de redistribution des composants inclus.

**Livrable :** version alpha reproductible et procédure complète d'installation, diagnostic et récupération.

**Critère de passage :** une installation propre sur la configuration cible, suivie des tests de qualification, de la désinstallation et d'une réinstallation, est documentée.

La qualification de cette configuration ne vaut pas prise en charge générale des RX 9000.

## Travaux ultérieurs

Ces travaux seront classés après les premiers résultats :

- Multi-écran et connecteurs supplémentaires.
- Optimisation des transferts et du partage des images.
- Fonctionnalités Metal avancées, dont ray tracing et mesh shaders.
- Qualification de MPS, Core ML, Core Image et des autres frameworks nécessaires aux usages retenus.
- Accélération vidéo avec les blocs AMD appropriés.
- Autres cartes et autres configurations matérielles.
- Nouveaux builds de Tahoe, chacun avec une nouvelle qualification.

## Rapport minimal pour chaque essai

| Champ | Contenu attendu |
| --- | --- |
| Identifiant | Étape, nom du test et numéro d'essai |
| Code | Révisions de tous les composants concernés |
| Machine | Carte mère, BIOS, GPU, VBIOS, écran et connecteur pertinents |
| Système | Version exacte et numéro de build |
| Artefacts | Versions du pilote et empreintes des binaires et firmwares |
| Options | Paramètres du pilote et configuration nécessaire au test |
| Exécution | Entrées, taille du travail et périphérique GPU sélectionné |
| Vérification | Référence, tolérance et nombre d'écarts |
| État | Réussi, échoué, bloqué ou non exécuté |
| Preuves | Journaux, données relues et images utiles |
| Récupération | Procédure utilisée si l'essai a interrompu la session |

## Prochaine action

La liste opérationnelle des tâches et blocages est dans [Travail restant](docs/NEXT-STEPS.md).

Les premiers boots **OFF puis ON** sur PROBE1401 sont observés : module chargé/non attaché en OFF, attaché à la bonne Radeon avec dictionnaire conforme en ON. [Rapport ON](docs/reports/2026-10-09-pci-probe-on-boot.md). Le premier retour OPENCORE est désormais vérifié, sans argument d'essai ni module chargé. À cette phase, PROBE1401 conservait le profil observateur ON et les deux EFI étaient inchangées.

Le premier `Navi48Native.kext` est créé, avec son service IOKit, le contrôleur et le vrai core firmware lié. La version 0.1.2 raccorde maintenant l'allocation DMA hors gate et est **déployée sur PROBE1401 pour le prochain boot** ; ancienne EFI sauvegardée, OPENCORE intact. **Aucun chargement dans le noyau courant ni GPU initialisé.** Action immédiate : [F12 → PROBE1401 UEFI → Tahoe installé](docs/NATIVE-KEXT-ESSAI.md), puis observer module et ressources. Le raccordement matériel mémoire/propriété/HDP/arrêt et de l'orchestration PSP/SMU reste nécessaire. Pas une nouvelle campagne sur l'observateur. Ensuite : première commande GPU réelle, rendu, Metal et WindowServer. [Livrable et blocages](docs/reports/2026-10-09-native-kext.md). Ne pas installer Navi48Bringup complet ni charger/décharger à chaud.

L'essai du secours est différé, pas marqué réussi. La nouvelle référence conserve les 101 fichiers de l'EFI actuelle, dont les changements OpenCore antérieurs : [rapport de préparation](docs/reports/2026-10-09-pci-probe-efi.md).

Les [constats de l'audit Navi48](docs/reports/2026-10-09-navi48-build-audit.md) demeurent applicables au pilote GPU complet, notamment console, DMA/IOMMU, voies Apple et retour arrière.

Navi48 compile ; préparer ensuite Mesa/RADV Darwin, mais ne qualifier aucun résultat GPU avant les essais natifs mémoire/commandes. Conserver les 36 cas de calcul et 48 cas graphiques Apple AIR/GLSL déjà validés sous Windows comme références pour le futur banc Tahoe. La restauration de secours, le chargement du pilote complet, RADV et Metal sous macOS restent ouverts.
