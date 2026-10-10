# Navi48Native.kext — service PCI/DMA et essai init/compute

**Sources et bundle x86_64 0.2.3 déployé sur PROBE1401**, personnalité PCI, service IOKit et points
kmod. Le service appelle maintenant un chemin expérimental complet jusqu'à deux
calculs gfx1201 internes, avec fences et comparaison des 64 résultats. Aucun
accélérateur annoncé, aucun hook Apple/NVIDIA, aucun `UserClient` autorisé.
**0.2.3 est booté le 10 octobre à 14:02:44 UTC**, module/UUID/arguments
conformes, service retiré, aucun calcul Radeon validé. **Scénario tranché :
mapping partagé déclarant un autre objet** (`RereadMatch=1`,
`DeclaredIsReread=0`) ; provider stable, autres propriétés BAR0 conformes,
BAR2/BAR5 non atteints, `HardwareTouched=0`.
[Boot 0.2.3](../../docs/reports/2026-10-10-native-0.2.3-boot.md).
Pas de Metal/WindowServer ni API pour soumettre librement des programmes.

**Procédure actuelle : [NATIVE-COMPUTE-ESSAI.md](../../docs/NATIVE-COMPUTE-ESSAI.md)** ·
[rapport](../../docs/reports/2026-10-09-native-compute.md).

## Deux chemins distincts, pas des preuves inventées

1. **Socle RO/DMA** : `IOKitController` acquiert BAR0/BAR2/BAR5 en RO/UC, ouvre
   le provider sans seize, compare identité/configuration/descripteurs/mappings
   et console. `start()` prépare 64 Kio de RAM DMA hors gate puis revalide sous
   gate. Le contexte RO reste privé/désactivé et ses blockers ne sont pas effacés.
   Sans opt-in compute, arrêt ici : aucune commande/écriture GPU.
2. **Essai matériel explicitement autorisé** : `ExperimentalCompute` possède
   un autre contexte/type ABI (`n48compute`), mappings RW/UC privés et accès
   bornés dans `ComputeAccess.hpp`. Découverte IP, vérifications MC/origine,
   bootloader PSP, GMC/GART, PSP/ring/TMR/firmwares, SMU/IMU/RLC, CP/MES/GFX,
   queue MES, fences et deux shaders publics. Pas de réutilisation d'un contexte
   RO « débloqué » ou de `Claims` fabriqués. Ce chemin est un essai risqué, pas une
   qualification de réservation VRAM/restauration/puissance/sommeil.

L'identité reste strictement `1002:7550 / 1849:5417 / c0 / 030000`, BAR0
256 Mio, BAR2 2 Mio, BAR5 512 Kio. Console ambiguë, autre accélérateur,
provider occupé, divergence de BAR ou erreur protocole conduisent au refus.
Aucun argument de l'ancien pilote amont n'active ce chemin.

## DMA → GART → commandes, réellement raccordés dans le code

- `DmaBuffer` utilise `IOBufferMemoryDescriptor` et `IODMACommand::kMapped` ;
  pages issues de `gen64IOVMSegments`, jamais de `getPhysicalSegment` CPU.
- `ComputeSysMem` remplace le SysMem amont : alignement/limites/alias validés,
  pages discontiguës conservées, publication avant remise de l'adresse.
- `gmc_bind_existing` construit chaque PTE depuis `sysmem_iovm_page()` ; ne
  dérive pas `first + page*4096`. Aucun allocateur high VRAM hors scratch.
- Rings/fences : vue CPU originale, sortie DMA 64 bits empêchant le bounce
  lié à la largeur d'adresse, pages néanmoins sous 48 bits, rejet si le
  descripteur DMA diffère de l'original. Fences nécessaires avant comparaison.
- Les anciennes opérations `read/write/syncForDevice/syncForCpu` restent
  disponibles pour les buffers à bounce ; synchroniser n'est pas une fence.

L'allocation/revalidation et les écritures GPU ne prouvent pas que la DMA marche.
Seuls les vrais résultats et fences Radeon après le boot pourront le démontrer.

## Durée de vie/annulation

Préparation RAM et orchestration bloquante hors `IOCommandGate`. Le service,
provider, loop et gate restent retenus ; stop pendant la préparation annule et
attend la finalisation pour nettoyer DMA avant fermeture PCI. Avant hardware,
les mappings RW/ressources/PM temporaires sont nettoyés hors gate sur échec.

**Après la première opération hardware : conservation terminale jusqu'au
reboot**, même après timeout/erreur/annulation/échec de publication IORegistry.
Le propriétaire garde le module, contrôleur/bail PCI, mappings et buffers
publiés. Pas de quiescement inventé, de réarmement ou d'unload à chaud.
L'assertion PM évite la veille automatique/de l'affichage, pas la veille forcée.
Ne pas mettre la session d'essai en veille.

`Navi48Native,Resources` schéma 2 décrit le socle RO/DMA non qualifié.
`Navi48Native,Compute` schéma 1 décrit les étapes/erreurs réellement observées,
fences et premiers résultats de chaque shader. La réussite exige deux fences,
deux tests IB et 64 comparaisons sans erreur ; `HardwareTouched` seul ne suffit
pas. Qualification globale et Metal restent zéro, même après un calcul passé.

## Diagnostic de refus conservé (0.2.1)

`Navi48Native,BootDiagnostics`, schéma 1, est publié dans IOResources hors
command gate, uniquement avec des objets valeur génériques. Aucune référence
au service/provider/mapping/DMA retenue par le rapport. Il est destiné à
survivre au refus de start/détachement/free et au buffer dmesg écrasé. Best
effort en cas d'OOM ; dernière publication, pas historique. Persistance testée
sur doubles IOKit, pas encore observée sur le vrai noyau. Aucun garde contourné,
aucun hardware/résultat inventé. Lire `PlatformObserved`, `DMAObserved` et
`ComputeObserved` avant d'interpréter leurs champs. Checkpoint, décision
plateforme/BAR, IOReturn DMA et `PreflightCheck` localisent le refus.
[Décodage](../../docs/reports/2026-10-10-native-boot-diagnostics.md).

## Activation explicite

Le socle exige `navi48-native-platform=1`, scratch offset aligné 64 Kio et
size multiple 4 Kio/minimum 24 Mio. Compute exige en plus :

```text
navi48-native-compute=1 navi48-native-risk=1
```

Le scratch compute doit être aligné 1 Mio, au moins 32 Mio ; le profil revu
utilise offset **64 Mio**, taille **64 Mio**. Il sélectionne une zone de l'essai,
**pas une réservation VRAM prouvée**. Aucun placement implicite. Le profil USB
conserve sécurité, SMBIOS, CPU patches et les cinq kexts de référence.

## Compilation reproductible, sans installation/chargement

```sh
python3 -B tools/build-native-kext.py \
  --core-build out/native-platform/service-dma \
  --output out/native-kext/<repertoire-neuf>
python3 -B -m unittest discover -s tests/tools -v
```

Bundle déployé : `out/native-kext/descriptor-identity-0.2.3/Navi48Native.kext`.
Anciens builds conservés dans les backups EFI de chaque remplacement.
Le builder vérifie core/controller/source/SDK, exporte la révision Navi48
épinglée et applique la correction PSP fail-closed déjà auditée. Les moteurs
sont renommés à la compilation, sans service/hook graphique amont. Dix
firmwares et deux bytecodes publics sont vérifiés dans le Mach-O signé.
Tout warning ou import non revu fait échouer le build.

1 290 contrôles service/lifecycle, 162 DMA, 29 pool IOVM et 39 accès RAM, tous
ASan/UBSan, plus 86 tests Python (dont les remplacements EFI stricts). **Les doubles ne fabriquent jamais une preuve
positive de calcul Radeon.** Les notices amont/firmware restent dans le bundle.
Les exports EFI privés et sorties de compilation restent hors Git.

Historique conservé : [0.1.0](../../docs/reports/2026-10-09-native-kext.md),
[DMA 0.1.1](../../docs/reports/2026-10-09-native-dma.md),
[essai PCI/DMA 0.1.2](../../docs/reports/2026-10-09-native-load-preparation.md),
[contrôleur RO](../../native/Navi48FirmwareCore/IOKIT-CONTROLLER.md).
