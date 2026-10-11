# PROBE1401 mise à jour en 0.2.12 — témoins GC avant/après kick

## État réellement vérifié

À la demande explicite de l'utilisateur (« force » déjà accordé pour cette
chaîne), **Navi48Native 0.2.12 est déployé sur PROBE1401 depuis le boot 0.2.11
conservé** (1 instance, GPU tenu). Préparation à
**2026-10-11T12:20:21.376923+00:00**, vérification finale à
**2026-10-11T12:23:12.355226+00:00**. Signature ad hoc stricte et `ocvalidate`
réussis ; **112 fichiers EFI USB** conformes au manifeste et au staging local.

**Transparence totale :** copie forcée (`retained-forced`, 1 instance pendant
la copie). Le kext chargé ne relit jamais les fichiers USB. Vérifications
identiques ; seule « zéro instance » est levée, à la demande explicite, et
enregistrée comme telle. Aucun hot-load, aucune commande au pilote en mémoire.

**0.2.12 n'est pas encore chargé ni testé au démarrage** ; les témoins GC et
la suite restent à observer. Point de départ : **boot 0.2.11, kick tue tout
le MMIO (BAR2 + GC, 3/3), shell snapshot/survey répondent**. Aucun chiffre
matériel nouveau ni `FailedStage` / `HardwareTouched` n'est supposé.

## Ce que 0.2.12 apporte : paires avant/après kick

Avant le kick : `GrbmStatusBefore` (attendu `0x382c`), `GfxIndexBefore`
(attendu `0xE0000000`) — déjà `GcCpMeCntl`/`GcCpMecCntl`/`GCRb0Rptr`.
Après le kick : `GrbmStatusAfter`, `GfxIndexAfter`, `MeCntlAfter`,
`MecCntlAfter`, `RptrAfter`. Lecture :

| Témoins après kick | Interprétation |
|---|---|
| GRBM/GFX vivants, SCRATCH mort | seul UCONFIG+doorbell touché |
| tout mort (`0xFFFFFFFF` partout) | tout le GC bascule |

Mêmes appels, mêmes attentes, aucune écriture ajoutée. Shell lecture seule
conservé (triple opt-in, fix `request.result` inclus).

## Remplacement et sauvegardes

Remplacement 0.2.11 → 0.2.12 avec binaire épinglé, profil compute 64+64 Mio +
shell, triple opt-in exacts. Ancienne EFI 0.2.11 sauvegardée et vérifiée (USB +
local).

Contrôles C++ : **3 207** contrôleur, **1 335** service/lifecycle, 162 DMA,
29 pool IOVM, 39 accès RAM, sous ASan/UBSan ; zéro échec. **88 tests Python**,
zéro échec. Zéro warning, dix firmwares/deux shaders vérifiés. 325 noms
d'import présents dans BootKC : audit de noms seulement, pas qualification
ABI/chargement de 0.2.12.

Sauvegardes effectivement revérifiées :

| Arbre | Fichiers | Contenu |
|---|---:|---|
| `/Volumes/OPENCORE/EFI` | 101 | référence inchangée |
| `/Volumes/PROBE1401/EFI` | 112 | nouvelle native 0.2.12 (+témoins) |
| `EFI.BACKUP-native-0.2.12-gc-witness-trial` | 112 | ancienne native 0.2.11 |
| `EFI.BACKUP-native-0.2.11-shellfix-trial` | 112 | native 0.2.10 conservée |
| versions antérieures | 112/104 | 0.2.9 → 0.1.2 + observateur conservés |

Copie locale de 0.2.11 :
`out/efi-native/native-0.2.12-gc-witness-trial/previous-trial/EFI` (112 fichiers).
Anciens manifeste/notice archivés et vérifiés, nouveaux manifeste/notice actifs
conformes. Les backups ne sont pas des entrées BIOS indépendantes.

## Produit du prochain boot

- Version **0.2.12**, exécutable SHA-256 :
  `6461c2d9a168195c4e9298a191cbeb24028b7c27e466388c4fb4af710d2f3da8`.
- **Six arguments natifs** (dont `navi48-native-shell=1`), vérifié dans l'EFI.
- **Trois fichiers EFI changés** : `OC/config.plist`, `Info.plist` et exécutable
  de Navi48Native. Tout le reste conservé.

## Prochain boot physique, pas de reload

1. Enregistrer le travail puis **F12 → PROBE1401 UEFI → Tahoe installé**.
2. Ne pas mettre en veille, ne pas décharger/recharger le pilote.
3. Au retour, relever version, diagnostic, service, rapports **et shell** :

   ```sh
   kmutil showloaded | grep -i Navi48Native
   ioreg -r -c IOResources -l -w 0 | grep 'Navi48Native,BootDiagnostics'
   ioreg -r -c Navi48Native -l -w 0
   /tmp/n48-shell-cli snapshot survey
   ```

   Si stage 16 : relever témoins avant/après (GRBM/GFX/ME/MEC/RPTR) avant
   toute hypothèse. Puis interagir via le shell (lecture seule).
4. Si écran noir/panic/blocage : photographier, arrêt complet si nécessaire,
   puis **F12 → OPENCORE**. Ce retour n'est pas une récupération GPU qualifiée.

Les étapes matérielles 1/2 exigent toujours les preuves du
[protocole](../NATIVE-COMPUTE-ESSAI.md) : init/firmware, deux fences/tests IB,
64 comparaisons et résultat conforme. Signature, EFI et chargement seuls ne
suffisent pas. Metal/WindowServer et soumission libre ne sont pas raccordés.

## Preuves privées et empreintes

Répertoire : `out/efi-native/native-0.2.12-gc-witness-trial/` ; EFI, SMBIOS,
relevés kernel/IORegistry et backups restent hors Git.

- Préparation :
  `e0c22de878e8d86b7c4f0a12d99474b196bb1b8574e4959a03392eb66425d3e6`.
- Manifeste nouvelle EFI :
  `18f712b963ce76fadfbdd50c19f32099cc014ef5ac9b11024a2c7a687c5ebc1a`.
- Vérification finale :
  `c25eedf6688b2ad9c448f092f48759213b88147b30c5c729e40a82eda90f25b2`.

Aucune installation système, modification SIP/NVRAM, recharge, commande GPU
ou redémarrage effectué par l'agent. Le pilote 0.2.11 en mémoire n'a été ni
perturbé ni commandé pendant la copie.
