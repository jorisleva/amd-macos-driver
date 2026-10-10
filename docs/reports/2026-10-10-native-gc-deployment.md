# PROBE1401 mise à jour en 0.2.9 — relevé GC (copie forcée transparente)

## État réellement vérifié

À la demande explicite de l'utilisateur (« force »), **Navi48Native 0.2.9 est
déployé sur PROBE1401 depuis le boot 0.2.8 conservé** (1 instance, GPU tenu).
Préparation à **2026-10-10T19:43:38.116818+00:00**, vérification finale à
**2026-10-10T19:44:32.387448+00:00**. Signature ad hoc stricte et `ocvalidate`
réussis ; **112 fichiers EFI USB** conformes au manifeste et au staging local.

**Transparence totale :** copie forcée depuis un boot non retiré
(`retained-forced`, 1 instance pendant la copie). Le kext chargé est déjà en
mémoire et ne relit jamais les fichiers USB : la copie ne peut pas le
perturber. Toutes les vérifications de fichiers restent identiques (staging,
hashes, signature, ocvalidate, swap avec rollback). Seule la condition « zéro
instance » est levée, à la demande explicite de l'utilisateur, et enregistrée
comme telle. Le pilote en mémoire n'est ni déchargé, ni rechargé, ni
commandé ; aucun hot-load.

**0.2.9 n'est pas encore chargé ni testé au démarrage** ; son relevé GC et la
suite de l'initialisation restent à observer. Le point de départ connu est le
**boot 0.2.8 : RingTest step 5, SCRATCH illisible, CP ne fetche pas, service
conservé**. Aucun chiffre matériel nouveau ni `FailedStage` / `HardwareTouched`
n'est supposé.

## Ce que 0.2.9 change : relevé GC en lecture seule

Avant le ring-test, le compute lit (sans écrire) : `GCBase0`, `GCBase1`
(bases IP résolues), `GCScratchBefore` (SCRATCH avant le test),
`GCScratchAfterWrite` (après), `GCRb0Rptr`, `GCCpMeCntl`, `GCCpMecCntl`
(états de halt PFP/ME/MEC). Publiés dans `BootDiagnostics` et Compute.
Lecture du prochain boot en cas de timeout :

| Observation | Interprétation |
|---|---|
| tout le GC illisible (`0xFFFFFFFF` partout) | base GC fausse |
| seul SCRATCH mort, autres GC vivants | power-gate/horloges sur ce registre |
| `GCCpMeCntl`/`GCCpMecCntl` haltés | CP jamais démarré (remonter à `GfxStart`) |

## Remplacement et sauvegardes

Remplacement 0.2.8 → 0.2.9 avec binaire épinglé, profil compute 64+64 Mio et
double opt-in exacts. Ancienne EFI 0.2.8 sauvegardée et vérifiée (USB +
local). Le préparateur accepte désormais `--replace-native-0.2.8` (copie
forcée depuis boot conservé autorisée uniquement pour cette version) ; les
tests couvrent le marquage `retained-forced` et le refus sans compteur entier.

Contrôles C++ : **3 207** contrôleur, **1 323** service/lifecycle, 162 DMA,
29 pool IOVM, 39 accès RAM, sous ASan/UBSan ; zéro échec. **87 tests Python**
(+1 matrice copie forcée), zéro échec. Zéro warning, dix firmwares/deux
shaders vérifiés. 324 noms d'import présents dans BootKC : audit de noms
seulement, pas qualification ABI/chargement de 0.2.9.

Sauvegardes effectivement revérifiées :

| Arbre | Fichiers | Contenu |
|---|---:|---|
| `/Volumes/OPENCORE/EFI` | 101 | référence inchangée |
| `/Volumes/PROBE1401/EFI` | 112 | nouvelle native 0.2.9 |
| `EFI.BACKUP-native-0.2.9-gc-trial` | 112 | ancienne native 0.2.8 |
| `EFI.BACKUP-native-0.2.8-stage16-trial` | 112 | native 0.2.7 conservée |
| versions antérieures | 112/104 | 0.2.6 → 0.1.2 + observateur conservés |

Copie locale de 0.2.8 :
`out/efi-native/native-0.2.9-gc-trial/previous-trial/EFI` (112 fichiers).
Anciens manifeste/notice archivés et vérifiés, nouveaux manifeste/notice actifs
conformes. Les backups ne sont pas des entrées BIOS indépendantes.

## Produit du prochain boot

- Version **0.2.9**, exécutable SHA-256 :
  `0bada18712b5bd2e31424ac40ea3996a4d70e277020d4e906a9bad1b4be09d07`.
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

   Ces lectures ne soumettent pas de commande GPU. Si stage 16 échoue,
   relever bases GC et états de halt avant toute hypothèse.
4. Si écran noir/panic/blocage : photographier, arrêt complet si nécessaire,
   puis **F12 → OPENCORE**. Ce retour n'est pas une récupération GPU qualifiée.

Les étapes matérielles 1/2 exigent toujours les preuves du
[protocole](../NATIVE-COMPUTE-ESSAI.md) : init/firmware, deux fences/tests IB,
64 comparaisons et résultat conforme. Signature, EFI et chargement seuls ne
suffisent pas. Metal/WindowServer et soumission libre ne sont pas raccordés.

## Preuves privées et empreintes

Répertoire : `out/efi-native/native-0.2.9-gc-trial/` ; EFI, SMBIOS,
relevés kernel/IORegistry et backups restent hors Git.

- Préparation :
  `16e2c968fd7cf0cbc274c8729ecd75f339a0c8517c7b6735034561595bdd77ac`.
- Manifeste nouvelle EFI :
  `0096aa288cf549ec763802f085a846cd46fdbe3324b4b51bbf2473c8ea267edb`.
- Vérification finale :
  `c8b78fafb6aadb2100ad76cb50008be247a258c7ff7ca947df5b65042205dd61`.

Aucune installation système, modification SIP/NVRAM, recharge, commande GPU
ou redémarrage effectué par l'agent. Le pilote 0.2.8 en mémoire n'a été ni
perturbé ni commandé pendant la copie.
