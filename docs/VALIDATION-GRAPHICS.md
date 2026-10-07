# Validation graphique hors écran sous Windows

Le banc `amd_gpu_probe --graphics` sélectionne la même cible PCI explicite que
le calcul : une unique RX 9070 XT AMD discrète. Il rend avec Vulkan dans des
images en mémoire device-local, relit chaque pixel et le compare à une
référence CPU. Le CPU ne produit pas l'image testée.

Cette première version utilise **quatre shaders de contrôle GLSL**, compilés
en SPIR-V Vulkan 1.2. Elle qualifie les copies d'images, l'échantillonnage, la
rastérisation et le blending du banc sous le pilote AMD Windows. Le calcul
`vector_add` issu d'AIR Apple reste un contrôle distinct ; les shaders
graphiques Metal/AIR et leur ABI de ressources restent à ajouter.

## Exécuter le corpus

Depuis la racine du dépôt, dans PowerShell, avec les outils décrits dans
[Validation Windows](VALIDATION-WINDOWS.md) :

```powershell
. ./tools/initialize-windows-dev.ps1
cmake -S tests/vulkan -B out/vulkan -G Ninja -DCMAKE_BUILD_TYPE=Debug
./tools/run-windows-graphics-probe.ps1 -DeviceId 0x7550
```

Remplacer l'ID par celui de l'inventaire de la cible. Le script configure et
construit le banc, compile les shaders, valide leur SPIR-V, lance les **7 CTest**
et exécute les **48 cas GPU**. La validation Khronos et sa validation de
synchronisation sont obligatoires par défaut. `-WithoutValidation` reste une
option de diagnostic consignée dans la provenance.

Pour conserver un dossier connu et obtenir des aperçus PNG sans dépendance
Python externe :

```powershell
./tools/run-windows-graphics-probe.ps1 -DeviceId 0x7550 `
  -ReportDirectory reports/local/graphics-check
python tools/render-graphics-report.py reports/local/graphics-check
```

Un nouveau dossier par essai conserve les preuves précédentes. Les paramètres
`-BuildDirectory` et `-ReportDirectory` acceptent aussi des chemins explicites.
Un générateur Visual Studio place le binaire dans `Debug/` ; le wrapper prend
en charge les deux dispositions. Les deux headers du banc sont déclarés
comme dépendances explicites pour reconstruire leurs modifications, y compris
avec un ancien cache Ninja dont la détection des inclusions MSVC est incorrecte.

## Scènes et critères

| Scène | Opérations GPU | Tailles | Cas | Tolérance par canal |
| --- | --- | --- | --- | --- |
| `texture-copy` | Upload → image optimale → readback | 1×1, 3×5, 64×64, 65×37, 257×129 | 15 | 0 |
| `texture-sample` | Upload → texture → sampler nearest → rendu plein écran → readback | mêmes tailles | 15 | 0 |
| `triangle` | Triangle plein, couleur et fond variables | 32×32, 65×37, 127×95 | 9 | 1 unité sur 255 |
| `alpha-blend` | Deux triangles partiellement superposés, dans un ordre fixe | mêmes tailles de triangle | 9 | 1 unité sur 255 |

Chaque taille est exécutée trois fois avec des entrées différentes. Toutes
les images sont `R8G8B8A8_UNORM`, linéaires, avec un seul échantillon par pixel,
sans profondeur ni culling. **Les quatre canaux et tous les pixels sont
comparés**, y compris le fond, les limites de couverture et les régions
superposées. Aucun pixel de bord n'est ignoré.

La texture contient des motifs distincts pour R, G, B et A ; son alpha varie
entre 192 et 255. Le sampler utilise les centres des texels et le filtre
nearest, sans blending : la comparaison exige les mêmes octets.

Pour les triangles, les sommets NDC utilisent des constantes binaires exactes,
avec un décalage vertical de 1/64 pour éviter un centre de pixel situé exactement
sur une arête aux tailles retenues. La référence utilise des coordonnées fixes
avec huit bits fractionnaires ; le banc exige `subPixelPrecisionBits >= 8`.
Il échoue si sa géométrie produit un échantillon d'arête ambigu ou une scène
vide. Ce contrat de test s'appuie sur les règles de
[rastérisation Vulkan](https://docs.vulkan.org/spec/latest/chapters/primsrast.html).

Le blending est configuré avec les facteurs RGB `SRC_ALPHA` et
`ONE_MINUS_SRC_ALPHA`, et les facteurs alpha `ONE` et `ONE_MINUS_SRC_ALPHA`,
avec addition. La référence applique `RGB = source.rgb × source.a + destination.rgb
× (1 − source.a)` et `A = source.a + destination.a × (1 − source.a)` dans
l'ordre des deux dessins, puis quantifie en UNORM8 après chaque dessin.
Les fonds ont aussi un alpha non opaque, pour vérifier le calcul du canal A.
Les paramètres suivent le
[contrat de blending Vulkan](https://docs.vulkan.org/spec/latest/chapters/framebuffer.html).

La tolérance d'une unité pour les scènes colorées couvre les différences
d'arrondi de conversion et de blending UNORM8 entre notre oracle CPU et le
GPU. C'est le seuil de ce corpus, pas une garantie générale de conformité.
L'erreur maximale est enregistrée même lorsqu'elle respecte cette tolérance.
Sur l'essai Radeon publié, elle vaut **zéro dans les 48 cas**.

## Mémoire, synchronisation et preuves

Les images utilisent un type device-local, de préférence non visible au CPU.
Seuls l'upload et le readback sont mappés en mémoire hôte cohérente. Le rapport
consigne les types, heaps et flags effectivement utilisés pour chaque cas,
ainsi que les capacités du format et la précision sous-pixel.

Les barrières couvrent les écritures hôte, la copie vers l'image, la lecture
par le fragment shader ou le transfert, les écritures de l'attachement couleur
et le readback. Ces dépendances sont explicites, conformément aux
[exemples de synchronisation Khronos](https://docs.vulkan.org/guide/latest/synchronization_examples.html).
Une fence borne chaque soumission à cinq secondes. Les ressources de chaque
cas sont détruites après complétion ; cette création/destruction est répétée
sur tout le corpus. La récupération après timeout n'a pas été provoquée sur
la Radeon.

Les buffers upload/readback portent **64 octets de garde `0xa7` de chaque côté**.
Le banc vérifie leur intégrité ainsi que tous les octets de l'upload après la
soumission. Tout écart, toute erreur Vulkan ou toute attente échouée produit
un rapport `failed` et un code de sortie non nul.

Le dossier local contient :

- `result.json` : état, GPU, capacités, allocations et résultats des 48 cas.
- `provenance.json` : révision Git, état du checkout, versions, origine GLSL et
  SHA-256 des binaires, sources et **96 fichiers RGBA**.
- `probe.log` : sortie du banc et diagnostics Vulkan.
- `images/<cas>.rgba` et `<cas>.reference.rgba` : octets RGBA8 sans en-tête,
  lignes de haut en bas, dimensions dans le JSON. Les PNG facultatifs
  préservent tous ces octets et l'alpha.

Les checksums FNV servent au diagnostic ; l'acceptation repose sur la
comparaison pixel par pixel. Le SHA-256 conserve l'intégrité des artefacts.
Les compteurs `covered_pixels` et `overlap_pixels` décrivent la couverture
attendue par l'oracle, vérifiée par la comparaison complète de l'image.

## Vérifier les rejets et le calcul Apple

Après compilation, rejouer les essais négatifs :

```powershell
./tools/test-windows-graphics-rejections.ps1 -DeviceId 0x7550
```

Le script prépare des copies des shaders dans son dossier de rapport et
vérifie trois échecs attendus : ID PCI absent, alpha de texture forcé à zéro,
et canaux R/B du triangle inversés. Les shaders altérés restent valides
structurellement ; leur résultat GPU doit être rejeté par l'oracle, sans
erreur de validation Vulkan. L'exécutable normal reste identique.
`-AbsentDeviceId` permet de choisir un autre ID absent.

Conserver aussi la régression du calcul réel transféré depuis le Mac :

```powershell
./tools/run-windows-probe.ps1 -DeviceId 0x7550 `
  -Shader tests/shaders/apple/vector_add.spv `
  -Reflection tests/shaders/apple/vector_add.reflection.json `
  -ShaderOrigin metal-air
./tools/run-windows-probe.ps1 -DeviceId 0x7550
```

Le [rapport hors écran du 7 octobre](reports/2026-10-07-offscreen-radeon.md)
publie les images utiles, les 48 cas, les trois rejets et les 36 paires de
calcul Apple/GLSL. RADV Darwin, les shaders graphiques AIR, le pilote noyau,
l'exécution via Metal et le bureau Tahoe restent à qualifier séparément.
