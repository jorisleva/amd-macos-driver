# Chargement du pilote natif depuis PROBE1401 — historique 0.1.2

> **Profil remplacé, sans boot 0.1.2 :** PROBE1401 contient désormais 0.2.0 avec
> initialisation et calcul explicitement activés. Utiliser
> [NATIVE-COMPUTE-ESSAI.md](NATIVE-COMPUTE-ESSAI.md), pas les attentes RO/DMA de
> cette procédure historique. L'EFI 0.1.2 est conservée dans
> `EFI.BACKUP-native-0.2.0-compute-trial` et localement.

**9 octobre 2026 — EFI d'essai réellement déployée, démarrage pas encore effectué.**
La clé `PROBE1401` contient `Navi48Native.kext` **0.1.2**, activé dans
`Kernel/Add`. Ce n'est pas `Navi48PciProbe`, ni le pilote graphique amont
complet. Le disque `OPENCORE` reste la référence intacte.

## Ce que ce premier boot doit établir

1. OpenCore injecte le bundle natif dans le noyau Tahoe.
2. Le module 0.1.2 est réellement chargé, avec l'UUID du binaire contrôlé.
3. Son service acquiert le bail PCI et les mappings RO BAR0/BAR2/BAR5.
4. Il prépare **64 Kio de RAM via IOBufferMemoryDescriptor/IODMACommand**,
   génère les pages IOVM puis revalide le provider.

**Ni commande GPU, ni firmware envoyé, ni périphérique Metal attendu.**
L'initialisation/les moteurs GPU ne sont pas encore orchestrés par le service.
Un chargement réussi ne termine donc pas les étapes matérielles 1 et 2.
Un défaut de kext peut toutefois provoquer un blocage ou un panic.

## Redémarrer sur l'essai

1. Enregistrer les applications ouvertes, laisser `OPENCORE` et `PROBE1401`
   accessibles, puis redémarrer normalement.
2. Au menu **F12** de la Gigabyte, sélectionner **PROBE1401 en UEFI**.
3. Dans OpenCore, choisir le **Tahoe installé**, pas la récupération.
4. Après arrivée au bureau, reprendre la session de développement pour relever
   le chargement et les ressources réels. Ne pas installer de kext système.

La racine amorçable est bien `/Volumes/PROBE1401/EFI`, pas un sous-dossier de
profils. L'ancienne EFI est conservée sur la clé sous
`EFI.BACKUP-native-0.1.2-trial`, et localement dans
`out/efi-native/native-0.1.2-trial/previous-trial/EFI`.
**Ce dossier de sauvegarde n'est pas une entrée supplémentaire du menu BIOS.**

Le contrôle `sudo -n true` demande toujours un mot de passe ; aucun redémarrage
administrateur ni chargement à chaud n'a été effectué par l'agent.
Le boot via OpenCore ne nécessite pas de désactiver SIP. Ne pas lancer
`kextload`, modifier `csr-active-config`, effectuer Reset NVRAM ou changer
l'ordre de démarrage par défaut pour cet essai.

## Vérifier le vrai noyau après le boot

```sh
sw_vers
uname -r
sysctl -n kern.bootargs
kmutil showloaded | grep -F com.amd-macos-driver.Navi48Native
ioreg -r -c Navi48Native -l -w 0
```

Attendu :

- Tahoe **25G241**, Darwin **25.6.0**, x86_64 ;
- `navi48-native-platform=1`, `navi48-native-scratch-offset=0x4000000`,
  `navi48-native-scratch-bytes=0x1800000` ;
- module **0.1.2**, UUID **`E54A318B-F675-3DF6-A4A5-4D25332E6A27`** ;
- `Navi48Native,Resources`, `SchemaVersion=2`, `MappingsHeld=1` ;
- `DMAAllocatorInvoked=1`, `DMAPhase=2` (Prepared), `DMAResult=0`,
  `DMABytes=65536`, `DMAPages=16` ;
- `DMAAddressPublished=0`, `GPUDMAValidated=0`, `AccessEnabled=0`,
  `FirmwareExecuted=0`, `GPUInitialized=0` ;
- les blocages matériels restent présents, éventuellement augmentés du blocage
  console. `DMADeviceMapper=0` désigne le mapper IOKit par défaut, **pas** une
  preuve de DMA identité.

Le candidat BAR0 à 64 Mio / 24 Mio est **uniquement un candidat de placement
pour les observations RO**. Il ne réserve aucune VRAM, n'autorise aucune
écriture GPU et est indépendant des 64 Kio de RAM DMA préparés.

Conserver le nouveau journal `opencore-*.txt` à la racine du support réellement
utilisé. Une injection annoncée réussie dans ce journal ne prouve pas, seule,
le chargement ou l'exécution de `start()`. Si le service refuse un provider
occupé, une configuration différente ou un échec DMA, conserver le diagnostic
et ne pas contourner ce refus. L'absence dans `showloaded` peut également
résulter d'un déchargement après refus : ne pas inventer une validation.

## En cas de blocage, écran noir ou panic

Photographier les dernières lignes et noter le support utilisé. Redémarrer
avec **F12 → OPENCORE → Tahoe installé**. Cette référence n'a pas été modifiée
et ne contient pas le module natif. Conserver le log OpenCore de PROBE1401 et
le rapport de panic s'il existe. Le secours après panne reste à qualifier ;
sa présence ne constitue pas à elle seule un essai réussi de restauration.

## Artefacts et reproduction

Bundle contrôlé : `out/native-kext/kernel-trial/Navi48Native.kext`.
Préparation/déploiement : `out/efi-native/native-0.1.2-trial/` ; empreintes
complètes dans `native-hashes.json`, anciennes EFI dans les copies vérifiées.
**Les config.plist et paquets EFI contiennent le SMBIOS privé : ne pas publier.**

Pour une nouvelle préparation locale, sans remplacer une EFI ni redémarrer :

```sh
python3 -B tools/prepare-native-kext-efi.py \
  --build out/native-kext/kernel-trial \
  --output out/efi-native/native-essai-neuf \
  --candidate-offset 0x4000000 --candidate-bytes 0x1800000
```

`--deploy-probe1401` ajoute un remplacement de l'EFI entière **sur cette seule
clé identifiée**, avec staging, sauvegarde, vérifications et rollback en cas
d'échec. L'outil refuse un essai déjà remplacé ou une configuration inattendue ;
il ne reformate pas et ne fusionne pas les EFI. Il ne touche jamais à l'EFI
`OPENCORE`, aux collections noyau système ou à la sécurité.

[Rapport de préparation](reports/2026-10-09-native-load-preparation.md) ·
[Service natif](../kexts/Navi48Native/)
