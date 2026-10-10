# PROBE1401 mise à jour en 0.2.7 — adoption RW voie 1

## État réellement vérifié

À la demande de l'utilisateur, **Navi48Native 0.2.7 est déployé sur PROBE1401**.
Préparation à **2026-10-10T15:27:45.314755+00:00**, vérification finale à
**2026-10-10T15:28:42.569062+00:00**. Signature ad hoc stricte et `ocvalidate`
réussis ; **112 fichiers EFI USB** conformes au manifeste et au staging local.

**Le noyau courant reste en 0.2.6**, zéro instance native, même boot
15:09:46 UTC. **0.2.7 n'est pas encore chargé ni testé au démarrage** ;
l'adoption RW et l'initialisation/calcul Radeon restent à observer. Le refus
connu est toujours **mapping RW BAR0 du boot 0.2.6** (`MapCheck=3`, partage,
provider stable), sans hardware. Aucun chiffre matériel nouveau ni
`FailedStage` / `HardwareTouched` n'est supposé.

## Ce que 0.2.7 change : voie 1 portée au chemin RW

Quand le mapping RW déclare un autre objet mais que le provider rend toujours
le retenu, le compute adopte l'objet déclaré **après revalidation totale**
(longueur, segment contigu à la base config, provider stable). Tout autre cas
reste terminal. L'objet adopté reçoit un retain supplémentaire ; le retenu
d'origine est conservé pour les vérifications de stabilité. Le diagnostic
ajoute `Origin` par RW BAR : 0 `Retained`, 1 `DeclaredAdopted`,
2 `NotAdopted`. La logique miroir du RO (déjà validée sur le vrai noyau en
0.2.4) s'applique au RW ; la couverture host du RW est limitée au diagnostic
publié (le vrai `run()` exige SDK + moteurs), le boot validera l'adoption
réelle.

## Remplacement strict et sauvegardes

Le préparateur accepte `--replace-native-0.2.6`, exclusif des anciennes
options. Chaîne revue : `0.2.0 → … → 0.2.6 → 0.2.7`, chacune avec binaire
épinglé, profil compute 64+64 Mio et double opt-in exacts. L'exception de copie
hors ligne exige le module/version/UUID exact du boot retiré
(**0.2.6 / BAD9777A-7C72-338A-A6B8-01FA4A3D28C3** ici), aucun autre module
expérimental, aucun nœud natif et compteur zéro. Elle ne prouve pas que le
hardware est intact et n'autorise aucun unload/retry/GPU/reboot.

Contrôles C++ : **3 207** contrôleur, **1 323** service/lifecycle, 162 DMA,
29 pool IOVM, 39 accès RAM, sous ASan/UBSan ; zéro échec. **86 tests Python**,
zéro échec. Zéro warning, dix firmwares/deux shaders vérifiés. 324 noms
d'import présents dans BootKC : audit de noms seulement, pas qualification
ABI/chargement de 0.2.7.

Sauvegardes effectivement revérifiées :

| Arbre | Fichiers | Contenu |
|---|---:|---|
| `/Volumes/OPENCORE/EFI` | 101 | référence inchangée |
| `/Volumes/PROBE1401/EFI` | 112 | nouvelle native 0.2.7 |
| `EFI.BACKUP-native-0.2.7-rw-adoption-trial` | 112 | ancienne native 0.2.6 |
| `EFI.BACKUP-native-0.2.6-rw-trial` | 112 | native 0.2.5 conservée |
| `EFI.BACKUP-native-0.2.5-preflight-trial` | 112 | native 0.2.4 conservée |
| `EFI.BACKUP-native-0.2.4-adoption-trial` | 112 | native 0.2.3 conservée |
| `EFI.BACKUP-native-0.2.3-descriptor-trial` | 112 | native 0.2.2 conservée |
| `EFI.BACKUP-native-0.2.2-mapping-trial` | 112 | native 0.2.1 conservée |
| `EFI.BACKUP-native-0.2.1-diagnostics-trial` | 112 | native 0.2.0 conservée |
| `EFI.BACKUP-native-0.2.0-compute-trial` | 112 | native 0.1.2 conservée |
| `EFI.BACKUP-native-0.1.2-trial` | 104 | ancien observateur conservé |

Copie locale de 0.2.6 :
`out/efi-native/native-0.2.7-rw-adoption-trial/previous-trial/EFI` (112 fichiers).
Anciens manifeste/notice archivés et vérifiés, nouveaux manifeste/notice actifs
conformes. Les backups ne sont pas des entrées BIOS indépendantes.

## Produit du prochain boot

- Version **0.2.7**, exécutable SHA-256 :
  `2409a00710dd276ff8c19848e568fca04f0793326294ef9da70af8a6f116ec37`.
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

   Ces lectures ne soumettent pas de commande GPU. Si adoption RW réussie
   (`Origin=1`), exiger les rapports Resources/Compute avec les critères du
   protocole. Si refus, relever `MapCheck`/`Origin` et les valeurs avant toute
   hypothèse.
4. Si écran noir/panic/blocage : photographier, arrêt complet si nécessaire,
   puis **F12 → OPENCORE**. Ce retour n'est pas une récupération GPU qualifiée.

Les étapes matérielles 1/2 exigent toujours les preuves du
[protocole](../NATIVE-COMPUTE-ESSAI.md) : init/firmware, deux fences/tests IB,
64 comparaisons et résultat conforme. Signature, EFI et chargement seuls ne
suffisent pas. Metal/WindowServer et soumission libre ne sont pas raccordés.

## Preuves privées et empreintes

Répertoire : `out/efi-native/native-0.2.7-rw-adoption-trial/` ; EFI, SMBIOS,
relevés kernel/IORegistry et backups restent hors Git.

- Préparation :
  `51ba25698d0e50fe31361f839e7577d7d7eb56a34f1daea1db5ac42490801654`.
- Manifeste nouvelle EFI :
  `a47490632edf48ae4ba93bbb59bbf44390d4841eb6c03c31db9502a526fc48e8`.
- Vérification finale :
  `b66b6ac51c6be35561c975d69fbbeb7dcbe8e9351be40605f3d04004e42c546a`.

Aucune installation système, modification SIP/NVRAM, recharge, commande GPU
ou redémarrage effectué par l'agent.
