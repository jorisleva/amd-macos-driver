# Hackintosh Tahoe : sauvegarde EFI, compilation et audit Navi48

Travail effectué les **8–9 octobre 2026 UTC**, directement sur le Ryzen/Radeon.
**EFI sauvegardée et relue ; Navi48Bringup 0.0.620 construit deux fois pour
x86_64 ; aucun kext installé ni chargé.** L'audit est ciblé sur le démarrage,
les entrées natives, la mémoire et la récupération, pas une certification
exhaustive du pilote. **Le kext amont n'est pas admis comme premier module
d'identification PCI sans écritures.**

## Machine réellement observée

- macOS **26.7.1 / 25G241**, Darwin 25.6.0, x86_64, SMBIOS MacPro7,1.
- Ryzen 5 5600X, 16 Gio ; GPU `1002:7550`, révision `c0`, sous-système
  **`1849:5417`**, conforme à la RX 9070 XT ASRock inventoriée sous Windows.
- Bureau 1920 × 1080 via `.Display_boot` / `IONDRVFramebuffer`, 7 Mo déclarés.
  `MTLCopyAllDevices()` renvoie **0** ; aucun service `IOAccelerator`.
  AMDSupport est présent, mais pas d'accélérateur Radeon/Navi48.
- SIP et authenticated root activés. Arguments conservés :
  `-v keepsyms=1 debug=0x100 navi48bringup=0 rdna4-off=1 -liludbgall`.
- Command Line Tools, Apple Clang **21.0.0 / clang-2100.1.1.101**, SDK macOS
  **26.5**, Python **3.9.6**. Pas d'installation de Xcode complet, Rust ou
  Homebrew pour ce build : ils ne sont pas requis par le Makefile du kext.

Le démarrage du système installé est désormais **observé**, contrairement au
précédent rapport limité au menu d'installation. Plusieurs démarrages à froid,
le secours, la veille et la stabilité prolongée restent non qualifiés.

Ressources **déclarées par IORegistry**, sans lecture directe des registres
GPU, sans mapping MMIO et sans module de diagnostic chargé :

| Registre PCI | Plage de base | Taille |
| --- | --- | --- |
| BAR0 `0x10` | `0x440000000` | 256 Mio |
| BAR2 `0x18` | `0x450000000` | 2 Mio |
| BAR4 `0x20` (I/O) | `0xe000` | 256 octets |
| BAR5 `0x24` | `0xfcb00000` | 512 Kio |
| ROM `0x30` | `0xfcb80000` | 128 Kio |

Ce relevé ne valide ni l'accès aux BAR, ni la VRAM, ni le DMA.

## Sauvegarde privée de l'EFI

Le volume est identifié comme **OPENCORE**, FAT32, partition USB de
8 577 359 872 octets du Hitachi HTS545050A7E de 500 Go. Aucun formatage,
démontage, remplacement de configuration ou changement NVRAM.

Sauvegarde sur le disque demandé :

```text
/Volumes/OPENCORE/PROFILS-TAHOE/avant-navi48-20261008T235644Z/
  EFI/
  EFI.zip
  backup.json
  RESTAURATION.txt
```

Copie supplémentaire dans
`out/efi-backups/avant-navi48-20261008T235644Z/`, ignorée par Git.
**98 fichiers, 7 066 947 octets**, fichiers cachés compris. Les trois arbres
(source active, sauvegarde USB, copie locale) ont été relus et comparés par
SHA-256, y compris après les builds. L'archive est également vérifiée fichier
par fichier. Aucun écart ; EFI active inchangée.

- SHA-256 `EFI.zip` :
  `a391f6243750b8cda85e54dd0de0b0698a406fce7a25c98f855f1c9d711f0d9c`.
- SHA-256 `OC/config.plist` actif :
  `48c33ed18b3f09518d817c0ac8697c482be6a0c08b664bcebbf93ccaa59d21f7`,
  identique au correctif français déjà consigné.
- La première copie `avant-navi48-20261008T235509Z` a été **rejetée** : le
  transfert des métadonnées de répertoires sur FAT32 avait omis un fichier
  AppleDouble `._Contents`. Elle porte `INCOMPLETE-NE-PAS-UTILISER.txt`.
  La seconde copie n'applique pas ces métadonnées et conserve les 98 fichiers.

**Ne pas publier les sauvegardes : elles contiennent les identifiants SMBIOS.**
La restauration consistera à remplacer le dossier EFI complet depuis un autre
OS/Recovery après conservation de l'état défaillant, pas à fusionner les
répertoires. **Copie vérifiée ne signifie pas restauration ou boot de secours
essayé.** La copie sur le même disque ne protège pas d'une panne de ce disque ;
la copie locale est conservée aussi pour cette raison.

## Construction à partir des sources épinglées

| Entrée | Révision |
| --- | --- |
| Navi48-MacOS | `696959753070e9a16677be485580dc53b06a7ab5` |
| MacKernelSDK | `05094e5e88cec7caedbfb35e8449ed0db94bf95b` |
| linux-firmware | `5ff473283bf11ba0a8b6b04a075ae01676aee10d` |
| Dépôt au début de la session | `22030300d89201ba2c2f05e67fa175092845bab1` |

Le checkout amont est détaché et ses sources suivies restent **inchangées**.
Le nouvel outil `tools/build-navi48.py` exporte le commit dans un arbre neuf,
réextrait le SDK depuis l'archive au SHA-256 épinglé, copie les dix firmwares
après vérification et lance uniquement `make ... all`. Il refuse une sortie
existante ou extérieure à `out/`. Les notices sont conservées.

Corrections de l'outillage, sans changement du pilote amont :

- `tools/pinned_downloads.py` calcule désormais le SHA-256 par blocs, compatible
  avec Python 3.9. L'ancien `hashlib.file_digest` nécessitait Python 3.11.
- GNU make supprimait les C firmware intermédiaires après le lien ; le premier
  contrôle post-build a donc échoué sur un fichier absent, **pas sur la
  compilation**. Un second makefile contenant `.SECONDARY:` les conserve pour
  l'audit, sans modifier les sources ni les flags du compilateur.
- `tools/navi48_binary.py` contrôle en plus les symboles de taille et les
  **octets des dix blobs dans le Mach-O lié**, pas uniquement les C générés.
  Les cinq blobs privés Apple optionnels ont une taille déclarée nulle.

Artefacts retenus :

```text
out/navi48/qualified-build-a/Navi48Bringup.kext
out/navi48/qualified-build-b/Navi48Bringup.kext
```

« qualified » dans ces noms désigne les contrôles de build, **pas une
qualification de chargement**. Chaque dossier conserve `build-report.json`,
`SHA256SUMS.json`, `embedded-firmware.json`, sources, SDK, notices et logs.

| Contrôle | Résultat |
| --- | --- |
| Deux exports/source et SDK neufs | Construction réussie, sans réutilisation d'objets |
| Bundle | `com.navi48.bringup`, version `0.0.620`, Mach-O `MH_KEXT_BUNDLE`, x86_64 uniquement |
| Signature | Ad hoc ; `codesign --verify --strict --verbose=4` réussi sur les deux copies |
| Firmwares | 10/10 conformes dans les entrées, C générés et exécutables liés |
| Bibliothèques utilisateurs | Aucun `LC_LOAD_DYLIB` relevé par `otool -L` |
| Symboles internes manquants | Aucun selon le contrôle amont étendu aux noms Navi48/AMD/xlat/DCN |
| Imports noyau | 422 symboles recensés ; liaison contre les collections de **ce Tahoe non vérifiée** |
| Sections Mach-O | Les sept sections avec octets de code/données sont identiques ; mêmes dimensions des deux sections zero-fill |
| Binaires signés complets | **Non identiques** : chemins de debug-map (68 entrées OSO par binaire), UUID/signature différents |

SHA-256 des exécutables complets :

- A : `9b9a3157716fd7168546e17e9498d13eb498359a5cae163cc2b22623880b3ae1`.
- B : `62e51e8a640c6480c5762beb54cc314eddf1bbf8de771c8af0869cbe4dbe5f48`.

La construction est reproduite, mais **pas reproductible octet par octet pour
le Mach-O complet** dans ces chemins distincts. Les binaires ne sont pas
publiés : ils contiennent des chemins locaux de debug et ne sont pas admis
pour installation. Les manifestes de build conservent le manifeste des pins
avant la mise à jour documentaire de leur statut ; les révisions sont inchangées.

## Tests logiciels

`tools/test-navi48-host.py` construit les quatre suites publiques avec
`-Wall -Wextra -Werror -O1 -fsanitize=address,undefined -fno-sanitize-recover=all`.
Elles exécutent les helpers purs et des assertions sur le texte des sources,
**sans connexion IOKit ni commande GPU**.

| Suite | Contrôles | Échecs |
| --- | ---: | ---: |
| `native_s1b` : VMID, tables, étapes | 378 | 0 |
| `native_s1c` : ABI, BO, VA, submit, fences, attentes | 507 | 0 |
| `native_hostimport` : pages, limites, libération | 947 | 0 |
| `native_ws_open` : privilèges, sélecteurs, état HUNG | 1 073 | 0 |
| **Total** | **2 905** | **0** |

Les scripts amont de mutations détectent **45 défauts S1c et 115 défauts
open/hostimport distincts**, soit 160/160. Les copies non modifiées passent.
Deux mutants délibérément infinis (IDs 45 et 51 du second script) sont arrêtés
par son `alarm(120)`. Le premier lancement global dépasse la limite du harnais
après le mutant 51 ; la plage **52–150** est ensuite relancée et termine à 0.
L'ensemble des identifiants prévus est vérifié contre les deux journaux : aucun
mutant manquant ou échappé. Les rejets à la compilation font partie de cette
preuve ; elle ne mesure pas la robustesse du noyau en exécution.

Les **21 tests Python du fork** passent : huit contrôles graphiques existants,
cinq tests de téléchargement/hash et huit tests du vérificateur Mach-O.

## Audit ciblé : constats et décision

Les chemins et lignes ci-dessous se réfèrent au commit Navi48 épinglé.

### A1 — Bloquant pour un premier relevé : le mode « read-only » écrit

`src/navi48-bringup/src/Navi48Bringup.cpp` :

- `start()` vers **7230** appelle `setMemoryEnable(true)` avant de considérer
  le niveau demandé.
- `loadOnDieDiscovery()` vers **360** appelle `vramRead32()` ; celui-ci, vers
  **132**, **écrit MM_INDEX_HI et MM_INDEX** pour lire la fenêtre VRAM.
- Vers **7250**, les interruptions sont demandées par défaut. Mettre
  `navi48-interrupts=0` ne supprime pas les écritures précédentes.

Donc `navi48-stage=0`, la propriété `survey-readonly` ou le seul retrait des
options PSP ne prouvent pas l'absence d'écritures. **Extraire un vrai module
PCI-only** ou rester au relevé IORegistry avant une activation matérielle.

### A2 — Bloquant pour un paquet de test : personnalités trop larges

`Info.plist` contient quatre personnalités, dont **Navi48AppleHWServices**
qui publie une correspondance pour le pilote Apple sans `IOPropertyMatch`
conditionnant cette personnalité. Le coupe-circuit `navi48bringup=0` est dans
le `probe()` de **Navi48Bringup** ; ce n'est pas une preuve de désactivation de
toutes les personnalités du paquet. L'attachement effectif du service Apple
sur 25G241 n'a pas été testé.

La voie expérimentale AMDRadeonX6000 et ses hooks privés restent compilés.
Pour un futur paquet natif/probe, **isoler les personnalités et désactiver les
voies Apple**, au lieu de recopier tel quel le bundle amont dans l'EFI.

### A3 — Filtrage matériel à resserrer

Le `probe()` vers **7199** accepte `1002:7550` **et** `1002:7551`, sans
contrôle du sous-système `1849:5417`. Le plist a la même portée. C'est plus
large que notre cible autorisée ; un prototype mono-machine doit refuser les
autres identités avant tout accès matériel.

### A4 — Risque pour l'unique écran : région console inconnue

`chooseVramBase()` vers **282** utilise une base VRAM de secours à **64 Mio**
si la console n'est pas localisée, puis `stagePSP()` peut y écrire. La taille
BAR0 est bornée à 256 Mio, mais cette supposition ne prouve pas l'absence de
recouvrement de notre écran. **Refuser l'initialisation si la région console
n'est pas démontrée**, avant le premier essai sur l'unique GPU.

### A5 — Contrats et limites du transport natif

Points favorables lus et partiellement couverts par les suites pures :

- ABI **1.9**, type `0x4E34384E`, contrôle des tailles et refus des sélecteurs
  inconnus ; ouverture après étage 17, calcul de référence et test VM positif.
- Un seul client natif à la fois, VMID 8, contrôles des plages BO/VA, adresses
  canoniques, allocations et protections READ/EXEC pour les IB.
- Administrateur par défaut ; UID 88 uniquement avec `navi48-metal-ws=1`,
  sélecteurs restreints ; import limité au processus propriétaire.
- Import hôte : 64 Mio par BO, **2 Gio par client** à cette révision, séparés
  des 512 Mio GTT. Certains commentaires historiques citent encore 256 Mio.
- Attentes natives plafonnées à 2 s ; état HUNG persistant, fuite volontaire
  des pages potentiellement encore utilisées par le GPU plutôt que libération
  dangereuse. Une telle panne peut donc exiger un redémarrage.

Limites importantes :

- `native_s1c.cpp:914` contrôle l'enveloppe et les plages des IB, **pas tous
  les opcodes PM4 ni les accès produits par leur contenu**. Ce n'est pas une
  interface GPU sûre pour des clients arbitraires non fiables.
- `DescSeg` vers **709** suppose **adresse physique CPU = adresse DMA**, avec
  `kIOMemoryMapperNone`. La configuration IOMMU/DMA de ce PC reste à vérifier.
- Le client singleton ne qualifie pas le partage multi-processus/WindowServer.
  Les courses réelles, fermeture pendant les appels, cohérence CPU/GPU et
  pression mémoire ne sont pas couvertes par des modèles logiciels seuls.
- `notes/design/NATIVE-S1C-ABI.md`, déclaré normatif par le header, est toujours
  **absent du commit public**. Le transport reste à spécifier/qualifier avant RADV.

### A6 — Retour arrière et chargement non prouvés

`stop()` vers **8666** tente le retour console, l'arrêt des moteurs et la
libération des mappings. Cela ne constitue pas une restauration générale de
l'état UEFI. `runStages()` interdit la réinitialisation lorsqu'un marqueur de
provisionnement existe ; les commentaires rapportent des redémarrages lors
d'anciens unload/reload. **Ne pas utiliser le rechargement à chaud comme
procédure de récupération.** Les échecs partiels et le secours doivent être
qualifiés séparément.

La signature ad hoc ne vaut ni approbation macOS, ni résolution des 422 imports
sur Tahoe. Aucune collection noyau système n'a été reconstruite. SIP n'a pas
été désactivé.

### A7 — Avertissements de compilation conservés, pas masqués

Chaque build émet **38 avertissements** : 29 flags `-fapple-kext` ignorés pour
les fichiers C, cinq shadowings, une variable non utilisée, une conversion de
signe et **deux diagnostics de format**. Ces derniers désignent
`src/apple/AppleHardwareHook.cpp:34295–34296` : un argument `"ON"/"OFF"`
est donné à `%llu`, décalant les compteurs et laissant un argument inutilisé.
C'est un défaut confirmé du journal de l'ancienne voie Apple, pas la preuve
d'un crash matériel. Les sources amont n'ont pas été corrigées silencieusement.

## Reproduction locale sans installation

Depuis la racine du fork ; choisir des sorties neuves :

```sh
git clone --no-checkout --filter=blob:none \
  https://github.com/Almosst-DEV/Navi48-MacOS.git out/dependencies/Navi48-MacOS
git -C out/dependencies/Navi48-MacOS checkout --detach \
  696959753070e9a16677be485580dc53b06a7ab5
python3 -B tools/prepare-amd-dependencies.py
python3 -B tools/build-navi48.py --output out/navi48/new-a --jobs 2
python3 -B tools/build-navi48.py --output out/navi48/new-b --jobs 2
python3 -B tools/test-navi48-host.py --output out/navi48/new-host-tests
python3 -B -m unittest discover -s tests/tools -v
```

Ne pas recloner ni relancer le préparateur sur une destination existante.
Pour les mutations, depuis le checkout amont, conserver les logs et prévoir
plus de cinq minutes pour le second script, ou le découper en plages :

```sh
zsh src/navi48-bringup/tests/native_s1c_plant.sh
zsh src/navi48-bringup/tests/native_ws_open_plant.sh 1 51
zsh src/navi48-bringup/tests/native_ws_open_plant.sh 52 150
```

## Suite proposée

1. Tester le retour à l'EFI sauvegardée / un véritable secours, avec l'utilisateur.
2. Extraire un module **strictement PCI-only**, limité à `1002:7550 / 1849:5417`,
   sans activation PCI, mapping MMIO, interruption, firmware ni personnalité Apple.
3. Préparer un paquet natif isolé et traiter les points A1–A6 avant une progression
   matérielle contrôlée ; identifier la console et les conditions DMA d'abord.
4. Qualifier mémoire, commandes, fences et résultats GPU ; ensuite construire
   Mesa/RADV Darwin et rejouer le corpus hors écran. **RADV n'a pas été construit
   par cette session, Metal n'est pas accéléré.**

Preuves structurées : [rapport JSON](2026-10-09-navi48-build-audit.json).
Logs locaux sous `out/navi48/` et diagnostics/sauvegardes privés sous `out/`.
