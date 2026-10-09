# Dépendances AMD épinglées et préparation de la compilation Mac

Le manifeste [`sources.lock.json`](../dependencies/sources.lock.json) fixe
désormais **MacKernelSDK et linux-firmware**. Le SDK et les dix firmwares ont
été récupérés et vérifiés sous Windows, puis utilisés le 9 octobre pour
**deux builds Navi48 x86_64 sur le Hackintosh Tahoe**. Le préparateur seul
ne construit ni n'installe le kext ; l'outil de build ci-dessous n'installe
rien non plus. Aucune qualification GPU macOS n'en est déduite.
Voir le [rapport de compilation et d'audit](reports/2026-10-09-navi48-build-audit.md).

## Versions retrouvées

| Composant | Révision / preuve |
| --- | --- |
| Navi48-MacOS | `696959753070e9a16677be485580dc53b06a7ab5`, déjà fixé |
| Mesa RADV | `f5cb8ee032adabef599ae892ec4e42d796b84da9`, patches 0001 à 0005 dans l'ordre amont |
| MacKernelSDK | `05094e5e88cec7caedbfb35e8449ed0db94bf95b` : gitlink `MacKernelSDK` de RDNA4FB à `5ff69aade9a7feb18bb665040661de33ccfe68ba` |
| linux-firmware | `5ff473283bf11ba0a8b6b04a075ae01676aee10d` : **dix SHA-256 conformes** aux empreintes Navi48 |

Les [instructions Navi48](https://github.com/Almosst-DEV/Navi48-MacOS/blob/696959753070e9a16677be485580dc53b06a7ab5/BUILDING.txt)
désignent MacKernelSDK dans RDNA4FB sans donner de commit SDK. Le gitlink de
[RDNA4FB](https://github.com/somestupidgirl/RDNA4FB/tree/5ff69aade9a7feb18bb665040661de33ccfe68ba)
permet de fixer une provenance reproductible. Ce choix reste à qualifier
pour le chargement sur notre Tahoe ; les deux compilations réussies ne
prouvent pas le SDK utilisé par l'auteur pour ses résultats historiques.

La dernière révision linux-firmware examinée, `24247053afda2bab6b4ace42402dbf9f3b1305d6`,
ne correspond qu'à **3 des 10 empreintes**. La recherche dans l'historique a
retrouvé une seule révision contenant les dix versions attendues. Le script
du projet refuse toute différence ; il ne remplace pas silencieusement les
versions attendues par les blobs les plus récents.

La notice racine MacKernelSDK est **APSL 2.0**, pas BSD. Les notices propres
aux headers/bibliothèques doivent également rester présentes. La notice
firmware est exactement `LICENSE.amdgpu` ; elle et `WHENCE` sont conservées
avec leurs empreintes. Aucun firmware binaire ni SDK tiers n'est ajouté à Git.

## Préparer sur Windows ou sur le Mac

Python 3, bibliothèque standard uniquement :

```text
python3 tools/prepare-amd-dependencies.py
```

Sous Windows, utiliser `python` à la place de `python3`. Le résultat par
défaut est dans `out/dependencies/amd-pinned/` :

- `sdk-source/MacKernelSDK-05094e5e88cec7caedbfb35e8449ed0db94bf95b/` :
  Headers, Library et notices originaux.
- `linux-firmware/amdgpu/` : les dix blobs strictement vérifiés.
- `linux-firmware/LICENSE.amdgpu`, `WHENCE`, `verification.json` et
  `sources.lock.json` : provenance et preuve de récupération.

Le cache vérifié est dans `out/downloads/amd/`. Le mode `--offline` permet
une reproduction sans réseau ; choisir un nouveau `--output` sous `out/`
pour préserver une préparation existante. Aucune sortie sur une partition
système/EFI ou une clé USB n'est acceptée.

## Première bibliothèque native isolée — non chargeable

Le module [Navi48FirmwareCore](../native/Navi48FirmwareCore/) utilise ces mêmes
entrées épinglées, mais seulement neuf unités amont sélectionnées et auditées.
Il produit un objet et une archive statique, **pas un kext**. Deux builds
identiques et les tests du modèle local sont consignés dans le
[rapport d'isolation native](reports/2026-10-09-native-isolation.md).

```sh
python3 -B tools/build-native-firmware-core.py --output out/native-isolation/nouveau-build
```

Le cache existant est obligatoire ; pas de téléchargement ni installation.
Aucune fonction GPU n'est exécutée. Les préconditions testées ne sont pas
encore raccordées à un contrôleur matériel : ne pas tenter de charger l'archive.

## Observateur PCI indépendant

Le nouveau [Navi48PciProbe](../kexts/Navi48PciProbe/) se construit avec
`python3 -B tools/build-pci-probe.py --output out/pci-probe/new-build`.
Il utilise seulement l'archive MacKernelSDK en cache : aucun checkout Navi48,
firmware ou composant graphique. Le builder teste le même code avec un faux
IOKit, puis produit le kext sans installation. Résultats et limites :
[rapport d'isolation](reports/2026-10-09-pci-probe-isolation.md).

## Construire Navi48 sans l'installer

Python **3.9+** et Command Line Tools suffisent pour ce kext. Depuis la racine
du fork, préparer les entrées puis un checkout AMD séparé et détaché :

```sh
git clone --no-checkout --filter=blob:none \
  https://github.com/Almosst-DEV/Navi48-MacOS.git out/dependencies/Navi48-MacOS
git -C out/dependencies/Navi48-MacOS checkout --detach \
  696959753070e9a16677be485580dc53b06a7ab5
python3 -B tools/prepare-amd-dependencies.py
python3 -B tools/build-navi48.py --output out/navi48/new-build --jobs 2
python3 -B tools/test-navi48-host.py --output out/navi48/new-host-tests
```

Ne pas recloner sur le checkout existant ; choisir de nouvelles sorties.
Le builder exporte le commit propre, réextrait l'archive SDK vérifiée et
contrôle les firmwares jusque dans le Mach-O signé. Il conserve les rapports,
empreintes, sources et notices. Les cinq blobs Apple optionnels doivent être
vides. `make install`, `kmutil load` et les modifications EFI sont exclus.

Deux builds indépendants passent ; leurs sections de code/données sont
identiques, mais les binaires complets diffèrent par les métadonnées de debug
et signature. Les suites natives totalisent 2 905 contrôles ASan/UBSan,
sans commande GPU. L'audit exige une isolation du bundle avant un essai.

### Détail manuel et suite RADV

Le builder automatise les points 1–3 et la reconstruction Navi48 du point 5.
Le travail Mesa n'a pas encore été exécuté. Dans une copie propre au commit
`696959753070e9a16677be485580dc53b06a7ab5` :

1. Rejouer le préparateur du fork pour obtenir le SDK et les firmwares épinglés.
2. Copier les dix blobs vérifiés dans `src/navi48-bringup/firmware/` du dépôt
   AMD ; conserver la liste des empreintes. Son `tools/fetch-firmware.sh`
   peut copier depuis notre répertoire `linux-firmware/`, mais ses simples
   avertissements de hash ne remplacent pas notre vérification stricte.
3. Construire avec `make -C src/navi48-bringup MKSDK=<chemin-absolu-du-SDK> all`.
   Vérifier architecture x86_64, symboles non résolus, dépendances et signature
   ad hoc. Conserver le journal et le SHA-256 du binaire ; aucune installation.
4. Construire Mesa à la révision épinglée avec les cinq patches Darwin de
   Navi48, dans l'ordre de son `mesa-patches/README.txt`, puis vérifier les
   dylibs x86_64 et leur ABI. L'application des patches et ce build ne sont
   pas encore validés ici.
5. Refaire la construction depuis une copie propre avant de préparer un
   artefact d'essai macOS. Ne pas l'ajouter à l'EFI de référence OpenCore.

Les chemins sont des paramètres explicites ; aucun chemin personnel de
l'auteur n'est présumé disponible. Les caches Apple absents ne sont pas
nécessaires à la voie native initiale, selon BUILDING.txt. Cela ne résout
pas les contrats privés requis plus tard pour WindowServer.

## Blocages conservés

- `notes/design/NATIVE-S1C-ABI.md`, déclaré normatif dans le header N48N,
  reste absent à la révision publique épinglée (HTTP 404). L'ABI n'est donc
  pas qualifiée par le seul pinning du SDK et des blobs.
- Navi48 compile sous Tahoe ; aucun chargement ni essai GPU macOS. Les
  imports noyau ne sont pas qualifiés contre les collections de Tahoe.
- Mesa Darwin n'est pas encore construit.
- Le chargement des firmwares et les chemins mémoire/commandes seront
  testés seulement après qualification du [démarrage Tahoe de référence](OPENCORE-TAHOE.md).

Preuves détaillées : [rapport de préparation](reports/2026-10-07-boot-preparation.md).
