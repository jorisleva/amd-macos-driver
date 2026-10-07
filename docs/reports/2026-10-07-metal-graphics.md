# Shaders graphiques Metal/AIR produits sur Mac — 7 octobre 2026

**Les quatre équivalents Metal du corpus hors écran compilent en AIR Apple et
metallib, puis sont traduits en SPIR-V Vulkan 1.2 valide.** La reconstruction
reproduit les 28 artefacts source/AIR/LLVM/metallib/SPIR-V/ASM/réflexion octet
par octet. 15 tests Rust ciblés, 13 CTest et 8 tests Python passent sur ce Mac.

Identifiant : `step-2-mac-metal-graphics-1`. Branche récupérée par fast-forward :
`codex/amd-validation-bootstrap`, base `d88a37dd716afee945a2ff8169412b1f2dd89f6b`.
Les changements testés sont non commités ; leurs empreintes figurent dans le
[rapport JSON](2026-10-07-metal-graphics.json). Les
[48 cas GLSL validés sous Windows](2026-10-07-offscreen-radeon.md) sont une
preuve antérieure distincte. **Aucune image de ces nouveaux shaders n'est
rendue sur GPU ici**, ni avec Metal ni avec Vulkan.

## Machine, outils et options

Même MacBookAir7,2 x86_64, Core i5-5350U et 8 Gio que le
[premier rapport AIR](2026-10-07-apple-air.md), macOS 15.7.8 / 24G824.
Xcode 26.3 / 17C529, SDK macOS 26.2, Apple Metal/AIR-LLD 32023.864,
LLVM 20.1.8, Rust/Cargo 1.98.1, SPIRV-Tools v2026.2 et CMake 4.4.3.

La compilation lit les copies sources par stdin : `-x metal -O2 -target
 air64-apple-macosx26.0 -std=metal4.0 -c -`. Le bitcode Apple n'est pas modifié.
Seul le commentaire ModuleID de llvm-dis est retiré du désassemblage textuel.
AIR64 est de l'IR GPU, pas du code hôte x86_64. Les metallibs ciblent Tahoe ;
leur présence ne qualifie pas leur exécution sur le Mac hôte Sequoia.

Pour construire aussi le banc C++ et ses tests GPU-free sur Mac, Vulkan-Headers,
Vulkan-Loader et glslang ont été obtenus depuis le tag
`vulkan-sdk-1.4.350.0`. Le glslang construit annonce **16.3.0**, contrairement
au 16.2.0 du SDK Windows observé précédemment ; les versions/révisions restent
séparées dans le JSON. Aucun ICD GPU, pilote AMD ni couche Metal du fork
n'est installé. Les suites internes de ces dépendances ne sont pas exécutées.
Le build C++ produit 56 avertissements `-Wmissing-field-initializers` pour
l'initialisation agrégée des structures Vulkan ; les champs restants sont
initialisés à zéro, aucune erreur de compilation n'est constatée.

## Shaders et ABI

Sources : [`tests/shaders/graphics/`](../../tests/shaders/graphics/).
Corpus Apple : [`tests/shaders/apple/graphics/`](../../tests/shaders/apple/graphics/).

| Shader | Entrée AIR / SPIR-V | Ressources |
| --- | --- | --- |
| `fullscreen.vert` | fullscreen_vertex / main | VertexIndex, Position ; aucun descriptor |
| `texture.frag` | texture_fragment / main | FragCoord ; image 2D float en set 0 / binding 32, sampler 160 ; sortie RGBA 0 |
| `triangle.vert` | triangle_vertex / main | VertexIndex, Position ; DrawParams au binding 0 |
| `solid.frag` | solid_fragment / main | Même DrawParams ; sortie RGBA 0 |

Le shader texture préserve R/G/B/**A**, emploie les centres des texels,
coordonnées normalisées et LOD 0 explicite. Le pipeline impose nearest et
clamp-to-edge sur une image RGBA8_UNORM à un mip, sans blending. Ce domaine
est celui du contrôle GLSL, qui utilise un LOD implicite sur le même mip unique.
Les sommets reprennent exactement les constantes binaires du corpus validé,
y compris le décalage vertical 1/64 ; aucune inversion de Y n'est ajoutée.
Le blending reste une opération fixe du pipeline, pas un calcul du fragment.

`DrawParams` contient float4 color à 0, uint shape à 16 et trois uint de padding
à 20/24/28 : taille 32, layout partagé par vertex et fragment. Le banc Metal
utilise un storage buffer dynamique readonly, avec deux régions alignées selon
`minStorageBufferOffsetAlignment`. Les deux paramètres de dessin sont écrits
avant la soumission ; aucun descriptor ni buffer n'est réécrit entre les deux
draws. Le buffer complet, padding et gardes compris, est vérifié après la fence.

## Deux particularités découvertes

1. **Le bindless NVIDIA par défaut n'est pas notre ABI.** Pour la texture,
   la réflexion annonce `(set,binding)` 0:32, 0:160, 0:640, alors que le module
   émet 0:160, 0:640 et 2:0. Une simple validation SPIR-V ne détecte pas cette
   incohérence de contrat. Le corpus sélectionne explicitement
   `NVMTL_NO_BINDLESS_ALL=1`, flag déjà présent dans le fork, et vérifie l'accord
   des descriptors émis avec la réflexion. Le résultat expose seulement 0:32
   et 0:160, sans heap ni BufferAddressTable. Le profil initial est refusé par
   les gates Python et C++ avant tout accès GPU. Ce choix n'est **pas** une
   correction générale de la réflexion bindless du traducteur.
2. **Int8 est requis pour le fragment texture.** L'intrinsèque Apple sample
   retourne un agrégat `{float4, i8}` avec un octet de résidence. Cet octet
   n'est pas utilisé par le Metal, mais reste représenté dans le SPIR-V émis.
   La capacité Int8 est donc conservée, avec ImageQuery et Shader. Le banc
   vérifie `VkPhysicalDeviceVulkan12Features::shaderInt8`, l'active lorsqu'elle
   est requise et refuse sinon. Sa disponibilité sur la Radeon et les pixels
   restent à vérifier ; aucune capacité GPU n'est déduite des tests Mac.

Aucun correctif du moteur Rust de traduction n'est ajouté pour ces shaders.

## Résultats mesurés

| Essai | Résultat | Limite |
| --- | --- | --- |
| Quatre sources → AIR → metallib | Réussi | Compilation Apple, pas d'exécution Metal |
| Quatre bitcodes → SPIR-V + réflexion | Réussi | CLI lit bien les AIR via llvm-dis |
| spirv-val Vulkan 1.2 + cross-check réflexion/descriptors | Réussi | Nécessaire, pas preuve de pixels |
| Tests Rust ciblés | 15 réussis, 4 balayages amont filtrés | Quatre nouvelles régressions, trois traductions chacune |
| Tests CTest complets sur Mac | 13 réussis | Admission, oracle et arguments ; zéro dispatch GPU |
| CTest sans SDK (`PROBE_SOFTWARE_ONLY=ON`) | 4 réussis | Contrat graphique et oracle CPU indépendants du loader |
| Tests Python | 8 réussis | Hashes, stage, layout, descriptors réels, bindless et fichier tronqué |
| Reconstruction Rust/AIR depuis copie des sources | Réussie, 15 tests | Sorties neuves, Cargo offline avec crates déjà en cache |
| Comparaison des artefacts | 28 fichiers identiques | Exécutables Rust de debug/provenances datées non annoncés identiques |
| Admission du nouveau profil par amd_gpu_probe | Réussie | selected_device null, aucune allocation ni image GPU |
| Admission du profil bindless original | Refus attendu | Code 1 C++, incohérence/ABI détectée en Python |
| Ancien corpus vector_add | Huit SHA-256 conformes | Le calcul de référence conservé n'est pas remplacé |
| Rendu Radeon / Metal sur Mac / PowerShell modifié | Non exécutés | 48 cas futurs, zéro cas exécuté ici |

La suite Rust complète reste bloquée par les trois fixtures amont absentes,
comme documenté dans le rapport précédent. Le nouveau profil ne ferme pas
ce blocage, ni RADV Darwin, le kext AMD ou le bureau Tahoe.

## Rejouer et conserver les preuves

```bash
# Avec les overrides LLVM/SPIR-V et Rust de VALIDATION-MACOS.md.
bash tools/build-translator.sh
python3 tools/compile-metal-graphics.py out/metal-graphics-new
python3 tools/compile-metal-graphics.py --check tests/shaders/apple/graphics
python3 -B -m unittest discover -s tests/tools -v
cmake -S tests/vulkan -B out/graphics-software-new -DPROBE_SOFTWARE_ONLY=ON
cmake --build out/graphics-software-new
ctest --test-dir out/graphics-software-new --output-on-failure
```

Le manifeste portable préserve sources, AIR, metallib, deux désassemblages,
réflexions et provenance. `.gitattributes` empêche la conversion LF/CRLF.
Les journaux bruts avec chemins personnels restent dans `out/`, ignoré par Git :
`translator-graphics-build.log`, `metal-graphics-python-tests.log`,
`vulkan-graphics-mac-tests.log`, `metal-graphics-admission.json`,
`graphics-clean-translator.log`, `graphics-clean-metal/` et
`metal-graphics-bindless-rejected/`. Le JSON public conserve les cas logiciels,
révisions, empreintes et les deux admissions GPU-free. Aucune récupération
système n'est nécessaire : aucun pilote installé ni dispatch soumis.

Sur Windows, suivre [la procédure graphique](../VALIDATION-GRAPHICS.md) avec
`-ShaderOrigin metal-air`, puis rejouer le contrôle GLSL et le calcul Apple
avec le banc modifié. Les 48 cas doivent comparer tous les canaux, les gardes
et les paramètres sans erreur Vulkan/synchronisation avant de qualifier
**ce nouveau corpus sur la RX 9070 XT**.
