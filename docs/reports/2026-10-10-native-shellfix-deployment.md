# PROBE1401 mise à jour en 0.2.11 — shell réparé + diagnostic doorbell

## État réellement vérifié

À la demande explicite de l'utilisateur (« force » déjà accordé pour cette
chaîne), **Navi48Native 0.2.11 est déployé sur PROBE1401 depuis le boot 0.2.10
conservé** (1 instance, GPU tenu). Préparation à
**2026-10-10T20:49:49.587615+00:00**, vérification finale à
**2026-10-10T20:51:57.048208+00:00**. Signature ad hoc stricte et `ocvalidate`
réussis ; **112 fichiers EFI USB** conformes au manifeste et au staging local.

**Transparence totale :** copie forcée (`retained-forced`, 1 instance pendant
la copie). Le kext chargé ne relit jamais les fichiers USB. Vérifications
identiques ; seule « zéro instance » est levée, à la demande explicite, et
enregistrée comme telle. Aucun hot-load, aucune commande au pilote en mémoire.

**0.2.11 n'est pas encore chargé ni testé au démarrage** ; le shell réparé,
le diagnostic doorbell et la suite restent à observer. Point de départ :
**boot 0.2.10, écriture CPU OK, kick tue l'accès (2/2), canal shell prouvé
avec bug plumbing**. Aucun chiffre matériel nouveau ni `FailedStage` /
`HardwareTouched` n'est supposé.

## Ce que 0.2.11 corrige et ajoute

1. **Fix shell (1 ligne)** : `shellAction` stocke `request->result`. Le shell
   répond vraiment (Snapshot, lectures GC/MMHUB/BAR0/survey).
2. **Diagnostic doorbell** : `DoorbellIndex`, `DoorbellBar2Offset`,
   `DoorbellWptrAtKick`, `DoorbellRangeLower/Upper`, `DoorbellReadback` dans
   BootDiagnostics et Compute. Tranchera file/adresse du kick.

Triple opt-in shell (`compute+risk+shell`) conservé, lecture seule stricte
(audit sans écriture). `navi48-native-shell=1` vérifié dans l'EFI USB.

## Remplacement et sauvegardes

Remplacement 0.2.10 → 0.2.11 avec binaire épinglé, profil compute 64+64 Mio +
shell, triple opt-in exacts. Ancienne EFI 0.2.10 sauvegardée et vérifiée (USB +
local).

Contrôles C++ : **3 207** contrôleur, **1 335** service/lifecycle, 162 DMA,
29 pool IOVM, 39 accès RAM, sous ASan/UBSan ; zéro échec. **88 tests Python**,
zéro échec. Zéro warning, dix firmwares/deux shaders vérifiés. 325 noms
d'import (`IOUserClient`) présents dans BootKC : audit de noms seulement, pas
qualification ABI/chargement de 0.2.11.

Sauvegardes effectivement revérifiées :

| Arbre | Fichiers | Contenu |
|---|---:|---|
| `/Volumes/OPENCORE/EFI` | 101 | référence inchangée |
| `/Volumes/PROBE1401/EFI` | 112 | nouvelle native 0.2.11 (+shell fixé) |
| `EFI.BACKUP-native-0.2.11-shellfix-trial` | 112 | ancienne native 0.2.10 |
| `EFI.BACKUP-native-0.2.10-shell-arg-trial` | 112 | native 0.2.9 conservée |
| versions antérieures | 112/104 | 0.2.8 → 0.1.2 + observateur conservés |

Copie locale de 0.2.10 :
`out/efi-native/native-0.2.11-shellfix-trial/previous-trial/EFI` (112 fichiers).
Anciens manifeste/notice archivés et vérifiés, nouveaux manifeste/notice actifs
conformes. Les backups ne sont pas des entrées BIOS indépendantes.

## Produit du prochain boot

- Version **0.2.11**, exécutable SHA-256 :
  `7a322a24560656215b5ff5327f4a229677c858780ad1e0e2d73336da30ddd477`.
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
   /tmp/n48-shell-cli snapshot
   /tmp/n48-shell-cli survey
   ```

   Le shell doit désormais répondre (Snapshot + survey). Si stage 16 : relever
   programmation doorbell avant toute hypothèse.
4. Si écran noir/panic/blocage : photographier, arrêt complet si nécessaire,
   puis **F12 → OPENCORE**. Ce retour n'est pas une récupération GPU qualifiée.

Les étapes matérielles 1/2 exigent toujours les preuves du
[protocole](../NATIVE-COMPUTE-ESSAI.md) : init/firmware, deux fences/tests IB,
64 comparaisons et résultat conforme. Signature, EFI et chargement seuls ne
suffisent pas. Metal/WindowServer et soumission libre ne sont pas raccordés.

## Preuves privées et empreintes

Répertoire : `out/efi-native/native-0.2.11-shellfix-trial/` ; EFI, SMBIOS,
relevés kernel/IORegistry et backups restent hors Git.

- Préparation :
  `c00e704a50626775c42abfb9994276d4348c02e73bb1c26816c82a64337279f1`.
- Manifeste nouvelle EFI :
  `10c719e823d089e57496381dd071b55130ce4911968c7fff92ce79a5b66260f5`.
- Vérification finale :
  `6e6342855ed07e700154a938fe4fa3b8a53b4a2c8da605f28343c22157d78db6`.

Aucune installation système, modification SIP/NVRAM, recharge, commande GPU
ou redémarrage effectué par l'agent. Le pilote 0.2.10 en mémoire n'a été ni
perturbé ni commandé pendant la copie.
