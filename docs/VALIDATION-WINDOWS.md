# Premier banc de validation Windows

Le banc construit un exécutable x64 Vulkan. Il sélectionne un unique périphérique
AMD discret dont le nom contient `RX 9070 XT` et dont l'ID PCI correspond au relevé
fourni. L'absence ou l'ambiguïté de la cible produit un code de sortie non nul et
un rapport d'échec. Il ne soumet aucune commande à un autre GPU.

## Prérequis et compilation

Versions observées lors du premier essai : Rust/Cargo 1.96.0, Visual Studio 2019
Build Tools (MSVC 19.29), SDK Windows 10.0.19041.0, CMake 4.3.3 et Vulkan SDK
1.4.350.0. Le traducteur demande Rust >= 1.87. Le SDK doit fournir les headers,
le loader, `glslangValidator`, `spirv-val` et la couche de validation Khronos.
Ces versions constituent un relevé, pas une qualification macOS.

Depuis la racine du dépôt, en PowerShell :

```powershell
./tools/build-translator.ps1
cmake -S tests/vulkan -B out/vulkan -G "Visual Studio 16 2019" -A x64
cmake --build out/vulkan --config Debug
ctest --test-dir out/vulkan -C Debug --output-on-failure
./out/vulkan/Debug/amd_gpu_probe.exe --list
```

Un autre compilateur C++17 convient si CMake le détecte ; un générateur à une
configuration place l'exécutable directement dans `out/vulkan`. Le script
`run-windows-probe.ps1` sait utiliser les deux dispositions. CMake compile le
shader GLSL et lance `spirv-val --target-env vulkan1.2` avant son utilisation.

`tools/build-translator.ps1` et `tools/build-translator.sh` utilisent `--locked`,
construisent le CLI avec réflexion JSON et lancent **10 tests autonomes**. Ils
excluent explicitement les 4 cas `every_public_fixture*` des suites choisies.
La suite unitaire complète reste bloquée par trois `include_str!` dont les
fixtures `validation/fixtures/public/` sont absentes. Les autres suites ne sont
pas couvertes par ce résultat. `-SkipTests` ne produit aucune preuve de test.

Le script traduit aussi `vector_add.synthetic.ll`. Cette fixture LLVM/AIR a été
écrite pour le projet ; elle n'est pas issue du compilateur Metal d'Apple. Son
succès vérifie une partie du CLI et du contrat, pas la compilation d'un AIR réel.

## Inventaire sur le PC cible

```powershell
./tools/collect-windows-inventory.ps1 -Role target `
  -CardManufacturer "à relever" -Vbios "à relever" `
  -Monitor "à choisir" -Connector "à choisir"
```

Le script recueille carte mère, BIOS, CPU, RAM, build Windows, GPU, IDs PCI et
sous-système, pilotes et versions des outils. Les numéros de série, comptes,
adresses réseau et le suffixe personnel des IDs PnP sont omis. Les informations
VBIOS/fabricant de la carte, écran/connecteur et stockage/périphériques de
démarrage restent à relever manuellement. Les erreurs CIM sont enregistrées
et produisent un échec ; un inventaire `target` refuse un PC sans RX 9070 XT.

Les fichiers sont écrits dans `reports/local/`, ignoré par Git. Le rôle
`development` permet d'inventorier une autre machine sans la confondre avec la
cible. Ne recopier un rapport en public qu'après revue, y compris ses champs
manuels et les chemins présents dans les journaux des outils.

## Calcul sur la Radeon

Lire l'ID effectivement relevé ; ne pas réutiliser un ID supposé depuis une
fiche technique :

```powershell
$inventory = Get-Content -Raw reports/local/windows-target.json | ConvertFrom-Json
$radeons = @($inventory.graphics | Where-Object { $_.vendor_id -eq '0x1002' -and $_.name -match 'RX 9070 XT' })
if ($radeons.Count -ne 1) { throw 'La cible doit être unique.' }
./tools/run-windows-probe.ps1 -DeviceId $radeons[0].device_id
```

Cette première commande utilise **le contrôle GLSL**, enregistré comme
`glsl-control` dans la provenance. Elle vérifie le banc sur le GPU, sans valider
la traduction Metal. Le lancement active la couche Khronos ; son absence
produit un échec. `-WithoutValidation` est possible pour diagnostic, mais doit
rester visible dans le rapport.

Tests exécutés : 1, 63, 64, 65, 257 et 4097 éléments, trois fois chacun, avec
des entrées différentes et des additions modulo 2^32 (tolérance zéro). Les
groupes font 64 threads ; le shader protège les threads excédentaires. Le banc
vérifie chaque résultat, les 16 mots de garde avant/après, toute la fin inutilisée
des allocations, les entrées et les paramètres. Une sortie préremplie ne peut
être acceptée comme calcul sans être comparée à la référence.

Le prototype emploie des buffers Vulkan en mémoire hôte visible et cohérente,
des barrières host/compute/host et une fence par soumission (5 secondes maximum).
Il ne teste pas encore les transferts staging/VRAM, textures ou images. Chaque
cas conserve les écarts et des sommes de contrôle FNV-1a de la sortie complète
et de la référence ; ces sommes servent au diagnostic, pas au contrôle
d'intégrité des artefacts, qui utilise SHA-256.

Le rapport `result.json` contient GPU/pilote, ID et type, version Vulkan, file,
local size, besoin `shaderInt64`, options, état et résultats. `provenance.json`
ajoute date UTC, build Windows, révision Git, état du travail, empreintes des
sources et binaires. Le wrapper lève une erreur si le banc sort avec un échec.
Sur timeout, le processus échoue et évite une attente GPU infinie pendant sa
destruction ; cette voie de récupération n'a pas encore été essayée sur Radeon.

## Produire et transférer le shader Metal

Sur le Mac, depuis une copie du projet :

```bash
bash tools/build-translator.sh
bash tools/compile-metal-reference.sh
```

Il faut Xcode avec les outils Metal, Rust, `llvm-dis` capable de lire l'AIR produit
et `spirv-val`. Les overrides `METAL2VULKAN_LLVM_DIS` et
`METAL2VULKAN_SPIRV_VAL` permettent de fixer les binaires. La compilation vise
`air64-apple-macosx26.0` ; sa disponibilité et la compatibilité LLVM restent à
vérifier sur le Mac réel. Le script conserve source, AIR, metallib, SPIR-V,
réflexion, versions et empreintes. Il ne lance pas le shader via Metal.

Transférer les artefacts et leurs preuves sur le PC cible, puis lancer :

```powershell
./tools/run-windows-probe.ps1 -DeviceId $radeons[0].device_id `
  -Shader out/metal-reference/vector_add.spv `
  -Reflection out/metal-reference/vector_add.reflection.json
```

Le contrat attendu est celui du **code livré** : réflexion version **56**, stage
Kernel, dispatch Workgroups, local size 64x1x1 et quatre storage buffers dans le
set 0 / bindings 0 à 3 (`a`, `b`, `result`, `{count, offset}`). La documentation
héritée indique encore 49 : ce nombre n'est pas celui du traducteur actuel.
La réflexion donne le nom de la fonction AIR (`vector_add`) ; le SPIR-V émis
utilise **`main`**. Le banc vérifie cette entrée et le local size avant la
sélection GPU. Le cas synthétique exige `shaderInt64` pour ses index : cette
capacité est demandée au GPU puis activée explicitement, ou refusée.

Pour diagnostiquer le contrat de la fixture synthétique, fournir ses artefacts
de `out/translator/fixture/` et ajouter `-ShaderOrigin synthetic-ir`. Cette
origine reste distincte de `metal-air` dans la provenance.

`amd_gpu_probe --check-shader FILE` contrôle uniquement l'admission de l'ABI sans
exécuter Vulkan ; `spirv-val` reste obligatoire pour la validité structurelle.
Un autre shader n'est accepté que s'il respecte le contrat restreint du banc.
Seule une exécution réussie sur la RX 9070 XT avec les artefacts issus du Mac
pourra fermer les cases correspondantes de l'étape 2.
