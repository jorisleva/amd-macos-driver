# PROBE1401 mise à jour en 0.2.13 — reset ME après GfxStart

## État réellement vérifié

À la demande explicite de l'utilisateur (« force » déjà accordé pour cette
chaîne), **Navi48Native 0.2.13 est déployé sur PROBE1401 depuis le boot 0.2.12
conservé** (1 instance, GPU tenu). Préparation à
**2026-10-11T13:06:27.950715+00:00**, vérification finale à
**2026-10-11T13:08:04.671097+00:00**. Signature ad hoc stricte et `ocvalidate`
réussis ; **112 fichiers EFI USB** conformes au manifeste et au staging local.

**Transparence totale :** copie forcée (`retained-forced`, 1 instance pendant
la copie). Le kext chargé ne relit jamais les fichiers USB. Vérifications
identiques ; seule « zéro instance » est levée, à la demande explicite, et
enregistrée comme telle. Aucun hot-load, aucune commande au pilote en mémoire.

**0.2.13 n'est pas encore chargé ni testé au démarrage** ; le reset ME après
start et la suite restent à observer. Point de départ : **boot 0.2.12, tout
le GC mort après le kick, `ME_PIPE0_RESET` collé avant**. Aucun chiffre
matériel nouveau ni `FailedStage` / `HardwareTouched` n'est supposé.

## Ce que 0.2.13 change : situer le reset collé

`MeCntlAfterStart`/`MecCntlAfterStart` (halt state juste après `cp_gfx_start`,
step 4) s'ajoutent au diagnostic. Lecture du prochain boot :

| AfterStart | Avant kick | Conclusion |
|---|---|---|
| bit 20 actif | bit 20 actif | le `clear` n'a jamais pris |
| bit 20 éteint | bit 20 actif | le ME se remet seul en reset (entrée invalide) |

Mêmes appels, aucune écriture ajoutée. Shell lecture seule conservé.

## Remplacement et sauvegardes

Remplacement 0.2.12 → 0.2.13 avec binaire épinglé, profil compute 64+64 Mio +
shell, triple opt-in exacts. Ancienne EFI 0.2.12 sauvegardée et vérifiée (USB +
local).

Contrôles C++ : **3 207** contrôleur, **1 335** service/lifecycle, 162 DMA,
29 pool IOVM, 39 accès RAM, sous ASan/UBSan ; zéro échec. **88 tests Python**,
zéro échec. Zéro warning, dix firmwares/deux shaders vérifiés. 325 noms
d'import présents dans BootKC : audit de noms seulement, pas qualification
ABI/chargement de 0.2.13.

Sauvegardes effectivement revérifiées :

| Arbre | Fichiers | Contenu |
|---|---:|---|
| `/Volumes/OPENCORE/EFI` | 101 | référence inchangée |
| `/Volumes/PROBE1401/EFI` | 112 | nouvelle native 0.2.13 (+reset ME) |
| `EFI.BACKUP-native-0.2.13-me-reset-trial` | 112 | ancienne native 0.2.12 |
| `EFI.BACKUP-native-0.2.12-gc-witness-trial` | 112 | native 0.2.11 conservée |
| versions antérieures | 112/104 | 0.2.10 → 0.1.2 + observateur conservés |

Copie locale de 0.2.12 :
`out/efi-native/native-0.2.13-me-reset-trial/previous-trial/EFI` (112 fichiers).
Anciens manifeste/notice archivés et vérifiés, nouveaux manifeste/notice actifs
conformes. Les backups ne sont pas des entrées BIOS indépendantes.

## Produit du prochain boot

- Version **0.2.13**, exécutable SHA-256 :
  `9888439002eea33af2196b0f3977fd2f35fcda8ea46ee0ccbb6f3c9f96cd8c6c`.
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

   Si stage 16 : comparer `MeCntlAfterStart` (après start) vs avant kick
   avant toute hypothèse. Puis interagir via le shell (lecture seule).
4. Si écran noir/panic/blocage : photographier, arrêt complet si nécessaire,
   puis **F12 → OPENCORE**. Ce retour n'est pas une récupération GPU qualifiée.

Les étapes matérielles 1/2 exigent toujours les preuves du
[protocole](../NATIVE-COMPUTE-ESSAI.md) : init/firmware, deux fences/tests IB,
64 comparaisons et résultat conforme. Signature, EFI et chargement seuls ne
suffisent pas. Metal/WindowServer et soumission libre ne sont pas raccordés.

## Preuves privées et empreintes

Répertoire : `out/efi-native/native-0.2.13-me-reset-trial/` ; EFI, SMBIOS,
relevés kernel/IORegistry et backups restent hors Git.

- Préparation :
  `28df0c36bdb66f51988e730f0ed5c9231bd3cf74b64660745d1fbe5f896c72fd`.
- Manifeste nouvelle EFI :
  `99da4a561097b420fcbf51cef5cb6a559700e1455c116c5bbb179c11fcebf422`.
- Vérification finale :
  `defc2ebaa00470b38f02bdd036078c5664a3b1bd706308730ec3531a0cad9d30`.

Aucune installation système, modification SIP/NVRAM, recharge, commande GPU
ou redémarrage effectué par l'agent. Le pilote 0.2.12 en mémoire n'a été ni
perturbé ni commandé pendant la copie.
