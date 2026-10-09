# Isolation du module PCI — Navi48PciProbe 0.1.0

**9 octobre 2026 UTC, sur le Hackintosh Tahoe 26.7.1 / 25G241.**
À la demande de l'utilisateur, le test de la copie de secours identique à
l'EFI actuelle est différé et le travail logiciel d'isolation est poursuivi.
La sauvegarde reste vérifiée ; **aucun essai de récupération n'est déclaré
réussi par équivalence**.

## Résultat

Un nouveau bundle indépendant, **Navi48PciProbe.kext**, est développé dans
[`kexts/Navi48PciProbe/`](../../kexts/Navi48PciProbe/). Il ne reprend pas le
`start()` de Navi48Bringup : les chemins PSP, VRAM, interruptions, natifs,
Apple, Metal et NVIDIA sont **absents de ses unités de compilation**.
Navi48-MacOS amont n'est pas modifié.

**Deux constructions x86_64 sans avertissement, 1 048 contrôles logiciels
sous ASan/UBSan, 25 mutations détectées et 31 tests Python réussis.** Les deux
bundles retenus sont identiques fichier par fichier, exécutables signés inclus.
**Aucun kext installé ou chargé.** Nos outils n'écrivent pas dans l'EFI et
n'ont pas modifié les arguments de boot ou SIP. Une modification distincte
du fichier OpenCore est détectée en fin de travail, détaillée ci-dessous.
Ce module n'apporte aucune accélération graphique.

## Périmètre observable, pas initialisation matérielle

| Point | Choix effectif |
| --- | --- |
| Identité | `com.amd-macos-driver.Navi48PciProbe`, version `0.1.0` |
| Personnalités | Une seule, classe/catégorie `Navi48PciProbe` |
| Cible | `1002:7550 / 1849:5417`, révision `c0`, classe `030000` |
| Activation | Absente par défaut ; seul `navi48-pci-probe=1` admet le module |
| Provider | Type IOPCIDevice vérifié ; **seulement `copyProperty()`** |
| Données | Six identifiants `OSData` et `assigned-addresses` déjà publiés |
| Accès PCI directs | Aucun, pas même `configRead32` |
| MMIO/VRAM/DMA/interruptions | Aucun mapping, allocation DMA ou accès GPU |
| Publication | Un dictionnaire sur le nœud propre du module ; aucune modification du provider |
| Service utilisateur | Les deux `newUserClient()` refusent ; pas de `registerService()` |
| Arrêt | Retrait du dictionnaire local ; aucune restauration GPU à tenter |

Les filtres du plist ne remplacent pas le contrôle dans le code. `probe()` et
`start()` relisent l'activation et l'identité. Les autres valeurs du boot-arg,
la R9700 `7551`, un autre sous-système, une autre révision ou classe sont refusés.
Le module ne se déclare ni framebuffer ni accélérateur, n'ouvre pas la carte
et ne publie pas `LoadAccelerator`.

Le parseur partagé accepte au plus sept enregistrements de vingt octets.
Il refuse types/longueurs incorrects, registres inconnus/dupliqués, BDF
contradictoires, plages nulles/débordantes/chevauchantes et absence des BAR
mémoire 0, 2 ou 5. Il conserve les adresses 64 bits et n'effectue aucune
écriture de dimensionnement PCI. Une entrée refusée ne produit pas de
résultat partiel.

### Rejeu du relevé de cette Radeon

`--live-registry` lit `ioreg` et ne conserve que les sept propriétés pertinentes,
sans numéro de série ni SMBIOS. Ces octets sont injectés dans un **faux provider
IOKit du programme hôte** utilisant le même `.cpp` que le kext. Le rejeu passe.
La fixture publique est dans `tests/pci-probe/target-registry.json`.

| Ressource publiée par macOS | Base | Longueur |
| --- | --- | --- |
| BAR0 | `0x440000000` | 256 Mio |
| BAR2 | `0x450000000` | 2 Mio |
| BAR4 (I/O) | `0xe000` | 256 octets |
| BAR5 | `0xfcb00000` | 512 Kio |
| ROM | `0xfcb80000` | 128 Kio |

Le code d'espace 2 utilisé par IOPCIFamily pour BAR0/BAR2 n'a pas fait perdre
leurs bits supérieurs à 4 Gio. Il ne doit pas être interprété ici comme une
preuve de largeur matérielle du BAR.

**Ce rejeu ne prouve pas que le kext s'attache dans le noyau**, ni que les
adresses sont accessibles ou les tailles matériellement correctes. Il ne
valide pas la VRAM, le DMA, le chargement firmware ou un calcul GPU.

## Contrôles exécutés

### Même code, faux IOKit volontairement restreint

Le builder compile `Navi48PciProbe.cpp` et `ProbePolicy.hpp` avec les en-têtes
hôtes de `tests/pci-probe/stubs/`. Ceux-ci ne proposent ni configuration PCI,
ni mapping, ouverture du provider, interruptions, gestion d'alimentation ou
enregistrement de service. Ils modélisent uniquement les propriétés,
références, objets de rapport et appels de base nécessaires.

Les **1 048 contrôles** couvrent notamment :

- activation absente/0/1/autres valeurs et zéro lecture du provider si désactivé ;
- rejet de chaque champ d'identité incorrect, manquant, tronqué ou du mauvais type ;
- parseur de ressources et cas limites, avec sortie inchangée en cas d'erreur ;
- revalidation lors de `start()`, après modification de l'identité ou de l'activation ;
- échecs d'allocation/insertion jusqu'au premier parcours complet réussi ;
- absence de fuite de références dans le modèle, publication uniquement locale,
  compensation par `stop()` si l'échec suit un `IOService::start()` réussi ;
- refus des deux interfaces utilisateur, y compris du type N48N ;
- retrait du rapport à l'arrêt et répétition des cycles logiciels.

Compilation avec `-Wall -Wextra -Werror -Wshadow`, ASan/UBSan,
`-fno-sanitize-recover=all`. Ce modèle **ne remplace pas IOKit réel**, ses
allocateurs, sa concurrence ou ses transitions d'état.

### Mutations et garde-fous du paquet

Le contrôle non modifié compile et passe. **25/25 modifications volontairement
incorrectes compilent, puis sont détectées par les tests** ; aucun timeout,
aucun mutant échappé. Elles suppriment notamment un filtre d'identité, le
contrôle d'activation, la revalidation, les bits d'adresse hauts, une limite
ou une libération, publient sur le provider ou autorisent un client.

Le runner part du build `checked-a` ; ses empreintes de sources et de tests
sont identiques à celles des deux builds finaux. Il ne modifie que des copies
sous `out/pci-probe/mutations/`.

Les **31 tests Python** comprennent les 21 régressions précédentes et dix
nouveaux tests du plist, des dépendances, du code de refus, des includes,
des appels interdits et des imports. Des ajouts de personnalité Apple,
élargissements PCI, écritures/lectures PCI directes, publication de service,
client accepté et imports noyau supplémentaires sont rejetés.

## Construction, signature et provenance

- Command Line Tools / Apple Clang `21.0.0 (clang-2100.1.1.101)` ; SDK macOS 26.5.
- MacKernelSDK `05094e5e88cec7caedbfb35e8449ed0db94bf95b`, archive SHA-256 vérifiée
  et réextraite pour chaque build. Aucun firmware ou checkout Navi48 requis.
- ABI de compilation x86_64/macOS 11, deux seules unités : `Navi48PciProbe.cpp`
  et `kmod_info.c`. `-Werror` ; aucun avertissement de compilation/lien relevé.
- Mach-O fin x86_64 `MH_KEXT_BUNDLE`, sans dépendance dylib utilisateur.
- Signature **ad hoc**, vérifiée avec `codesign --verify --strict`.
- **294 imports noyau** : beaucoup proviennent de la vtable héritée d'IOService.
  L'unique import direct IOPCIDevice est `IOPCIDevice::metaClass`, pas une API
  d'accès PCI. La présence de `IOService::mapDeviceMemoryWithIndex` ou
  `registerInterrupt` dans la vtable n'est pas un appel par notre code.
- La liaison avec les collections noyau de 25G241 reste **non qualifiée**.
  Le scan de surface et la liste d'import autorisés sont des garde-fous,
  pas une preuve formelle de tous les effets d'IOKit.

Artefacts retenus, non destinés à une installation automatique :

```text
out/pci-probe/release-a/Navi48PciProbe.kext
out/pci-probe/release-b/Navi48PciProbe.kext
```

SHA-256 identique des deux exécutables signés :

```text
8146414908a073e9fe504ca89b285d3ee076f2e327eb606cecaf023d0cd1ca94
```

Contrairement aux builds Navi48Bringup précédents, ce petit module est compilé
sans informations de debug noyau incorporant des chemins locaux. Les deux
bundles sont ici reproduits **octet par octet**. Les binaires des tests hôtes,
les logs et les répertoires SDK ne sont pas inclus dans cette affirmation.

Le premier essai a été arrêté par le contrôleur d'imports : il attendait
`gMetaClass` au lieu du symbole réel `metaClass` d'IOPCIDevice. La liste a été
corrigée pour accepter **ce seul symbole de type**, puis les builds ont été
refaits depuis des arbres neufs. Aucun appel PCI supplémentaire n'a été autorisé.

## Écart de configuration OpenCore détecté, conservé

À la vérification finale, `OPENCORE/EFI` compte **101 fichiers au lieu de 98**.
Les trois ajouts sont `OC/oldConfig.plist`, `OC/._oldConfig.plist` et
`OC/._config.plist`. Aucun fichier de la sauvegarde ne manque ; parmi eux,
**seul `OC/config.plist` a changé**. Son mtime indique `00:33:26 UTC`.

- Ancien SHA-256, également celui de `oldConfig.plist` :
  `48c33ed18b3f09518d817c0ac8697c482be6a0c08b664bcebbf93ccaa59d21f7`.
- SHA-256 constaté du fichier actuel :
  `e88ce3555003f97fee228e647114b92ae52a4f40038da14c983d97fed2c27487`.
- Diff plist : cinq commentaires de `Kernel/Add` changés, ajout de
  `Misc/Serial/Custom`, suppression de la clé NVRAM `prev-lang:kbd`.
- Aucun changement des entrées kext, de leurs activations, des arguments de
  boot ou de la configuration SIP dans ce diff. La session reste avec SIP
  activé et `navi48bringup=0 rdna4-off=1`, sans le nouveau boot-arg.

Ces changements **ne proviennent pas des outils de ce travail** ; leur origine
n'est pas attribuée. Ils ne sont ni écrasés ni annulés. Les deux sauvegardes
initiales sont revérifiées. L'EFI courante n'est donc plus strictement identique
à ces copies : conserver cette distinction dans tout futur plan de chargement.
Aucun contenu SMBIOS ou commentaire privé n'est publié dans le rapport.

## Reproduction et suite

Commandes et contrat détaillés :
[README du module](../../kexts/Navi48PciProbe/README.md).
Le builder refuse les sorties existantes et celles hors du `out/` du dépôt ;
il ne propose ni installation ni chargement.

Les prochaines vérifications sont un **plan de chargement séparé de ce seul
observateur** : compatibilité des imports, exigences macOS, preuve d'absence
si désactivé, présence du dictionnaire si activé, puis retour à la référence.
Aucun de ces essais noyau n'est marqué réussi par les tests logiciels.
Le pilote Navi48Bringup complet demeure distinct et nécessite encore le
traitement des risques console, DMA/IOMMU et retour arrière de
[l'audit initial](2026-10-09-navi48-build-audit.md).

Preuves structurées : [rapport JSON](2026-10-09-pci-probe-isolation.json).
Les sorties de build/test sont conservées localement sous `out/pci-probe/`.
