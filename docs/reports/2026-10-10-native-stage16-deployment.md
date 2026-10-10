# PROBE1401 mise à jour en 0.2.8 — détail stage 16

## État réellement vérifié

Depuis le boot de secours (aucun module natif, zéro instance), **Navi48Native
0.2.8 est déployé sur PROBE1401**. Préparation à
**2026-10-10T15:47:54.513482+00:00**, vérification finale à
**2026-10-10T15:50:05.226961+00:00**. Signature ad hoc stricte et `ocvalidate`
réussis ; **112 fichiers EFI USB** conformes au manifeste et au staging local.

**0.2.8 n'est pas encore chargé ni testé au démarrage** ; son détail stage 16
et la suite de l'initialisation restent à observer. Le point de départ connu
est le **boot 0.2.7 : premier hardware, firmware chargé, stage 16 timeout
(`0xE00002D6`), service conservé**. Aucun chiffre matériel nouveau ni
`FailedStage` / `HardwareTouched` n'est supposé.

## Ce que 0.2.8 change : nommer le sous-appel du stage 16

Le stage 16 enchaîne 6 appels ; le timeout ne disait pas lequel a expiré.
`Stage16Step` (0 non atteint, 1 `GfxConstants`, 2 `MqdInit`, 3 `MapKgqMes`,
4 `GfxStart`, 5 `RingTest`, 6 `EopTest`, 7 `Done`), `RingTestPassed`,
`FetchProven` et `RingTestValue` (relecture `SCRATCH_REG0`) sont publiés dans
`BootDiagnostics` et dans le rapport Compute. Mêmes appels, mêmes attentes
bornées, aucun garde modifié. Lecture du prochain boot en cas de timeout :

| Stage16Step | Interprétation |
|---|---|
| 5 + `RingTestPassed=0` | le CP ne fetche pas (ring/doorbell/démarrage) |
| 6 (`RingTestPassed=1`) | le CP exécute mais le chemin mémoire EOP échoue |
| 1–4 | échec d'initialisation GFX/MQD/MES/démarrage, pas un poll |

## Remplacement strict et sauvegardes

Déployé depuis le boot de secours : remplacement 0.2.7 → 0.2.8 avec binaire
épinglé, profil compute 64+64 Mio et double opt-in exacts (l'exception de
copie hors ligne ne s'appliquait pas, le boot étant de référence). Ancienne
EFI 0.2.7 sauvegardée et vérifiée (USB + local).

Contrôles C++ : **3 207** contrôleur, **1 323** service/lifecycle, 162 DMA,
29 pool IOVM, 39 accès RAM, sous ASan/UBSan ; zéro échec. **86 tests Python**,
zéro échec. Zéro warning, dix firmwares/deux shaders vérifiés. 324 noms
d'import présents dans BootKC : audit de noms seulement, pas qualification
ABI/chargement de 0.2.8.

Sauvegardes effectivement revérifiées :

| Arbre | Fichiers | Contenu |
|---|---:|---|
| `/Volumes/OPENCORE/EFI` | 101 | référence inchangée |
| `/Volumes/PROBE1401/EFI` | 112 | nouvelle native 0.2.8 |
| `EFI.BACKUP-native-0.2.8-stage16-trial` | 112 | ancienne native 0.2.7 |
| `EFI.BACKUP-native-0.2.7-rw-adoption-trial` | 112 | native 0.2.6 conservée |
| `EFI.BACKUP-native-0.2.6-rw-trial` | 112 | native 0.2.5 conservée |
| `EFI.BACKUP-native-0.2.5-preflight-trial` | 112 | native 0.2.4 conservée |
| `EFI.BACKUP-native-0.2.4-adoption-trial` | 112 | native 0.2.3 conservée |
| `EFI.BACKUP-native-0.2.3-descriptor-trial` | 112 | native 0.2.2 conservée |
| `EFI.BACKUP-native-0.2.2-mapping-trial` | 112 | native 0.2.1 conservée |
| `EFI.BACKUP-native-0.2.1-diagnostics-trial` | 112 | native 0.2.0 conservée |
| `EFI.BACKUP-native-0.2.0-compute-trial` | 112 | native 0.1.2 conservée |
| `EFI.BACKUP-native-0.1.2-trial` | 104 | ancien observateur conservé |

Copie locale de 0.2.7 :
`out/efi-native/native-0.2.8-stage16-trial/previous-trial/EFI` (112 fichiers).
Anciens manifeste/notice archivés et vérifiés, nouveaux manifeste/notice actifs
conformes. Les backups ne sont pas des entrées BIOS indépendantes.

## Produit du prochain boot

- Version **0.2.8**, exécutable SHA-256 :
  `8871d7f167de34ce98d14780c62d3608d9d7530f3a7bc7eaa59bc468e4f8c0bd`.
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
   relever `Stage16Step`, `RingTestPassed`, `FetchProven` et `RingTestValue`
   avant toute hypothèse.
4. Si écran noir/panic/blocage : photographier, arrêt complet si nécessaire,
   puis **F12 → OPENCORE**. Ce retour n'est pas une récupération GPU qualifiée.

Les étapes matérielles 1/2 exigent toujours les preuves du
[protocole](../NATIVE-COMPUTE-ESSAI.md) : init/firmware, deux fences/tests IB,
64 comparaisons et résultat conforme. Signature, EFI et chargement seuls ne
suffisent pas. Metal/WindowServer et soumission libre ne sont pas raccordés.

## Preuves privées et empreintes

Répertoire : `out/efi-native/native-0.2.8-stage16-trial/` ; EFI, SMBIOS,
relevés kernel/IORegistry et backups restent hors Git.

- Préparation :
  `7db27c8520f9022c33e34ff6267e172ae975af77ca956570240a17a073ef452d`.
- Manifeste nouvelle EFI :
  `3886b3d169c26f2f26936d2a00b1e28e22e799ec875ee4e346b2b0f02ad395d2`.
- Vérification finale :
  `4f28e9d597ab4452fe9ed184c6f90cfce0573d43af14e9cf87115ae0db8e2b81`.

Aucune installation système, modification SIP/NVRAM, recharge, commande GPU
ou redémarrage effectué par l'agent.
