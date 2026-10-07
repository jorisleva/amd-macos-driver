# Roadmap — RX 9070 XT sur macOS Tahoe

Cette feuille de route décrit le développement pour le PC **Ryzen 5 5600X + RX 9070 XT**, avec Windows comme système actuel et un autre Mac disponible pour compiler.

L'architecture, le périmètre et les références sont détaillés dans [README.md](README.md).

**État au 7 octobre 2026 : étapes 0 et 1 commencées, banc Windows de l'étape 2 compilé.** Le traducteur et 10 tests logiciels ciblés passent sous Windows ; 4 tests du banc et le refus d'une cible absente sont vérifiés. Aucune étape complète ni exécution sur Radeon n'est validée. Preuves et limites : [premier rapport](docs/reports/2026-10-07-bootstrap.md).

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

- [ ] Relever le modèle de carte mère, la version du BIOS, la RAM et les périphériques nécessaires au démarrage.
- [ ] Relever le fabricant de la Radeon, son VBIOS, ses identifiants PCI et de sous-système.
- [ ] Choisir l'écran et le connecteur utilisés pour la première qualification.
- [ ] Relever le modèle du Mac, son système, Xcode et les SDK disponibles.
- [x] Créer une branche de développement dédiée dans le fork (`codex/amd-validation-bootstrap`).
- [ ] Fixer les révisions du fork, du projet AMD, de Mesa, de MacKernelSDK et des firmwares.
- [x] Inventorier les fonctions Metal génériques et les dépendances directes à NVK, NVRM, NVIDIAShared et NVENC ([audit initial](docs/AMD-INTEGRATION.md), wrapper à approfondir).
- [x] Définir les contrats du backend AMD : buffers, images, mémoire partagée, commandes, synchronisation et présentation ([contrat proposé](docs/AMD-INTEGRATION.md), implémentation et qualification à faire).
- [x] Inventorier les licences et notices à conserver ([inventaire initial](docs/AMD-INTEGRATION.md), MacKernelSDK et redistribution à vérifier).

**Livrable :** inventaire matériel et logiciel, architecture d'intégration et liste des dépendances figées.

**Critère de passage :** chaque composant à conserver ou à remplacer est identifié ; les informations matérielles manquantes sont explicitement suivies.

## Étape 1 — Rendre la compilation reproductible

- [x] Compiler le traducteur Rust et lancer ses tests logiciels pertinents (CLI Windows, 10 tests ciblés réussis ; suite complète bloquée par corpus absent ; Mac à vérifier).
- [ ] Installer et qualifier les outils de compilation Metal et de validation SPIR-V.
- [ ] Compiler le pilote AMD pour x86_64 avec MacKernelSDK.
- [ ] Construire Mesa à la révision attendue et appliquer les adaptations RADV Darwin dans l'ordre prévu.
- [ ] Construire les outils de test et les composants Metal pour x86_64.
- [ ] Remplacer les chemins propres à la machine de l'auteur par des paramètres documentés.
- [ ] Traiter les fichiers et caches absents du dépôt public : générer ceux nécessaires à nos propres tests et identifier ceux qui restent nécessaires à l'intégration du bureau.
- [ ] Vérifier l'architecture des binaires, les bibliothèques requises, les symboles et la signature des bundles.
- [ ] Refaire la compilation depuis une copie propre, sans dépendre de fichiers non déclarés.
- [ ] Préparer des paquets versionnés avec leurs empreintes et leur provenance.

Les éléments produits ici sont des artefacts de test. Ils ne sont pas encore qualifiés pour être installés sur le PC.

**Livrable :** procédure de compilation, versions d'outils fixées, artefacts identifiés et rapport de tests logiciels.

**Critère de passage :** la compilation depuis une copie propre est reproductible ; chaque erreur, test ignoré ou dépendance manquante est expliqué. Aucun résultat matériel n'est déduit de cette étape.

## Étape 2 — Vérifier les shaders AMD sous Windows

- [x] Développer un petit programme Vulkan Windows sélectionnant explicitement la RX 9070 XT (compilé, refus d'autres GPU vérifié ; exécution Radeon à faire).
- [ ] Enregistrer l'identité du GPU, le pilote et les capacités utilisées.
- [ ] Compiler sur le Mac des shaders Metal écrits pour le projet et conserver leurs sources et leur AIR.
- [ ] Traduire ces shaders avec le traducteur du fork et valider le SPIR-V.
- [ ] Exécuter une addition de tableaux et comparer tous les résultats à une référence CPU.
- [ ] Tester des tailles non multiples des groupes de travail et vérifier les zones de garde des buffers.
- [ ] Tester les transferts, barrières, lectures après écriture et commandes répétées.
- [ ] Tester une texture, un triangle, puis le mélange de couleurs hors écran.
- [ ] Comparer les images à une référence et documenter les tolérances numériques.
- [ ] Vérifier qu'un GPU absent, un shader non pris en charge ou un résultat incorrect produit un échec explicite.

La référence CPU sert uniquement à vérifier le résultat. Elle ne remplace pas l'exécution GPU dans le chemin testé.

**Livrable :** banc de tests Windows, corpus de shaders et résultats numériques ou images de référence.

**Critère de passage :** les tests choisis produisent les résultats attendus sur la RX 9070 XT et peuvent être rejoués. Leur réussite ne valide pas RADV Darwin ni le pilote noyau AMD.

## Étape 3 — Préparer Tahoe et identifier la carte

- [ ] Préparer un disque de test et une configuration OpenCore adaptée au Ryzen et à la carte mère.
- [ ] Fixer la version exacte de Tahoe et son numéro de build.
- [ ] Obtenir un démarrage de référence sans notre accélération graphique.
- [ ] Vérifier clavier, stockage, réseau et possibilité de récupérer les diagnostics.
- [ ] Préparer et essayer le démarrage de secours permettant de désactiver le pilote expérimental.
- [ ] Développer ou extraire un module minimal limité à l'identification PCI et aux dimensions des plages mémoire, sans initialisation GPU ni changement de mode d'affichage.
- [ ] Vérifier l'identité de la carte et les plages mémoire par rapport au relevé matériel.
- [ ] Vérifier que le module refuse les périphériques qui ne correspondent pas à la cible autorisée.
- [ ] Vérifier le comportement lorsque le module est désactivé.

Le pilote existant doit être audité avant d'être utilisé pour ce premier relevé. Un commentaire « lecture seule » ne suffit pas à prouver l'absence d'écritures matérielles sur tous les chemins.

**Livrable :** configuration de démarrage qualifiée, procédure de récupération et rapport d'identification de la Radeon.

**Critère de passage :** démarrages répétés réussis, identification conforme et récupération essayée sur le PC.

## Étape 4 — Valider le pilote noyau AMD

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

Compléter l'inventaire sur le PC cible et le Mac, puis exécuter le [banc Windows](docs/VALIDATION-WINDOWS.md) déjà compilé sur la Radeon. Produire ensuite le premier AIR sur le Mac et comparer son calcul traduit. Poursuivre le verrouillage MacKernelSDK/firmwares et la compilation AMD des **étapes 0 et 1**. La préparation du démarrage Tahoe et de son moyen de récupération reste ouverte.

Le premier succès à rechercher est un shader Metal écrit pour le projet, traduit en SPIR-V et exécuté avec un résultat correct sur la RX 9070 XT sous Windows. Il permettra de qualifier une partie réutilisable du fork avant les essais noyau sous macOS.
