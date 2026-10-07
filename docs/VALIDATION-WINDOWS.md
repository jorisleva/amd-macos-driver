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

Sur le PC cible, MSVC 19.51, SDK Windows 10.0.26100.0, CMake 4.3.1-msvc1 et
Rust 1.98.1 sont disponibles. Le SDK Vulkan est préparé dans `out/tools`, sans
enregistrement des couches ni changement du PATH système, avec le mode
`copy_only=1` documenté par [LunarG](https://vulkan.lunarg.com/doc/view/1.4.350.0/windows/getting_started.html).
Le téléchargement est figé à 1.4.350.0 et vérifié par son SHA-256 officiel.
L'extracteur utilise aussi son cache temporaire utilisateur.

Depuis la racine du dépôt, en PowerShell :

```powershell
./tools/prepare-windows-vulkan-sdk.ps1
. ./tools/initialize-windows-dev.ps1
./tools/build-translator.ps1
cmake -S tests/vulkan -B out/vulkan -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build out/vulkan --config Debug
ctest --test-dir out/vulkan -C Debug --output-on-failure
./out/vulkan/amd_gpu_probe.exe --list
```

La préparation peut télécharger environ 275 Mo et copier environ 1 Go. Elle
réutilise les fichiers du SDK s'ils sont déjà présents. L'initialisation doit
être **dot-sourcée** à chaque nouvelle session PowerShell : elle découvre les
C++ Build Tools x64 avec `vswhere`, ajoute CMake/Ninja si nécessaire, et configure
`VULKAN_SDK`, le PATH du processus et `VK_ADD_LAYER_PATH` pour la couche de
validation locale. Un SDK existant peut être choisi avec
`. ./tools/initialize-windows-dev.ps1 -SdkDirectory C:/VulkanSDK/1.4.350.0`.
Un générateur Visual Studio peut également être utilisé ; changer de générateur
nécessite un nouveau dossier de compilation.

Un autre compilateur C++17 convient si CMake le détecte ; un générateur à une
configuration place l'exécutable directement dans `out/vulkan`. Le script
`run-windows-probe.ps1` sait utiliser les deux dispositions. CMake compile le
shader GLSL et lance `spirv-val --target-env vulkan1.2` avant son utilisation.

`tools/build-translator.ps1` et `tools/build-translator.sh` utilisent `--locked`,
construisent le CLI avec réflexion JSON et sélectionnent désormais **15 tests
autonomes**, dont `apple_vector_add` et quatre régressions `apple_graphics`
sur les désassemblages de nos AIR Apple. Les 15 passent sur Mac et sous
Windows ; le [rapport Metal/Radeon](reports/2026-10-07-metal-graphics-radeon.md)
conserve le rejeu PowerShell, les 13 CTest et huit tests Python. Les scripts
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
la traduction Metal. Le lancement active la couche Khronos et sa validation
de synchronisation avec `--sync-validation` ; l'absence de la couche
produit un échec. `-WithoutValidation` est possible pour diagnostic, mais doit
rester visible dans le rapport.

Tests exécutés : 1, 63, 64, 65, 257 et 4097 éléments, trois fois chacun, avec
des entrées différentes, sur **deux chemins mémoire**, soit **36 cas par shader**,
et des additions modulo 2^32 (tolérance zéro). Les
groupes font 64 threads ; le shader protège les threads excédentaires. Le banc
vérifie chaque résultat, les 16 mots de garde avant/après, toute la fin inutilisée
des allocations, les entrées et les paramètres. Une sortie préremplie ne peut
être acceptée comme calcul sans être comparée à la référence.

Le chemin `host-coherent` emploie des buffers hôte visibles et cohérents et
des barrières host/compute/host. Le chemin `device-local-staging` copie les quatre
buffers vers des allocations device-local, exécute le calcul puis recopie
**toutes** les allocations, y compris entrées/paramètres/gardes, vers le staging.
Les barrières couvrent host → transfer → compute → transfer → host et la
réutilisation du staging. Le choix mémoire préfère un type device-local non
visible au CPU et consigne les flags réellement sélectionnés. Sur la Radeon
relevée, les buffers utilisent le type 0, flags `DEVICE_LOCAL` seuls, heap 1.
Chaque soumission emploie une fence (5 secondes maximum). Les textures et images
sont maintenant testées par le [banc hors écran](VALIDATION-GRAPHICS.md) : 48 cas
de contrôle GLSL, avec comparaison complète RGBA et gardes. Les dispatchs
dépendants, files multiples et mémoire hôte non cohérente restent à tester. Chaque
cas conserve les écarts et des sommes de contrôle FNV-1a de la sortie complète
et de la référence ; ces sommes servent au diagnostic, pas au contrôle
d'intégrité des artefacts, qui utilise SHA-256.

Le rapport `result.json` (schéma 2) contient GPU/pilote, ID et type, version Vulkan,
file, local size, besoin `shaderInt64`, validation de synchronisation, allocations
(types, heaps, flags et dimensions des heaps), chemin mémoire, état et résultats.
`probe.log` conserve la sortie et les diagnostics Vulkan. `provenance.json`
ajoute date UTC, build Windows, révision Git, état du travail, empreintes des
sources et binaires ainsi que les versions des outils C++ et SPIR-V. Le wrapper
lève une erreur si le banc sort avec un échec.
Sur timeout, le processus échoue et évite une attente GPU infinie pendant sa
destruction ; cette voie de récupération n'a pas encore été essayée sur Radeon.

## Produire et transférer le shader Metal

Le [premier AIR Apple](reports/2026-10-07-apple-air.md) est maintenant compilé
et traduit sur Mac. Le corpus partageable est dans
[`tests/shaders/apple/`](../tests/shaders/apple/). La
[procédure Mac](VALIDATION-MACOS.md) détaille outils, révisions, préparation,
vérification des empreintes et transfert Windows.

Pour régénérer, après configuration des outils décrite dans cette procédure :

```bash
bash tools/build-translator.sh
bash tools/compile-metal-reference.sh out/metal-reference-new
```

La sortie Metal doit être nouvelle ou vide. Le script utilise les overrides
`METAL2VULKAN_LLVM_DIS`, `METAL2VULKAN_SPIRV_VAL` et `METAL2VULKAN_SPIRV_DIS`.
Apple Metal 32023.864 / SDK macOS 26.2 et LLVM 20.1.8 ont été essayés avec
`air64-apple-macosx26.0` et Metal 4.0. Le script conserve source, AIR, metallib,
les deux désassemblages, SPIR-V, réflexion, provenance JSON et empreintes
relatives. Il vérifie le contrat statique et ne lance pas le shader via Metal.

Transférer les artefacts et leurs preuves sur le PC cible, puis lancer :

```powershell
./tools/run-windows-probe.ps1 -DeviceId $radeons[0].device_id `
  -Shader tests/shaders/apple/vector_add.spv `
  -Reflection tests/shaders/apple/vector_add.reflection.json `
  -ShaderOrigin metal-air
```

Le contrat attendu est celui du **code livré** : réflexion version **56**, stage
Kernel, dispatch Workgroups, local size 64x1x1 et quatre storage buffers dans le
set 0 / bindings 0 à 3 (`a`, `b`, `result`, `{count, offset}`). La documentation
héritée indique encore 49 : ce nombre n'est pas celui du traducteur actuel.
La réflexion donne le nom de la fonction AIR (`vector_add`) ; le SPIR-V émis
utilise **`main`**. Le banc vérifie cette entrée et le local size avant la
sélection GPU. Le cas synthétique **et le premier shader issu d'Apple** exigent
`shaderInt64` pour leurs index : cette capacité est demandée au GPU puis activée
explicitement, ou refusée. Le module Apple a été exécuté sur la Radeon : les
36 cas passent, sans écart ni erreur Vulkan/synchronisation, et leurs sorties
correspondent au contrôle GLSL. Les empreintes transférées correspondent
au manifeste du Mac. Preuves : [rapport Apple AIR/Radeon](reports/2026-10-07-apple-air-radeon.md).

Pour diagnostiquer le contrat de la fixture synthétique, fournir ses artefacts
de `out/translator/fixture/` et ajouter `-ShaderOrigin synthetic-ir`. Cette
origine reste distincte de `metal-air` dans la provenance.

```powershell
./tools/run-windows-probe.ps1 -DeviceId 0x7550 `
  -Shader out/translator/fixture/vector_add.spv `
  -Reflection out/translator/fixture/vector_add.reflection.json `
  -ShaderOrigin synthetic-ir
```

Cette commande a réussi sur la RX 9070 XT avec les 36 cas, zéro écart et zéro
erreur de validation/synchronisation. Ses sorties correspondent au contrôle
GLSL. Le [rapport Radeon](reports/2026-10-07-radeon-windows.md) contient les
preuves partageables ; ce succès ne représente pas un AIR compilé par Apple.

`amd_gpu_probe --check-shader FILE` contrôle uniquement l'admission de l'ABI sans
exécuter Vulkan ; `spirv-val` reste obligatoire pour la validité structurelle.
Un autre shader n'est accepté que s'il respecte le contrat restreint du banc.
L'exécution réussie sur la RX 9070 XT avec les artefacts issus du Mac qualifie
le premier calcul de l'étape 2. Le [corpus graphique de contrôle GLSL](VALIDATION-GRAPHICS.md)
et ses équivalents Metal/AIR sont désormais validés sur Radeon, avec 48 cas
chacun et des fichiers RGBA identiques ; voir le
[rapport Metal/Radeon](reports/2026-10-07-metal-graphics-radeon.md).
La qualification macOS reste ouverte.
