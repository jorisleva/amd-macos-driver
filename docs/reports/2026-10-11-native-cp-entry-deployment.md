# PROBE1401 mise à jour en 0.2.14 — points d'entrée CP et bases IC

## État réellement vérifié

À la demande explicite de l'utilisateur (copie forcée déjà accordée pour cette
chaîne), **Navi48Native 0.2.14 est déployé sur PROBE1401 depuis le boot 0.2.13
conservé** (1 instance, GPU tenu). Préparation à
**2026-10-11T13:22:38.899956+00:00**, vérification finale à
**2026-10-11T13:23:40.458052+00:00**. Signature ad hoc stricte et `ocvalidate`
réussis ; **112 fichiers EFI USB** conformes au manifeste et au staging local.

**Transparence totale :** copie forcée (`retained-forced`, 1 instance pendant
la copie). Le kext chargé ne relit jamais les fichiers USB. Vérifications
identiques ; seule « zéro instance » est levée, à la demande explicite, et
enregistrée comme telle. Aucun hot-load, aucune commande au pilote en mémoire.

**0.2.14 n'est pas encore chargé ni testé au démarrage** ; les points d'entrée
et la suite restent à observer. Point de départ : **boot 0.2.13, aucun reset
collé, CP démarré mais sans fetch, kill global au kick**. Aucun chiffre
matériel nouveau ni `FailedStage` / `HardwareTouched` n'est supposé.

## Ce que 0.2.14 change : nommer le fetch raté

Après `cp_gfx_start` et avant toute écriture du ring-test, 14 lectures pures :
`PfpPcStart/MePcStart/MecPcStart(+Hi)`, `PfpIcBase/MeIcBase(Lo/Hi)`,
`Rb0Base(+Hi)/Rb0Cntl/RbActive`. Lecture du prochain boot :

| Observation | Conclusion |
|---|---|
| IC base ou PC à zéro | fetch impossible, fetch « page 0 » comme en 0.0.14 |
| RB base fausse / inactive | ring illisible par le CP |
| Tout programmé et cohérent | le coupable est le contenu du ring ou le GART |

Mêmes appels, aucune écriture ajoutée. Shell lecture seule conservé.

## Correction 0.2.12 : le reset n'a jamais été collé

Le boot 0.2.13 prouve `MeCntlAfterStart=0x0100A000` == avant kick == preuve
privée 0.2.12 (`16818176`), bit 20 = 0 partout. La transcription `0x100A00`
avait perdu un zéro. Le ME/MEC démarrent correctement ; l'hypothèse est
réfutée. [Boot 0.2.13](2026-10-11-native-0.2.13-boot.md).

## Remplacement et sauvegardes

Remplacement 0.2.13 → 0.2.14 avec binaire épinglé, profil compute 64+64 Mio +
shell, triple opt-in exacts. Ancienne EFI 0.2.13 sauvegardée et vérifiée (USB +
local).

Contrôles C++ : **3 207** contrôleur, **1 335** service/lifecycle, 162 DMA,
29 pool IOVM, 39 accès RAM, sous ASan/UBSan ; zéro échec. **88 tests Python**,
zéro échec. Zéro warning, dix firmwares/deux shaders vérifiés. 325 noms
d'import présents dans BootKC : audit de noms seulement, pas qualification
ABI/chargement de 0.2.14.

Sauvegardes effectivement revérifiées :

| Arbre | Fichiers | Contenu |
|---|---:|---|
| `/Volumes/OPENCORE/EFI` | 101 | référence inchangée |
| `/Volumes/PROBE1401/EFI` | 112 | nouvelle native 0.2.14 (+entrées CP) |
| `EFI.BACKUP-native-0.2.14-cp-entry-trial` | 112 | ancienne native 0.2.13 |
| `EFI.BACKUP-native-0.2.13-me-reset-trial` | 112 | native 0.2.12 conservée |
| versions antérieures | 112/104 | 0.2.11 → 0.1.2 + observateur conservés |

Copie locale de 0.2.13 :
`out/efi-native/native-0.2.14-cp-entry-trial/previous-trial/EFI` (112 fichiers).
Anciens manifeste/notice archivés et vérifiés, nouveaux manifeste/notice actifs
conformes. Les backups ne sont pas des entrées BIOS indépendantes.

## Produit du prochain boot

- Version **0.2.14**, exécutable SHA-256 :
  `55b94d91cc6a56ae1636411171f1b2cdd1f7a358a68458e9089b46379ccf010e`.
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

   Lire `PfpPcStart/MePcStart/MecPcStart`, `PfpIcBase/MeIcBase`,
   `Rb0Base/Rb0Cntl/RbActive` avant toute hypothèse. Puis interagir via le
   shell (lecture seule).
4. Si écran noir/panic/blocage : photographier, arrêt complet si nécessaire,
   puis **F12 → OPENCORE**. Ce retour n'est pas une récupération GPU qualifiée.

Les étapes matérielles 1/2 exigent toujours les preuves du
[protocole](../NATIVE-COMPUTE-ESSAI.md) : init/firmware, deux fences/tests IB,
64 comparaisons et résultat conforme. Signature, EFI et chargement seuls ne
suffisent pas. Metal/WindowServer et soumission libre ne sont pas raccordés.

## Preuves privées et empreintes

Répertoire : `out/efi-native/native-0.2.14-cp-entry-trial/` ; EFI, SMBIOS,
relevés kernel/IORegistry et backups restent hors Git.

- Préparation :
  `8670dd3a2592c5d6e292bcea8c05eaa7faa3e0efe2dbbd021422e9625c21d496`.
- Manifeste nouvelle EFI :
  `f8001d8e28087ba131255cb060e0745c4b8398befb10d7ef818d533e2f101554`.
- Vérification finale :
  `5a1badea83cc7dd5374d311ab3d2533595ea1e9bb03a7bf22b9218e218f8c28a`.

Aucune installation système, modification SIP/NVRAM, recharge, commande GPU
ou redémarrage effectué par l'agent. Le pilote 0.2.13 en mémoire n'a été ni
perturbé ni commandé pendant la copie.
