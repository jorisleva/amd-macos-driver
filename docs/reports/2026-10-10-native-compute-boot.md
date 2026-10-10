# Premier boot natif 0.2.0 — module chargé, service retiré, calcul non validé

## Verdict matériel observé

L'utilisateur a démarré le profil de calcul de PROBE1401. **L'injection OpenCore
et le chargement noyau de Navi48Native 0.2.0 sont réellement observés.**
L'UUID chargé est celui du bundle déployé. Les cinq arguments natifs sont reçus.

**Aucun nœud `Navi48Native` ne subsiste, et le compteur d'instances IOKit de sa
classe est zéro.** Aucun rapport `Navi48Native,Resources` / `Navi48Native,Compute`
n'est disponible. Le journal IOPCIFamily montre deux fermetures de bail PCI par
le client Navi48Native pendant le boot : démarrage/acquisition tentés puis
retrait. Cela ne confirme ni une acquisition complète des mappings, ni la
préparation DMA, ni l'exécution de l'orchestration firmware.

**Initialisation GPU, fences et résultats Radeon : non validés. Étapes
matérielles 1/2 : non terminées.** Le code de refus exact reste inconnu : la capture privilégiée ultérieure ne
contient plus les messages du boot ; ne pas attribuer cet échec à PSP, au mapper,
à la console ou à un BAR précis sans le journal correspondant.

## Relevé du vrai noyau

- Boot : **10 octobre 2026, 11:20:06 UTC** (13:20:06 local).
- Capture finalisée : **2026-10-10T11:27:22.891296+00:00**.
- Tahoe **26.7.1 / 25G241**, Darwin **25.6.0**, x86_64.
- `kmutil showloaded` : module **0.2.0**,
  **AB1F0B3A-865C-33FC-BC6B-5CA0056BBF45**.
- Arguments natifs : platform=1, scratch-offset=0x4000000,
  scratch-bytes=0x4000000, compute=1 et risk=1, tous avec préfixe navi48-native-.
- OpenCore : `Prelinked injection Navi48Native.kext ... - Success` et v0.2.0.
- `ioreg -r -c Navi48Native -l -w 0` : sortie vide, code retour zéro.
- `IOKitDiagnostics/Classes/Navi48Native` : **0 instances**.
- Fermetures IOPCIFamily : **11:20:06.662 UTC** et **11:20:13.040 UTC**,
  chacune avec client Navi48Native et `isOpen(forClient) == 1`.

La conservation terminale prévue en 0.2.0 après la première opération hardware
implique normalement une instance native encore retenue. Son absence et les
fermetures PCI sont **compatibles avec un refus pré-hardware**, mais ne
remplacent pas une valeur `HardwareTouched` / `FailedStage` effectivement lue.
Aucune valeur de phase/calcul n'est inventée pour compléter le rapport absent.

## État graphique et provider

La Radeon physique est toujours un vrai `IOPCIDevice` nommé `VGA`, identité
`1002 / 7550 / 1849 / 5417 / c0 / 030000`. Ses enfants actuels sont
`AMDSupport` et `.Display_boot` (`IONDRVFramebuffer`), pas Navi48Native.

- Recherche classe `IOAccelerator` vide ; compteur de cette classe **0**.
- Recherche classe `IOGPU` vide.
- `system_profiler SPDisplaysDataType` : RX identifiée par IDs, affichage de
  base **1920×1080**, framebuffer annoncé 7 Mio, pas de support Metal rapporté.
- Le nœud nommé IOAccelerator sous un faux display de compatibilité est de
  classe `IOServiceCompatibility`, **pas un accélérateur GPU réel**. Ses chaînes
  AGXMetalA12/VRAM ne sont pas une preuve d'initialisation ou de Metal AMD.
- Pas d'assertion PM intitulée Navi48 native compute dans le relevé.

L'erreur `kernelmanager_helper` « Did not find identifier ... in helper »
coexiste avec la notification de chargement et le module effectivement listé.
Elle **n'annule pas le chargement observé** et ne démontre pas la cause du
retrait du service.

## Ce qui empêche le diagnostic exact

La recherche du journal unifié, info/debug inclus, ne restitue pas les lignes
IOLog `start refused`, `DMA/final validation refused` ou `compute FAILED`.
Le buffer noyau brut nécessite une élévation :

- `dmesg` : Operation not permitted ;
- `sudo -n dmesg` : **a password is required**.

Aucune élévation interactive ni changement de sécurité effectué. Demander à
l'utilisateur, **dans le même boot et sans reboot/reload**, de capturer :

```sh
(umask 077; sudo dmesg > /tmp/navi48-native-dmesg.txt)
```

Ne jamais demander le mot de passe dans la conversation. Lire ensuite le
fichier et relever le code/ligne de refus. Si le buffer de boot a été écrasé,
il faudra rendre le diagnostic de refus persistant avant un essai suivant,
plutôt que relancer aveuglément l'initialisation dans cette session.

### Suite après capture utilisateur (même boot)

L'utilisateur a exécuté la capture privilégiée ci-dessus. Le fichier de
**131 071 octets / 135 lignes** commence au milieu d'un message et couvre
742,730521–777,569067 secondes après le boot : **buffer de 128 Kio écrasé,
aucune ligne native**. Code de refus/phase toujours inconnus. Les erreurs
XProtect et IOVersatileHDCPClient restantes ne démontrent pas un défaut natif.

Un correctif **0.2.1** conserve désormais un diagnostic de refus dans
IOResources, sans référence au service/provider ; compilé et testé **hors
ligne, non chargé et non déployé**. Il ne récupère pas ce boot rétroactivement.
[Capture, correctif et procédure suivante](2026-10-10-native-boot-diagnostics.md).
Les preuves originales ci-dessous sont conservées ; le buffer supplémentaire
reste privé dans `dmesg-followup/`, SHA-256
`3ab94bc30321cc89db4e7ad2a7eeb94d4c637cb90bc1c6f1623e68f8388eb199`.

## Préservation et preuves

Vérification uniquement en lecture, aucune commande GPU émise, aucun hot-load,
installation système, modification SIP/NVRAM/sécurité ou reboot.

- **112 fichiers EFI PROBE1401 inchangés**, signature stricte encore valide.
- Exécutable USB SHA-256 :
  `34ce08473ac2acedd0a9094420cbd4ebf159bf48b21a0134095715cfd95809f9`.
- **101 fichiers OPENCORE inchangés** ; backup EFI 0.1.2 intact.
- Sorties brutes privées : `out/native-compute/boot-2026-10-10-112006/`.
  XML PCI/IORegistry et données de session restent hors Git.
- Observation SHA-256 :
  `452affd2b4a139d353cb1b12be743ed1695bc74018fef6a4407a3af896c2512b`.
- Manifeste de preuves SHA-256 :
  `0816cd14e776af02e9c092593881fb018159940ed3ae958b599e8847f9d9ab45`.

[Essai préparé](2026-10-09-native-compute.md) ·
[Procédure](../NATIVE-COMPUTE-ESSAI.md).
