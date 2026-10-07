# Produire l'AIR Apple sur le Mac

Cette procédure produit un corpus de calcul pour l'étape 2. Elle ne construit
ni n'installe de pilote et n'exécute aucune commande GPU.
[Rapport du premier essai](reports/2026-10-07-apple-air.md).

## Environnement qualifié pour ce shader

MacBookAir7,2 x86_64, Core i5-5350U, 8 Gio, macOS 15.7.8 / 24G824.
Xcode 26.3 / 17C529, SDK macOS 26.2, composant Metal Toolchain 17C7003j,
Apple Metal/AIR-LLD 32023.864. Rust/Cargo 1.98.1, LLVM 20.1.8,
SPIRV-Tools v2026.2, CMake 4.4.3 et Python 3.9.6.

AIR64 est un IR GPU à pointeurs 64 bits, **pas** un exécutable hôte x86_64.
La cible `air64-apple-macosx26.0` produit ici
`air64_v28-apple-macosx26.0.0` et Metal 4.0. Le Mac hôte est sous Sequoia,
pas Tahoe. La metallib cible Tahoe et n'est pas essayée sur son Intel HD 6000.

## Préparer les outils

Depuis la racine du dépôt, avec Xcode sélectionné (`xcode-select -p`) :

```bash
mkdir -p out/tools
xcodebuild -downloadComponent MetalToolchain
xcrun --sdk macosx metal --version
xcrun --sdk macosx metallib --version
brew install llvm@20
```

Le téléchargement Apple était d'environ 705 Mo. Le simple résultat de
`xcrun --find metal` ne suffisait pas : il trouvait le lanceur alors que le
composant Metal manquait. LLVM 20.1.8 sait lire le bitcode obtenu ; aucune
modification du bitcode n'est nécessaire. La formule `llvm@20` est keg-only.

Si Rust >= 1.87 n'est pas déjà installé, installation officielle observée
sur ce Mac Intel, sans modification des fichiers de profil :

```bash
curl --fail --location --proto '=https' --tlsv1.2 \
  https://static.rust-lang.org/rustup/dist/x86_64-apple-darwin/rustup-init \
  -o out/tools/rustup-init
curl --fail --location --proto '=https' --tlsv1.2 \
  https://static.rust-lang.org/rustup/dist/x86_64-apple-darwin/rustup-init.sha256 \
  -o out/tools/rustup-init.sha256
(cd out/tools && shasum -a 256 -c rustup-init.sha256)
chmod +x out/tools/rustup-init
out/tools/rustup-init --profile minimal --default-toolchain 1.98.1 --no-modify-path -y
export PATH="$HOME/.cargo/bin:$PATH"
export RUSTUP_TOOLCHAIN=1.98.1
```

Un Mac Apple Silicon nécessite l'installateur Rust `aarch64-apple-darwin` ;
cette machine et ce chemin ne sont pas qualifiés par notre essai Intel.
Les empreintes et provenances observées figurent dans le rapport JSON.

SPIRV-Tools a été construit localement à la même révision que le SDK Windows :

```bash
git clone --depth 1 --branch vulkan-sdk-1.4.350.0 \
  https://github.com/KhronosGroup/SPIRV-Tools.git out/tools/src/SPIRV-Tools
git clone --depth 1 --branch vulkan-sdk-1.4.350.0 \
  https://github.com/KhronosGroup/SPIRV-Headers.git \
  out/tools/src/SPIRV-Tools/external/spirv-headers
cmake -S out/tools/src/SPIRV-Tools -B out/tools/spirv-build \
  -DCMAKE_BUILD_TYPE=Release -DSPIRV_SKIP_TESTS=ON \
  -DSPIRV_SKIP_EXECUTABLES=OFF -DSPIRV_WERROR=OFF \
  -DCMAKE_OSX_ARCHITECTURES=x86_64 -DCMAKE_OSX_DEPLOYMENT_TARGET=15.0
cmake --build out/tools/spirv-build --target spirv-val spirv-dis -j 2
```

Vérifier les révisions avant de comparer les résultats : SPIRV-Tools
`0539c81f69a3daeb706fd3477dca61435b475156`, SPIRV-Headers
`ad9184e76a66b1001c29db9b0a3e87f646c64de0`. Les tests internes de SPIRV-Tools
sont désactivés dans cette construction ; nous vérifions son usage comme
validateur, pas sa suite complète. Ne pas remplacer un checkout existant
silencieusement lors d'un nouvel essai.

## Construire et conserver le corpus

```bash
export METAL2VULKAN_LLVM_DIS="$(brew --prefix llvm@20)/bin/llvm-dis"
export METAL2VULKAN_SPIRV_VAL="$PWD/out/tools/spirv-build/tools/spirv-val"
export METAL2VULKAN_SPIRV_DIS="$PWD/out/tools/spirv-build/tools/spirv-dis"
bash tools/build-translator.sh
bash tools/compile-metal-reference.sh out/metal-reference-new
(cd out/metal-reference-new && shasum -a 256 -c SHA256SUMS)
```

`build-translator.sh` exécute désormais 15 tests autonomes avec `--locked` et
réflexion JSON, dont `apple_vector_add` et quatre cas `apple_graphics`.
Les 15 passent sur Mac et sous Windows ; le
[rapport Metal/Radeon](reports/2026-10-07-metal-graphics-radeon.md) conserve
aussi les 48 cas graphiques Apple AIR exécutés sur la RX 9070 XT.
Quatre balayages du corpus amont absent sont
filtrés ; `cargo test --lib` reste bloqué par trois fixtures manquantes.
Le cas synthétique reste distinct du shader compilé par Apple.

La sortie de compilation Metal doit être **nouvelle ou vide**. Le script
préserve la copie source, AIR, désassemblage LLVM, metallib, SPIR-V,
désassemblage SPIR-V, réflexion, `provenance.json`, `SHA256SUMS`, `status.txt`
et `build.log`. Une erreur arrête le script ; le manifeste de réussite n'est
écrit qu'après les contrôles. `build.log` reste local : il contient des chemins.

La source est transmise par stdin : ce compilateur conserve un chemin absolu
dans `!air.source_file_name` malgré `-ffile-prefix-map`. Le bitcode n'est jamais
réécrit. Le script contrôle l'ABI statique du calcul et consigne explicitement
l'absence d'essai GPU. Options : `METAL_REFERENCE_TARGET`,
`METAL_REFERENCE_STD`, `METAL2VULKAN_BIN` et les trois overrides d'outils ci-dessus.
Toute variation d'outil ou d'option nécessite ses propres preuves.

La reconstruction observée utilisait une copie des sources et des sorties
Cargo/Metal neuves, avec `CARGO_NET_OFFLINE=true` et les crates du lockfile
**déjà en cache**. Sept artefacts sont identiques, y compris AIR et metallib.
Les exécutables Rust de debug et les métadonnées datées ne sont pas annoncés
identiques. Ce résultat ne qualifie pas l'assemblage AMD complet.

## Produire les shaders graphiques

Avec les mêmes overrides et le traducteur construit :

```bash
python3 tools/compile-metal-graphics.py out/metal-graphics-new
python3 tools/compile-metal-graphics.py --check tests/shaders/apple/graphics
python3 -B -m unittest discover -s tests/tools -v
cmake -S tests/vulkan -B out/graphics-software -DPROBE_SOFTWARE_ONLY=ON
cmake --build out/graphics-software
ctest --test-dir out/graphics-software --output-on-failure
```

Le nouveau script conserve les quatre sources, AIR, metallibs, SPIR-V,
désassemblages et réflexions ; il écrit une provenance et un manifeste à noms
relatifs. La sortie doit être neuve/vide. La compilation sélectionne
`NVMTL_NO_BINDLESS_ALL=1` : les ressources sont des descriptors directs, sans
heap NVIDIA. Le script refuse un désaccord entre réflexion et SPIR-V.
La texture utilise image 32 et sampler 160 ; elle exige shaderInt8.
Les deux shaders colorés partagent un buffer DrawParams de 32 octets au binding 0.
Ce sont des contrats de tests, pas la qualification du backend Metal AMD.

Les 28 artefacts de ce corpus sont reproduits depuis une copie des sources
avec target Cargo neuf et dépendances déjà en cache. Le build CMake sans SDK
lance quatre tests C++ d'admission et de référence sans loader ni GPU.
Un build complet du banc C++ et ses 13 CTest a également été exécuté sur Mac,
avec headers/loader Vulkan et glslang construits localement ; aucun dispatch
GPU n'est nécessaire à ces tests. Voir [rapport graphique Metal/Mac](reports/2026-10-07-metal-graphics.md).
La commande Radeon est dans [Validation graphique](VALIDATION-GRAPHICS.md).

## Transférer et exécuter sur Windows

Le corpus partageable est conservé dans [`tests/shaders/apple/`](../tests/shaders/apple/).
`.gitattributes` y désactive la conversion LF/CRLF pour conserver les octets
même avec `core.autocrlf` sous Windows. Sur le PC Ryzen/Radeon, vérifier les
empreintes après transfert :

```powershell
$corpus = 'tests/shaders/apple'
Get-Content "$corpus/SHA256SUMS" | ForEach-Object {
    $expected, $name = $_ -split '\s+', 2
    $actual = (Get-FileHash -Algorithm SHA256 -LiteralPath "$corpus/$name").Hash.ToLowerInvariant()
    if ($actual -ne $expected) { throw "Empreinte incorrecte : $name" }
}
. ./tools/initialize-windows-dev.ps1
./tools/run-windows-probe.ps1 -DeviceId 0x7550 `
  -Shader "$corpus/vector_add.spv" `
  -Reflection "$corpus/vector_add.reflection.json" `
  -ShaderOrigin metal-air
```

`0x7550` provient de l'inventaire cible déjà relevé ; vérifier sa concordance
sur la machine utilisée. Le banc refuse un autre GPU et active Int64 lorsque
le module l'exige. Les 36 cas doivent vérifier toutes les allocations et
produire zéro écart et zéro erreur Vulkan/synchronisation avant de qualifier
**ce** shader. Aucune exécution de cette commande Windows n'est revendiquée
par le rapport Mac. Détails : [banc Windows](VALIDATION-WINDOWS.md).

Cette commande a ensuite été exécutée sur le PC Ryzen/Radeon : les 36 cas
Apple AIR passent et correspondent au contrôle GLSL et à la référence CPU,
sans erreur Vulkan/synchronisation. Les 11 tests Rust ciblés passent aussi
sous Windows. Preuves : [rapport Apple AIR/Radeon](reports/2026-10-07-apple-air-radeon.md).
