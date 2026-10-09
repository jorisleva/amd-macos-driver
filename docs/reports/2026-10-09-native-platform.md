# Contrôleur de ressources IOKit codé — 9 octobre 2026

## Travail livré

Après les [accès et chemins PSP raccordés sur RAM](2026-10-09-native-access.md),
le raccordement aux **API IOKit réelles** est maintenant implémenté dans
[`IOKitController.cpp`](../../native/Navi48FirmwareCore/IOKitController.cpp).
Il est construit dans une archive séparée, **`libNavi48PlatformController.a`**.

**Aucun appel de ce contrôleur n'a été exécuté sur le provider Radeon réel.**
Il n'y a ni IOService enregistrable, personnalité PCI, point d'entrée, kext,
client utilisateur ou nouvelle campagne sur l'observateur. Le firmware core
et le contrôleur sont des bibliothèques non chargeables, pas un pilote activable.

[Contrat détaillé et cycle de vie](../../native/Navi48FirmwareCore/IOKIT-CONTROLLER.md) ·
[En-tête/API](../../native/Navi48FirmwareCore/IOKitController.hpp).

### Acquisition et conservation des mappings

- Opt-in distinct de valeur exactement 1, lu avant tout appel PCI ; aucun
  argument n'est ajouté à l'EFI. Owner attaché au bon IOPCIDevice, provider
  actif et pas déjà ouvert. Bail `open` sans `seize` ; retains conservés.
- Six champs d'identité lus dans la configuration PCI, pas recopiés depuis
  IORegistry. Décodage mémoire déjà actif exigé ; **aucune écriture PCI**, aucun
  changement du bus mastering, de l'alimentation ou des interruptions.
- BAR0/BAR2 lus en 64 bits avec contrôle haut/bas/haut ; BAR5 en 32 bits.
  Profil limité aux fenêtres 256 Mio / 2 Mio / 512 Kio déjà observées, avec
  adresses dynamiques. Pas de sizing destructif, ni de confusion avec la VRAM totale.
- Chaîne contrôlée **adresse BAR → segment CPU physique du descripteur →
  segment du mapping** : mêmes adresses/longueurs, contiguïté complète,
  `kIOMemoryMapperNone`, backing exact, tâche noyau et absence de chevauchement.
- Descripteurs retenus explicitement et mappings RO/UC/uniques conservés.
  Aucun pointeur ou contexte mutable n'est exposé à l'appelant.
- Revalidation de la configuration, de l'attachement/bail, des descripteurs,
  VAs, segments et de la console. Une divergence termine la session.

Le contexte AMD privé contient les VAs obtenues par IOKit, mais reste
**désactivé** : aucune lecture/écriture de BAR, MMIO, VRAM ou doorbell, aucun
appel au binder, au PSP ou à un firmware n'est effectué par le contrôleur.

### Placement console et candidat

`IOPlatformExpert::getConsoleInfo` fournit la base et la géométrie brutes.
Le contrôleur vérifie largeur/profondeur, stride, multiplication, offset,
longueur et containment physique dans BAR0. Il protège l'allocation complète,
pas seulement les pixels visibles ; un chevauchement connu fait échouer
l'acquisition avant mapping.

Une base nulle ou non alignée reste ambiguë. Les bits bas **ne sont pas
masqués en preuve de succès**, contrairement à l'heuristique du pilote amont.
Console absente, géométrie invalide ou adresse hors BAR0 restent bloquantes.
Le candidat est une requête explicite dans l'aperture ; être hors console
ne signifie pas qu'il est libre ou réservé. Aucune adresse MC/DMA n'est déduite
de son adresse CPU physique.

### Propriété, cache et arrêt : pas de faux succès

La réussite s'appelle `MappingsHeldUnqualified`. L'API n'accepte pas de `Claims`
positifs et ne transforme pas un bail IOKit en propriété exclusive du GPU.
Les options de cache demandées et rapportées ne constituent pas une mesure
des PTE/MTRR, ni une preuve HDP/cohérence CPU-GPU.

**Sept familles de preuves restent toujours bloquantes** : propriété GPU
exclusive ; géométrie VRAM/origine BAR0/base MC ; réservation VRAM ; HDP ;
DMA/IOMMU ; restauration ; quiescement. Le masque ne peut jamais devenir nul.
Une console inconnue ajoute son propre blocage ; elle n'autorise aucun accès.

Le nettoyage partiel retire d'abord tous les pointeurs sous verrou, puis
libère les mappings/descripteurs en ordre inverse **hors verrou**, ferme le
bail et rend les retains. Pas de `unmap()` forcé. La fermeture peut réentrer
sans deadlock ou double libération. Les snapshots et le retrait de la ressource
sont sérialisés ; l'appelant doit joindre les utilisateurs avant destruction.

Ce nettoyage concerne exclusivement des ressources **jamais publiées au GPU**.
Il n'est pas une implémentation de l'arrêt des moteurs ou de la quarantaine
d'un GPU actif. Le futur IOService doit encore gérer événements, sommeil,
terminaison et publication/quiescement ; les observations ne sont pas un
verrou global sur la console ou les autres utilisateurs de la carte.

## Vérification concrète, sans matériel

[`controller_test.cpp`](../../tests/native-platform/controller_test.cpp)
compile le **même fichier contrôleur**, avec des doubles IOKit indépendants
de ceux de l'observateur. Les VAs sont des sentinelles non déréférençables :
le banc ne simule ni pixels utilisables ni réponses d'un GPU.

Il vérifie refus d'opt-in/identité/propriété de bail, types et adresses 64 bits,
segments/descripteurs, overflow et chevauchements, options/backing des mappings,
comptage des références, acquisitions partielles, console ambiguë/padding,
revalidation, perte du provider, réentrée de fermeture et snapshots concurrents
avec libération. **2 241 contrôles ASan/UBSan**, zéro échec.

Les régressions restent réussies : **1 582 contrôles modèle/logger** et
**398 contrôles accès/PSP sur RAM**. La réponse PSP de ce dernier banc reste
simulée. **66 tests Python** passent, dont les nouvelles frontières du manifeste,
des dépendances et des symboles du contrôleur. Aucune nouvelle mutation ou
qualification de l'observateur n'est revendiquée.

## Deux builds neufs identiques, zéro avertissement

Sorties retenues : `out/native-platform/final-a/` et `final-b/`.
Les entrées locales ont les mêmes empreintes. Les quatre objets/archives sont
identiques octet par octet. `build-a/` est une itération antérieure, avant le
nettoyage hors verrou et ses tests de réentrée ; ce n'est pas le résultat retenu.

| Artefact | SHA-256 dans les deux builds |
| --- | --- |
| `Navi48PlatformController.o` | `09b6ffd44efe999400e0dbe52d34ded1c8e8bf3a6c4504b7d1dc74f8cb24de96` |
| `libNavi48PlatformController.a` | `7859a0d7cc750d478abc3a8f181a5a94a3ae2cecee428bfcac5b9ee577361c4c` |
| `Navi48FirmwareCore.o` | `0e4d37fa8add2924ab05e867cdc5fcfc5b7254ad46d79ced0d21cf7a37dd9fb3` |
| `libNavi48FirmwareCore.a` | `305e1f9235dbcc8d671a506e469bb4dce57f1a9c41bbab61379633a2fa763f07` |

Le firmware core est **inchangé au niveau binaire** par rapport à l'étape
précédente : mêmes treize imports et dix firmwares vérifiés dans l'objet.
Le contrôleur a quinze imports noyau sur une liste séparée ; aucun symbole
interne non résolu. Les dépendances compilées du contrôleur sont seulement ses
deux fichiers locaux, `AmdGpuAccess.hpp`, `Preflight.hpp`, `NativeLog.hpp`,
`amd/amdgpu_ip.h` et le SDK épinglé. Les deux produits sont des `MH_OBJECT` /
archives, sans initialiseur global, point d'entrée, dylib ou packaging kext.
**Cela ne valide pas leur liaison ou leur exécution avec le noyau Tahoe.**

[Résumé JSON](2026-10-09-native-platform.json). Rapports détaillés, journaux,
objets, tests et manifests EFI avant/après restent sous `out/native-platform/`.
Reproduction locale/offline :

```sh
python3 -B tools/build-native-firmware-core.py --output out/native-platform/essai-neuf
python3 -B -m unittest discover -s tests/tools -v
```

## Limites et prochain travail matériel

L'acquisition IOKit n'est plus un trou logiciel, mais ses observations runtime
n'ont pas été obtenues sur cette machine. Il reste à établir le contrat console
Tahoe, origine/taille/base MC et réservations VRAM, retrait des utilisateurs GPU,
HDP, DMA/IOMMU, arrêt/restauration et cycle de vie du futur IOService.
Ces producteurs de preuves doivent précéder tout passage à des mappings
écrivables ou appel firmware ; pas de conversion automatique des observations
en `Claims` positifs. Ensuite : autres phases PSP/SMU/bootloader, puis moteur
mémoire et commandes compute avec résultats relus.

**OPENCORE : 101 fichiers ; PROBE1401 : 104 fichiers, tous inchangés** par
comparaison des chemins et SHA-256 avant/après. Aucun déploiement, chargement
noyau, firmware envoyé, redémarrage ou réglage de sécurité effectué. Le code
de `Navi48PciProbe` n'est pas modifié.
