# PROBE1401 mise à jour en 0.2.2 — diagnostic BAR0 au champ près

## État réellement vérifié

À la demande de l'utilisateur, **Navi48Native 0.2.2 est déployé sur PROBE1401**.
Préparation à **2026-10-10T12:52:24.803495+00:00**, vérification finale à
**2026-10-10T12:55:21.146935+00:00**. Signature ad hoc stricte et `ocvalidate`
réussis ; **112 fichiers EFI USB** conformes au manifeste et au staging local.

**Le noyau courant reste en 0.2.1**, zéro instance native, même boot
12:31:43 UTC. **0.2.2 n'est pas encore chargé ni testé au démarrage** ; son
diagnostic détaillé et l'initialisation/calcul Radeon restent à observer. Le
refus connu est toujours **`InvalidMap` sur BAR0 (`0x10`) du boot 0.2.1**,
sans hardware/DMA/compute. Aucun chiffre matériel nouveau ni `FailedStage` /
`HardwareTouched` n'est supposé.

## Ce que 0.2.2 change, sans assouplir les gardes

Le contrôleur conserve exactement les mêmes conditions de refus. `checkMaps()`
attribue désormais chaque divergence à un `MapCheck` stable et enregistre les
valeurs observées (adresse, options, longueur, contiguïté, physique, descripteur,
tâche) comme simples nombres, sans les déréférencer ni retenir de mapping.

Le diagnostic persistant ajoute `BAR0Map`, `BAR2Map`, `BAR5Map` :

| MapCheck | Champ rejeté |
|---:|---|
| 0 | BAR jamais atteint par `checkMaps()` |
| 1 | `Ok`, toutes les propriétés conformes |
| 2 | `NullMap`, aucun objet mapping |
| 3 | `DescriptorMismatch`, descripteur différent |
| 4 | `TaskMismatch`, tâche autre que noyau |
| 5 | `LengthMismatch` |
| 6 | `ContiguousMismatch`, segment physique |
| 7 | `PhysicalMismatch`, base physique |
| 8 | `CacheMismatch`, cache non `InhibitCache` |
| 9 | `FlagsMismatch`, `Unique`/`ReadOnly` manquants |
| 10 | `NullAddress`, adresse virtuelle nulle |
| 11 | `UnalignedAddress`, non alignée 4 Kio |
| 12 | `RangeOverflow`, intervalle hors espace |
| 13 | `AddressChanged`, adresse divergée entre deux lectures |

Les BAR avant l'échec sont `Ok`, celui de l'échec porte son ID, les suivants
restent `NotChecked`. Le prochain boot doit donc nommer la propriété BAR0
exacte au lieu d'un code global. Les 12 mutations logicielles du test
contrôleur correspondent une à une à ces IDs ; le test service vérifie les
trois BAR, y compris `AddressChanged` et les BAR non atteints.

## Remplacement strict et sauvegardes

Le préparateur accepte désormais `--replace-native-0.2.1` en plus de
l'ancienne option 0.2.0, mutuellement exclusives. Chaque source n'autorise que
son successeur revu (`0.2.0 → 0.2.1`, `0.2.1 → 0.2.2`), avec binaire épinglé,
profil compute 64+64 Mio et double opt-in exacts. L'exception de copie hors
ligne exige le module/version/UUID exact du boot retiré, aucun autre module
expérimental, aucun nœud natif et compteur zéro. Elle ne prouve pas que le
hardware est intact et n'autorise aucun unload/retry/GPU/reboot.

**86 tests Python** passent, dont les deux chaînes de remplacement, le refus
d'une source croisée et la vérification du binaire épinglé. Contrôles C++ :
**2 731** contrôleur (+460), **1 041** service/lifecycle (+622), 162 DMA,
29 pool IOVM, 39 accès RAM, sous ASan/UBSan ; zéro échec. Zéro warning, dix
firmwares/deux shaders vérifiés. 324 noms d'import présents dans BootKC :
audit de noms seulement, pas qualification ABI/chargement de 0.2.2.

Sauvegardes effectivement revérifiées :

| Arbre | Fichiers | Contenu |
|---|---:|---|
| `/Volumes/OPENCORE/EFI` | 101 | référence inchangée |
| `/Volumes/PROBE1401/EFI` | 112 | nouvelle native 0.2.2 |
| `EFI.BACKUP-native-0.2.2-mapping-trial` | 112 | ancienne native 0.2.1 |
| `EFI.BACKUP-native-0.2.1-diagnostics-trial` | 112 | native 0.2.0 conservée |
| `EFI.BACKUP-native-0.2.0-compute-trial` | 112 | native 0.1.2 conservée |
| `EFI.BACKUP-native-0.1.2-trial` | 104 | ancien observateur conservé |

Copie locale de 0.2.1 :
`out/efi-native/native-0.2.2-mapping-trial/previous-trial/EFI` (112 fichiers).
Anciens manifeste/notice archivés et vérifiés, nouveaux manifeste/notice actifs
conformes. Les backups ne sont pas des entrées BIOS indépendantes.

## Produit du prochain boot

- Version **0.2.2**, exécutable SHA-256 :
  `46fe9e475150d275dde9986a98591dc5acd92b20ff506b6853e35cfe0a5de62a`.
- **Trois fichiers EFI changés** : `OC/config.plist`, `Info.plist` et exécutable
  de Navi48Native. SMBIOS, sécurité, patches Ryzen, cinq kexts de référence,
  bornes Darwin 25.6.0, scratch et cinq arguments natifs conservés.
- Le journal OpenCore **2026-10-10-123033** du boot 0.2.1 reste sur PROBE1401 ;
  un nouveau log sera ajouté au prochain démarrage.

## Prochain boot physique, pas de reload

1. Enregistrer le travail puis **F12 → PROBE1401 UEFI → Tahoe installé**.
2. Ne pas mettre en veille, ne pas décharger/recharger le pilote.
3. Au retour, relever version, diagnostic persistant et service :

   ```sh
   kmutil showloaded | grep -i Navi48Native
   ioreg -r -c IOResources -l -w 0 | grep 'Navi48Native,BootDiagnostics'
   ioreg -r -c Navi48Native -l -w 0
   ```

   Ces lectures ne soumettent pas de commande GPU. Si `InvalidMap` revient,
   relever `BAR0Map/MapCheck` et ses valeurs avant toute autre hypothèse.
   Lire d'abord `PlatformObserved`/`DMAObserved`/`ComputeObserved`.
4. Si écran noir/panic/blocage : photographier, arrêt complet si nécessaire,
   puis **F12 → OPENCORE**. Ce retour n'est pas une récupération GPU qualifiée.

Les étapes matérielles 1/2 exigent toujours les preuves du
[protocole](../NATIVE-COMPUTE-ESSAI.md) : init/firmware, deux fences/tests IB,
64 comparaisons et résultat conforme. Signature, EFI et chargement seuls ne
suffisent pas. Metal/WindowServer et soumission libre ne sont pas raccordés.

## Preuves privées et empreintes

Répertoire : `out/efi-native/native-0.2.2-mapping-trial/` ; EFI, SMBIOS,
relevés kernel/IORegistry et backups restent hors Git.

- Préparation :
  `069e086531a0ccc944218326c8f63330d86110640589b040cdf6a921b77e6482`.
- Manifeste nouvelle EFI :
  `22deb89870b9b3a7f8c54a3fba82c0458c57ca77188fb6bab623ea145a474e93`.
- Vérification finale :
  `814ba5e4bda281eb714b936614f32f7813fe83fcfb4546a0aca1d88df007c102`.

Aucune installation système, modification SIP/NVRAM, recharge, commande GPU
ou redémarrage effectué par l'agent.
