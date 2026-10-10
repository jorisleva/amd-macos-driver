# PROBE1401 mise à jour en 0.2.6 — diagnostic RW au champ près

## État réellement vérifié

À la demande de l'utilisateur, **Navi48Native 0.2.6 est déployé sur PROBE1401**.
Préparation à **2026-10-10T15:05:01.534392+00:00**, vérification finale à
**2026-10-10T15:05:51.073055+00:00**. Signature ad hoc stricte et `ocvalidate`
réussis ; **112 fichiers EFI USB** conformes au manifeste et au staging local.

**Le noyau courant reste en 0.2.5**, zéro instance native, même boot
14:54:56 UTC. **0.2.6 n'est pas encore chargé ni testé au démarrage** ; son
diagnostic RW et l'initialisation/calcul Radeon restent à observer. Le refus
connu est toujours **mapping RW BAR0 du boot 0.2.5** (`PreflightCheck=12`,
`BadArgument`), sans hardware. Aucun chiffre matériel nouveau ni `FailedStage`
/ `HardwareTouched` n'est supposé.

## Ce que 0.2.6 change, sans assouplir les gardes

Le préflight RW du compute publie `RWBar0Map`/`RWBar2Map`/`RWBar5Map` au champ
près, dans `BootDiagnostics` (survit au retrait) et dans le rapport Compute
(quand le service survit) : `MapCheck` (0 non atteint, 1 Ok, 2 NullMap,
3 descripteur, 4 tâche, 5 longueur, 6 contigu, 7 physique, 8 cache,
9 flags/ReadOnly présent, 10 adresse nulle, 11 non alignée, 12 descripteur
absent, 13 longueur descripteur), adresse, options, longueur, contigu,
physique, descripteur, tâche, relecture. Mêmes conditions de refus, simplement
attribuées. Le RW exige `ReadOnly` absent (inverse du RO).

## Remplacement strict et sauvegardes

Le préparateur accepte `--replace-native-0.2.5`, exclusif des anciennes
options. Chaîne revue : `0.2.0 → … → 0.2.5 → 0.2.6`, chacune avec binaire
épinglé, profil compute 64+64 Mio et double opt-in exacts. L'exception de copie
hors ligne exige le module/version/UUID exact du boot retiré
(**0.2.5 / 160033C1-04F7-3DE5-A795-A511A749F8F5** ici), aucun autre module
expérimental, aucun nœud natif et compteur zéro. Elle ne prouve pas que le
hardware est intact et n'autorise aucun unload/retry/GPU/reboot.

Contrôles C++ : **3 207** contrôleur, **1 319** service/lifecycle, 162 DMA,
29 pool IOVM, 39 accès RAM, sous ASan/UBSan ; zéro échec. **86 tests Python**,
zéro échec. Zéro warning, dix firmwares/deux shaders vérifiés. 324 noms
d'import présents dans BootKC : audit de noms seulement, pas qualification
ABI/chargement de 0.2.6.

Sauvegardes effectivement revérifiées :

| Arbre | Fichiers | Contenu |
|---|---:|---|
| `/Volumes/OPENCORE/EFI` | 101 | référence inchangée |
| `/Volumes/PROBE1401/EFI` | 112 | nouvelle native 0.2.6 |
| `EFI.BACKUP-native-0.2.6-rw-trial` | 112 | ancienne native 0.2.5 |
| `EFI.BACKUP-native-0.2.5-preflight-trial` | 112 | native 0.2.4 conservée |
| `EFI.BACKUP-native-0.2.4-adoption-trial` | 112 | native 0.2.3 conservée |
| `EFI.BACKUP-native-0.2.3-descriptor-trial` | 112 | native 0.2.2 conservée |
| `EFI.BACKUP-native-0.2.2-mapping-trial` | 112 | native 0.2.1 conservée |
| `EFI.BACKUP-native-0.2.1-diagnostics-trial` | 112 | native 0.2.0 conservée |
| `EFI.BACKUP-native-0.2.0-compute-trial` | 112 | native 0.1.2 conservée |
| `EFI.BACKUP-native-0.1.2-trial` | 104 | ancien observateur conservé |

Copie locale de 0.2.5 :
`out/efi-native/native-0.2.6-rw-trial/previous-trial/EFI` (112 fichiers).
Anciens manifeste/notice archivés et vérifiés, nouveaux manifeste/notice actifs
conformes. Les backups ne sont pas des entrées BIOS indépendantes.

## Produit du prochain boot

- Version **0.2.6**, exécutable SHA-256 :
  `efcf2e8f823c3be7485a536f6819bfe10cf686d243b207bcacb8c7a361da67c0`.
- **Trois fichiers EFI changés** : `OC/config.plist`, `Info.plist` et exécutable
  de Navi48Native. SMBIOS, sécurité, patches Ryzen, cinq kexts de référence,
  bornes Darwin 25.6.0, scratch et cinq arguments natifs conservés.

## Prochain boot physique, pas de reload

1. Enregistrer le travail puis **F12 → PROBE1401 UEFI → Tahoe installé**.
2. Ne pas mettre en veille, ne pas décharger/recharger le pilote.
3. Au retour, relever version, diagnostic persistant, service et rapports :

   ```sh
   kmutil showloaded | grep -i Navi48Native
   ioreg -r -c IOResources -l -w 0 | grep 'Navi48Native,BootDiagnostics'
   ioreg -r -c Navi48Native -l -w 0
   ```

   Ces lectures ne soumettent pas de commande GPU. Si refus RW, relever
   `RWBar0Map/MapCheck` et ses valeurs avant toute hypothèse. Lire d'abord
   `PlatformObserved`/`DMAObserved`/`ComputeObserved`.
4. Si écran noir/panic/blocage : photographier, arrêt complet si nécessaire,
   puis **F12 → OPENCORE**. Ce retour n'est pas une récupération GPU qualifiée.

Les étapes matérielles 1/2 exigent toujours les preuves du
[protocole](../NATIVE-COMPUTE-ESSAI.md) : init/firmware, deux fences/tests IB,
64 comparaisons et résultat conforme. Signature, EFI et chargement seuls ne
suffisent pas. Metal/WindowServer et soumission libre ne sont pas raccordés.

## Preuves privées et empreintes

Répertoire : `out/efi-native/native-0.2.6-rw-trial/` ; EFI, SMBIOS,
relevés kernel/IORegistry et backups restent hors Git.

- Préparation :
  `115e4e9203adce47408d19fa71ebff058e95d8ab204184c0d4f6d4e1d482b0b3`.
- Manifeste nouvelle EFI :
  `db9dfa90b7b09d66738108a07328198cd710bc78367b9d881d80c9a4188fd1e4`.
- Vérification finale :
  `f58217fb44f6857d6b41d6a64e337c619fdfa092637d9907de4530f31b79447f`.

Aucune installation système, modification SIP/NVRAM, recharge, commande GPU
ou redémarrage effectué par l'agent.
