# Premiers calculs sur la RX 9070 XT — 7 octobre 2026

Le contrôle GLSL et la fixture LLVM/AIR synthétique traduite par le fork passent
chacun **36 cas sur la RX 9070 XT**, avec **zéro écart de données, de gardes ou
d'entrées**, et **zéro erreur Vulkan ou de synchronisation**. Les sorties des
deux shaders sont identiques dans chaque cas.

Code de départ : `f745813cd50c0a3e58e81911b7ddb7a6ea7f8333`, branche
`codex/amd-validation-bootstrap`. Les modifications testées ne sont pas encore
commitées ; les SHA-256 des sources et artefacts les identifient dans le
[rapport JSON partageable](2026-10-07-radeon-windows.json). Ce rapport omet les
chemins personnels, numéros de série et identifiants d'instance Windows.

## Configuration effectivement observée

| Élément | Relevé |
| --- | --- |
| CPU | Ryzen 5 5600X, 6 cœurs / 12 threads |
| Carte mère | Gigabyte B550M DS3H ; révision rapportée `x.x`, à préciser |
| BIOS | American Megatrends FD, date 22 mars 2024 |
| RAM | 2 × 8 Gio, fréquence configurée 3200 MT/s |
| Windows | Windows 11 Professionnel, build 26300, x64 |
| GPU | AMD Radeon RX 9070 XT, PCI `1002:7550` |
| Sous-système | Vendor `1849`, device `5417` |
| Pilote Windows | `32.0.31041.1004` |
| Pilote Vulkan | AMD proprietary driver, `26.8.1 (LLPC)` |
| API Vulkan du GPU | `1.4.349` |
| File utilisée | Famille 0, compute |

Le fabricant exact de la carte, le VBIOS, la révision matérielle de la carte
mère, les périphériques de démarrage et le choix d'écran/connecteur restent
à compléter. Les identifiants de sous-système sont conservés comme relevé,
sans en déduire un modèle commercial de carte.

## Implémentation ajoutée

Le banc conserve le chemin `host-coherent` et ajoute `device-local-staging`.
Ce dernier copie les quatre buffers vers le GPU, exécute le shader puis
recopie les allocations entières vers le CPU. Entrées, paramètres, résultat,
gardes et fin inutilisée sont vérifiés. Les barrières couvrent les transferts,
les écritures compute et la réutilisation des buffers de staging.

Sur cette carte, les allocations de calcul du nouveau chemin utilisent le
type mémoire 0, heap 1, flags `DEVICE_LOCAL` seuls : la VRAM n'est pas mappée
par le CPU. Le staging et le chemin hôte utilisent le type 1, heap 0, flags
`HOST_VISIBLE | HOST_COHERENT`. Ces choix sont enregistrés par allocation,
avec les dimensions de heaps effectivement exposées par le pilote.

Le wrapper active désormais la validation de synchronisation Khronos, conserve
`probe.log` et les versions MSVC/SDK Windows/glslang dans la provenance.
Les rapports du banc passent au schéma 2.

Deux scripts rendent la préparation de cette machine reproductible :

- `tools/prepare-windows-vulkan-sdk.ps1` télécharge la version officielle
  1.4.350.0, vérifie son SHA-256 et copie les outils dans `out/tools` avec
  `copy_only=1`.
- `tools/initialize-windows-dev.ps1`, dot-sourcé, découvre les outils x64
  Visual Studio/CMake/Ninja et expose le SDK et sa couche de validation
  dans le processus courant.

La procédure copy-only est documentée par
[LunarG](https://vulkan.lunarg.com/doc/view/1.4.350.0/windows/getting_started.html).
Empreinte de l'extracteur vérifiée contre le service officiel :
`855b27ba05d2d8119c5114c5d4ff870ca38f2c632b11e1bb9923b9b7e6ecfe7b`.

## Résultats

| Essai | Résultat | Portée |
| --- | --- | --- |
| Inventaire cible | Réussi, aucune erreur CIM | PC Ryzen/Radeon observé |
| Compilation CLI Rust | Réussie avec `--locked` | Rust/Cargo 1.98.1, Windows x64 |
| Tests Rust ciblés | 10 réussis, 4 balayages du corpus filtrés | Corpus public toujours absent |
| Construction CMake/Ninja et SPIR-V | Réussie | MSVC 19.51, SDK Windows 10.0.26100.0 |
| CTest | 4 réussis | Vérificateur, sélection simulée, admission du shader, arguments |
| Reconstruction du banc dans un dossier de sortie neuf | Réussie, 4 CTest réussis | Sortie `out/vulkan-fresh`, mêmes sources finales |
| Contrôle GLSL initial | 18 cas réussis | Banc original, mémoire hôte |
| Contrôle GLSL avec staging | 36 cas réussis | 18 hôte + 18 VRAM/staging |
| Fixture synthétique traduite | 36 cas réussis | 18 hôte + 18 VRAM/staging ; Int64 activé |
| Comparaison GLSL / fixture traduite | Identique pour les 36 cas | Vérification complète et checksums de diagnostic |
| ID PCI `0xffff` absent | Refus attendu, sélection `null`, aucun dispatch | Aucune substitution matérielle |
| Shader valide produisant volontairement une mauvaise somme | Échec attendu au premier cas, 1 écart de données | Échec de résultat sur la Radeon réelle |
| AIR Apple / compilation Mac / RADV Darwin / kext AMD | Non exécutés | Qualification toujours ouverte |

Chaque shader teste 1, 63, 64, 65, 257 et 4097 éléments, trois fois avec des
entrées différentes, sur les deux chemins mémoire. Le groupe est 64×1×1,
la tolérance entière est zéro, les gardes ont 16 mots de chaque côté et
la fence est bornée à 5 secondes. La comparaison examine les allocations
entières ; les checksums FNV-1a servent seulement au diagnostic.

Les SHA-256 des shaders restent identiques à ceux du premier rapport :

- Contrôle GLSL : `fdc2c078dc7784a8b8ed40acaaaeaf67e6499d444f043cd56eae795878f44594`.
- Fixture traduite : `0f7fdc5dc1a9b69701145b43d75bc1a4a9efb24f2daeaab0c21bab1047aa74bc`.

Outils complémentaires : CMake 4.3.1-msvc1, Vulkan SDK 1.4.350.0,
glslang 16.2.0 et SPIRV-Tools v2026.2.

## Preuves locales et limites

Les rapports complets sont ignorés par Git :

- `reports/local/windows-target.json` : inventaire cible avec outils.
- `reports/local/2026-10-07-radeon-baseline/` : essai du banc original.
- `reports/local/2026-10-07-radeon-final-control/` : contrôle GLSL final,
  `result.json`, `provenance.json`, `probe.log`.
- `reports/local/2026-10-07-radeon-final-synthetic/` : fixture traduite finale,
  mêmes fichiers.
- `reports/local/2026-10-07-radeon-negative/` : ID absent et mauvais résultat.
- `out/translator-build.log` : compilation, tests Rust et traduction.
- `out/probe-final-{control,synthetic}.log` : construction et essais finaux.
- `out/probe-fresh-build.log` : reconstruction et tests dans une sortie neuve.

Le [JSON partageable](2026-10-07-radeon-windows.json) conserve les cas, choix
mémoire, identités, versions et empreintes sans les chemins personnels.
Les rapports locaux originaux restent la preuve détaillée de chaque exécution.

La fixture est un IR écrit pour le projet, **pas un AIR produit par Apple**.
Ces résultats qualifient ce calcul traduit sur le pilote AMD Windows. Ils
ne valident pas la compilation Metal, les textures, le rendu, les dispatchs
dépendants, les files multiples, la mémoire non cohérente, RADV Darwin ou
le pilote macOS. Le timeout et les erreurs matérielles restent non essayés.

Prochaine qualification : produire l'AIR de `tests/shaders/vector_add.metal`
sur le Mac, transférer SPIR-V/réflexion/provenance et rejouer le même banc.
Le corpus texture, triangle et blending hors écran peut être développé sur
Windows pendant la préparation de cet essai.
