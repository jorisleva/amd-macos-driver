# Dépendances AMD épinglées et préparation de la compilation Mac

Le manifeste [`sources.lock.json`](../dependencies/sources.lock.json) fixe
désormais **MacKernelSDK et linux-firmware**. Le SDK et les dix firmwares ont
été récupérés sous Windows et vérifiés. Cette préparation ne construit ni
n'installe le kext ; aucune qualification GPU macOS n'en est déduite.

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
par compilation sur notre Mac ; il ne prouve pas le SDK utilisé par l'auteur
pour chacun de ses résultats historiques.

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

## Étape suivante sur le Mac, après le travail AIR en cours

Depuis une copie du dépôt AMD séparée, vérifier et détacher le checkout sur
`696959753070e9a16677be485580dc53b06a7ab5`. Ne pas modifier la copie en cours
d'utilisation pour le travail de shaders. Dans cette copie propre :

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
- Aucun build du kext AMD ni de Mesa Darwin n'a été effectué sur Windows.
- Le chargement des firmwares et les chemins mémoire/commandes seront
  testés seulement après qualification du [démarrage Tahoe de référence](OPENCORE-TAHOE.md).

Preuves détaillées : [rapport de préparation](reports/2026-10-07-boot-preparation.md).
