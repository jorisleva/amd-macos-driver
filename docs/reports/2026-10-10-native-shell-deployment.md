# PROBE1401 mise à jour en 0.2.10 — shell lecture seule + phases ring-test

## État réellement vérifié

À la demande explicite de l'utilisateur (« force »), **Navi48Native 0.2.10 est
déployé sur PROBE1401 depuis le boot 0.2.9 conservé** (1 instance, GPU tenu).
Préparation à **2026-10-10T20:25:28.405762+00:00**, vérification finale à
**2026-10-10T20:27:17.159038+00:00**. Signature ad hoc stricte et `ocvalidate`
réussis ; **112 fichiers EFI USB** conformes au manifeste et au staging local.

**Transparence totale :** copie forcée (`retained-forced`, 1 instance pendant
la copie). Le kext chargé ne relit jamais les fichiers USB. Toutes les
vérifications restent identiques ; seule la condition « zéro instance » est
levée, à la demande explicite, et enregistrée comme telle. Aucun hot-load,
aucune commande au pilote en mémoire.

**Incident de préparation honnêtement rapporté :** la première tentative a
déployé 0.2.10 **sans** `navi48-native-shell=1` (l'option `--read-only-shell`
n'était pas branchée sur le profil). Détecté par relecture de l'EFI USB,
rollback vérifié vers 0.2.9, re-préparation **avec** shell, re-déploiement
vérifié. L'EFI actuelle contient bien `navi48-native-shell=1` (triple
opt-in). Le staging sans-shell et son dossier local sont supprimés/archivés
(`native-0.2.10-shell-NOSUFFIX-OBSOLETE`).

**0.2.10 n'est pas encore chargé ni testé au démarrage** ; le shell, les
phases ring-test et la suite restent à observer. Point de départ : **boot
0.2.9, GC vivant, SCRATCH tué pendant le test, service conservé**. Aucun
chiffre matériel nouveau ni `FailedStage` / `HardwareTouched` n'est supposé.

## Ce que 0.2.10 apporte

1. **Découpage ring-test** (mêmes packets/doorbell/timeout) : `RingPhase`
   (0 non atteint, 1 WriteRead, 2 Kicked, 3 Polled), `RingWriteReadback`
   (après `WREG32(CAFEDEAD)`), `RingAfterKick` (après doorbell),
   `RingPollFirst`. Tranchera write vs kick vs poll.
2. **Shell lecture seule** (`Navi48Shell`, triple opt-in compute+risk+shell) :
   `Snapshot`, `ReadGcReg` (SCRATCH/RPTR/ME/MEC), `ReadMmhubReg`
   (FB/TOP/OFFSET), `ReadBar0Word` (scratch seul), `RingTestSurvey`.
   AUCUNE écriture/soumission/horloge. Refusé sans shell=1 ou pré-hardware.
   Sérialisé sous le gate du service. Après ce boot, interaction sans reboot.

## Remplacement et sauvegardes

Remplacement 0.2.9 → 0.2.10 avec binaire épinglé, profil compute 64+64 Mio +
shell, triple opt-in exacts. Ancienne EFI 0.2.9 sauvegardée et vérifiée (USB +
local). Audit statique du shell : sous-classe `IOUserClient` dédiée, aucun
appel d'écriture/soumission/firmware/horloge (vérifié par motif + revue),
sélecteurs stables, sorties valeurs uniquement.

Contrôles C++ : **3 207** contrôleur, **1 335** service/lifecycle (+12 matrice
shell), 162 DMA, 29 pool IOVM, 39 accès RAM, sous ASan/UBSan ; zéro échec.
**88 tests Python** (+1 delta shell, +1 copie forcée), zéro échec. Zéro
warning, dix firmwares/deux shaders vérifiés. 324+1 noms d'import
(`IOUserClient`) présents dans BootKC : audit de noms seulement, pas
qualification ABI/chargement de 0.2.10.

Sauvegardes effectivement revérifiées :

| Arbre | Fichiers | Contenu |
|---|---:|---|
| `/Volumes/OPENCORE/EFI` | 101 | référence inchangée |
| `/Volumes/PROBE1401/EFI` | 112 | nouvelle native 0.2.10 (+shell) |
| `EFI.BACKUP-native-0.2.10-shell-arg-trial` | 112 | ancienne native 0.2.9 |
| `EFI.BACKUP-native-0.2.9-gc-trial` | 112 | native 0.2.8 conservée |
| versions antérieures | 112/104 | 0.2.7 → 0.1.2 + observateur conservés |

Copie locale de 0.2.9 :
`out/efi-native/native-0.2.10-shell-arg-trial/previous-trial/EFI` (112 fichiers).
Anciens manifeste/notice archivés et vérifiés, nouveaux manifeste/notice actifs
conformes. Les backups ne sont pas des entrées BIOS indépendantes.

## Produit du prochain boot

- Version **0.2.10**, exécutable SHA-256 :
  `1ab98748fec072e3420aae1c7384d177900807588d1a53f7edd08d716ca43502`.
- **Six arguments natifs** : platform/scratch-offset/scratch-bytes/compute/
  risk/**shell** (`navi48-native-shell=1`).
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
   ioreg -r -c Navi48Shell -l -w 0
   ```

   Ces lectures ne soumettent pas de commande GPU. Si stage 16 : relever
   phases ring-test. Puis interagir via le shell (lecture seule) au lieu de
   rebooter.
4. Si écran noir/panic/blocage : photographier, arrêt complet si nécessaire,
   puis **F12 → OPENCORE**. Ce retour n'est pas une récupération GPU qualifiée.

Les étapes matérielles 1/2 exigent toujours les preuves du
[protocole](../NATIVE-COMPUTE-ESSAI.md) : init/firmware, deux fences/tests IB,
64 comparaisons et résultat conforme. Signature, EFI et chargement seuls ne
suffisent pas. Metal/WindowServer et soumission libre ne sont pas raccordés.

## Preuves privées et empreintes

Répertoire : `out/efi-native/native-0.2.10-shell-arg-trial/` ; EFI, SMBIOS,
relevés kernel/IORegistry et backups restent hors Git.

- Préparation :
  `825a8e4bc1acad8e7ae03eb6ab3ddaa336d2dee35408f45a3185bdc4d018f622`.
- Manifeste nouvelle EFI :
  `b7b51f8bfbfdbdee42a092e43920c6469cde00722d549fcdb2fe0b16be212fa6`.
- Vérification finale :
  `6bf306361ec7e50934c78b682726c4118ca2496657d1601deae14ec243df452a`.

Aucune installation système, modification SIP/NVRAM, recharge, commande GPU
ou redémarrage effectué par l'agent. Le pilote 0.2.9 en mémoire n'a été ni
perturbé ni commandé pendant la copie.
