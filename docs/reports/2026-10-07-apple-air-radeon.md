# Premier calcul issu d'AIR Apple sur Radeon — 7 octobre 2026

**Le shader Metal `vector_add`, compilé sur le Mac en AIR puis traduit en
SPIR-V par le fork, produit les résultats attendus sur la RX 9070 XT sous
Windows.** Les 36 cas passent en mémoire hôte et via staging/VRAM, avec zéro
écart de données, d'entrées, de paramètres ou de gardes, et zéro erreur Vulkan
ou de synchronisation. Le contrôle GLSL rejoué donne les mêmes résultats.

Identifiant : `steps-1-2-apple-air-radeon-vector-add-1`.
Code testé : `b8ad4a69cb9bc7c7c6194e11a9a46f75edb20fb7`, branche
`codex/amd-validation-bootstrap`. L'exécution utilise un **checkout propre**
de ce commit récupéré depuis le Mac, avant les présentes mises à jour de
documentation. Le [rapport JSON](2026-10-07-apple-air-radeon.json) conserve
les cas, identités, versions, choix mémoire et empreintes sans chemins
personnels ni numéros de série.

## Chaîne effectivement vérifiée

1. Compilation du shader du projet par Apple Metal 32023.864 sur le Mac,
   cible `air64-apple-macosx26.0`, SDK macOS 26.2, `-std=metal4.0 -O2`.
2. Lecture du bitcode Apple avec LLVM 20.1.8, traduction par le fork et
   validation SPIR-V Vulkan 1.2 sur le Mac, avec corpus reproduit octet par
   octet. Preuves : [rapport Mac/AIR](2026-10-07-apple-air.md).
3. Transfert Git du corpus, vérification des **huit** entrées SHA-256 du
   manifeste sous Windows, y compris AIR, metallib, SPIR-V, réflexion et
   provenance. La conversion LF/CRLF reste désactivée pour ce corpus.
4. Exécution du SPIR-V transféré sur la RX 9070 XT avec origine `metal-air`,
   sélection PCI explicite, Int64 activé et validation de synchronisation.
5. Vérification de chaque mot par rapport au calcul CPU et comparaison
   avec une nouvelle exécution du contrôle GLSL sur la même carte.

Aucun correctif du traducteur ou du banc Vulkan n'a été nécessaire pour ce
shader. Le test Rust `apple_vector_add` est également passé sous Windows :
ses trois traductions du désassemblage AIR produisent exactement le SPIR-V
et la réflexion livrés par le Mac. Ce test logiciel utilise le désassemblage
publié ; il n'exécute pas un compilateur Metal sous Windows.

## Matériel et options

| Élément | Valeur observée |
| --- | --- |
| CPU / RAM | Ryzen 5 5600X, 16 Gio |
| Carte mère / BIOS | Gigabyte B550M DS3H, BIOS FD |
| Système | Windows 11 Professionnel x64, build 26300 |
| GPU / PCI | AMD Radeon RX 9070 XT, `1002:7550` |
| Sous-système | `1849:5417` |
| Pilote Windows | `32.0.31041.1004` |
| Pilote Vulkan / API | AMD proprietary driver, `26.8.1 (LLPC)` / `1.4.349` |
| File | Famille 0, compute |
| Shader | Entrée `main`, local 64×1×1, quatre storage buffers set 0 / bindings 0..3 |
| Capacité demandée | `shaderInt64`, disponible et activée |
| Chemins mémoire | `host-coherent` et `device-local-staging` |
| VRAM de calcul | Type 0, heap 1, flags `DEVICE_LOCAL` seuls, non mappée par le CPU |
| Hôte / staging | Type 1, heap 0, `HOST_VISIBLE | HOST_COHERENT` |
| Vérification | Addition modulo 2^32, tolérance zéro, allocations entières |
| Synchronisation | Barrières host/transfer/compute/readback, fence bornée à 5 s |

Les tailles sont 1, 63, 64, 65, 257 et 4097 éléments, trois fois chacune
avec des entrées différentes, sur les deux chemins mémoire : **36 cas par
shader**. Les 16 mots de garde de chaque côté, les fins inutilisées, les
entrées et les paramètres sont vérifiés. Les checksums FNV-1a conservés dans
le JSON servent au diagnostic ; la comparaison CPU porte sur chaque mot.

## Résultats

| Essai | Résultat |
| --- | --- |
| Récupération du commit Mac `b8ad4a6` | Avance rapide réussie |
| Manifeste du corpus Apple | 8 / 8 empreintes conformes |
| Compilation Rust Windows `--locked`, cache offline | Réussie |
| Tests Rust ciblés | 11 réussis, 4 balayages du corpus absent filtrés |
| Régression `apple_vector_add` | Réussie, trois traductions identiques à la référence Mac |
| Compilation du banc et validation SPIR-V | Réussies |
| CTest | 4 réussis |
| Apple AIR traduit, chemin hôte | 18 cas réussis |
| Apple AIR traduit, chemin staging/VRAM | 18 cas réussis |
| Contrôle GLSL rejoué | 36 cas réussis |
| Comparaison Apple / CPU / GLSL | Zéro écart dans les 36 cas |
| Erreurs Vulkan / synchronisation | Zéro pour les deux exécutions |

Outils Windows : Rust/Cargo 1.98.1, MSVC 19.51, SDK Windows 10.0.26100.0,
CMake 4.3.1-msvc1, Vulkan SDK 1.4.350.0, glslang 16.2.0 et SPIRV-Tools v2026.2.

Artefacts du Mac effectivement utilisés :

- AIR : `1369b355edc5d5dc2d214a9b2dead69c23061bf4862d6dad9a2adf0949f5eb75`.
- SPIR-V : `f7cb6896c6d9d174a1c87b27a682adffa4649c0d718c1be9895215a5cd0cf797`.
- Réflexion : `dcfebcd5cbaa5ee504856fdc8aa8b7674dc0a33a34a1685a7e37919cd271032e`.

Les empreintes des huit fichiers, de l'exécutable du banc et des sources
testées sont dans le [JSON partageable](2026-10-07-apple-air-radeon.json).
Les empreintes de fichiers texte hors corpus sont celles des octets du
checkout Windows ; la conversion CRLF peut les distinguer des fichiers Mac.

## Rejouer et consulter les preuves

Depuis la racine du dépôt, dans l'environnement Windows préparé :

```powershell
. ./tools/initialize-windows-dev.ps1
./tools/build-translator.ps1
./tools/run-windows-probe.ps1 -DeviceId 0x7550 `
  -Shader tests/shaders/apple/vector_add.spv `
  -Reflection tests/shaders/apple/vector_add.reflection.json `
  -ShaderOrigin metal-air
```

La [procédure Mac/transfert](../VALIDATION-MACOS.md) donne la vérification
du manifeste et la régénération du corpus.

Preuves locales ignorées par Git :

- `reports/local/2026-10-07-apple-air-radeon/` : `result.json`, `provenance.json`,
  `probe.log`, `run.log`, `translator-tests.log`, `corpus-hashes.log`,
  `comparison.txt` et `windows-target.json`.
- `reports/local/2026-10-07-apple-air-radeon-control/` : résultats, provenance
  et journal du contrôle GLSL rejoué.

## Limites et prochaine action

Cette preuve concerne **un shader de calcul entier issu de Metal**, exécuté
via Vulkan sur le pilote AMD Windows. La metallib n'a pas été exécutée via
l'API Metal. Le rendu hors écran, les textures, le blending, RADV Darwin,
le kext AMD, la couche Metal du pilote et le démarrage Tahoe restent ouverts.
Les suites Rust non sélectionnées et les fixtures amont absentes ne sont
pas couvertes par les 11 tests ciblés.

La prochaine extension du banc est le corpus texture/triangle/blending
hors écran, avec vérification des images et shaders Metal correspondants.
Le calcul Apple AIR devient le contrôle de régression à rejouer après les
changements pertinents du traducteur, du banc ou du pilote Windows.
