# Premier rapport de développement — 7 octobre 2026

Code de départ : `aefcbc077755148d5763433a72d84d91d7183102`.
Branche créée : `codex/amd-validation-bootstrap`. Ce rapport couvre les essais
réalisés avant le commit du livrable ; les rapports locaux enregistrent l'état
du travail et les empreintes des fichiers concernés. Les sorties de compilation
et inventaires locaux sont ignorés par Git.

## Résultats observés

| Essai | Résultat | Portée |
| --- | --- | --- |
| Inventaire Windows en lecture seule | Réussi, aucune erreur CIM | Machine de développement, pas PC Ryzen/Radeon |
| Compilation CLI Rust avec Cargo.lock corrigé | Réussie | Windows x64 ; pas de binaire macOS |
| Tests d'intégration Rust ciblés | 10 réussis, 4 filtrés pour corpus absent | Déterminisme, réflexion des ressources/accès, registre des options |
| Suite unitaire Rust `--lib` | Bloquée à la compilation | 3 fixtures absentes ; aucun succès global annoncé |
| Construction CMake du banc | Réussie | MSVC x64 et compilation/validation du contrôle GLSL |
| Tests CTest | 4 réussis | Corruption injectée, sélection matérielle simulée, ABI shader et arguments |
| Traduction de `vector_add.synthetic.ll` | Réussie, SPIR-V Vulkan 1.2 validé | IR écrit pour le projet, pas AIR Apple |
| Essai avec la cible demandée absente | Échec explicite attendu | GPU sélectionné `null`, zéro dispatch, zéro cas exécuté |
| Reconstruction du banc depuis copie des sources, sortie vide | Réussie, 4 CTest réussis | Reproductibilité de compilation locale Windows ; aucun résultat GPU |
| Reconstruction Rust depuis copie des sources, sortie vide, mode offline | Réussie, 10 tests ciblés réussis et fixture traduite | Dépendances du lockfile déjà en cache ; aucune fixture externe ajoutée |
| Shaders incorrects injectés (local size 32, fichier tronqué) | Refus explicites | Admission avant énumération GPU |
| Compilation et exécution Metal sur Mac | Non exécutées | Modèle, système, Xcode et SDK du Mac à relever |
| Exécution calcul sur RX 9070 XT | Non exécutée | Carte absente de la machine actuelle |
| Kext AMD / RADV Darwin / démarrage Tahoe | Non exécutés | Étapes matérielles encore ouvertes |

La machine actuelle est un Dell XPS 15 9520, Core i7-12700H, environ 16 Gio de
RAM, Windows 11 build 26300, BIOS 1.42.0. Vulkan énumère Intel Iris Xe
(`8086:46a6`) et RTX 3050 Ti Laptop (`10de:25a0`). Aucun de ces résultats
n'est attribué à la configuration Ryzen 5600X + RX 9070 XT.

Le PCI ID `0x7550` utilisé dans l'essai négatif est une entrée de test, **pas un
relevé de notre Radeon**. Il ne remplit pas l'inventaire cible.

Versions de l'environnement : Rust/Cargo 1.96.0, CMake 4.3.3, MSVC 19.29,
SDK Windows 10.0.19041.0, Vulkan SDK 1.4.350.0, glslang 16.2.0,
SPIRV-Tools v2026.2.

## Défauts de la base publique et points résolus

Le lockfile Rust contenait des dépendances d'un workspace de validation non
livré et ne correspondait plus au manifeste : `cargo --locked` refusait la
construction. Il a été régénéré par Cargo pour le manifeste existant ; les
commandes suivantes utilisent `--locked`. Le wrapper Rust n'a pas été construit
et son propre lockfile n'a pas été qualifié.

La suite unitaire attend `kernel_nullable_branch_cursor.ll`,
`kernel_nested_loop_constant_weight_walk.ll` et `vertex_narrow_attributes.ll`
dans `validation/fixtures/public/`, absent du dépôt. Quatre balayages du même
corpus dans les suites choisies sont également indisponibles. Les scripts les
filtrent explicitement ; ils n'inventent pas les fixtures et ne modifient pas
les assertions amont.

La fixture synthétique confirme : réflexion 56, entrée SPIR-V `main` distincte
du nom AIR, buffers 0..3, local 64x1x1 et capacité Int64 requise. Le banc contrôle
ce contrat et active `shaderInt64` seulement quand le shader l'exige.

## Preuves locales et suite

- Inventaire : `reports/local/windows-development.json`.
- Rapport de refus : `reports/local/2026-10-07-absent-target/{result,provenance}.json`.
- Refus après admission du shader synthétique : `reports/local/2026-10-07-synthetic-absent/{result,provenance}.json`.
- Journaux : `out/probe-bootstrap.log`, `out/probe-synthetic.log` et sorties CTest.
- Reconstruction Rust : `out/clean-translator.log` ; sources copiées dans `out/clean-source/`, sorties vides `out/clean-translator/` et `out/clean-vulkan/`.
- Cas traduit : `translator/translator/target/fixture/` lors de la première exécution ; `out/translator/fixture/` avec les paramètres par défaut.

Les deux reconstructions produisent des shaders identiques octet par octet :
SHA-256 du contrôle GLSL
`fdc2c078dc7784a8b8ed40acaaaeaf67e6499d444f043cd56eae795878f44594`,
SHA-256 de la fixture traduite
`0f7fdc5dc1a9b69701145b43d75bc1a4a9efb24f2daeaab0c21bab1047aa74bc`.
Cette comparaison ne prétend pas rendre les binaires de debug identiques.

Prochaine preuve matérielle : relever les IDs sur le PC Ryzen/Radeon et
exécuter le contrôle GLSL avec `tools/run-windows-probe.ps1`. Puis produire
l'AIR sur le Mac et comparer le même calcul via le traducteur. L'écran,
connecteur, VBIOS, Mac, révision MacKernelSDK et provenance firmware restent
suivis comme manquants. Les dépendances partielles et le contrat proposé sont
dans `dependencies/sources.lock.json` et `docs/AMD-INTEGRATION.md`.
