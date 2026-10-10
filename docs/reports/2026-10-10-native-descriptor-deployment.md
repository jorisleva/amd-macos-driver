# PROBE1401 mise à jour en 0.2.3 — identités descripteur au prochain boot

## État réellement vérifié

À la demande de l'utilisateur, **Navi48Native 0.2.3 est déployé sur PROBE1401**.
Préparation à **2026-10-10T13:57:43.271986+00:00**, vérification finale à
**2026-10-10T13:59:10.698519+00:00**. Signature ad hoc stricte et `ocvalidate`
réussis ; **112 fichiers EFI USB** conformes au manifeste et au staging local.

**Le noyau courant reste en 0.2.2**, zéro instance native, même boot
13:20:56 UTC. **0.2.3 n'est pas encore chargé ni testé au démarrage** ; ses
identités descripteur et l'initialisation/calcul Radeon restent à observer. Le
refus connu est toujours **`DescriptorMismatch` sur BAR0 (`MapCheck=3`) du
boot 0.2.2**, sans hardware/DMA/compute. Aucun chiffre matériel nouveau ni
`FailedStage` / `HardwareTouched` n'est supposé.

## Ce que 0.2.3 change, sans assouplir les gardes

`checkMaps()` relit le provider au moment du contrôle (lecture seule, sans
retain ni stockage) et enregistre trois booléens par BAR :

- `RereadPresent` : la relecture existe ;
- `RereadMatch` : elle rend le descripteur retenu ;
- `DeclaredIsReread` : le mapping déclare l'objet relu.

Lecture du prochain boot en cas de `DescriptorMismatch` :

| RereadMatch | DeclaredIsReread | Interprétation |
|---|---|---|
| 0 | 1 | **substitution provider** : déclaré==relu≠retenu |
| 1 | 0 | **mapping partagé** : relu==retenu≠déclaré |
| 0 | 0 + `RereadPresent=0` | relecture disparue au moment du check |

L'ordre réel des gardes est conservé : `revalidateLocked()` rejoue
`checkDescriptors()` après les mappings, donc une substitution persistante se
manifeste en `ConfigurationChanged` avant `checkMaps()`. Seul un mapping
partagé déclarant un autre objet atteint `DescriptorMismatch` avec
relu==retenu. Les tests l'affirment aux deux niveaux (contrôleur et service
réel), sans masquer aucune divergence.

## Remplacement strict et sauvegardes

Le préparateur accepte `--replace-native-0.2.2`, exclusif des anciennes
options. Chaîne revue : `0.2.0 → 0.2.1 → 0.2.2 → 0.2.3`, chacune avec binaire
épinglé, profil compute 64+64 Mio et double opt-in exacts. L'exception de copie
hors ligne exige le module/version/UUID exact du boot retiré
(**0.2.2 / E88B08E7-29B2-3BF1-A836-D7270C52B6AC** ici), aucun autre module
expérimental, aucun nœud natif et compteur zéro. Elle ne prouve pas que le
hardware est intact et n'autorise aucun unload/retry/GPU/reboot.

Contrôles C++ : **3 028** contrôleur, **1 290** service/lifecycle, 162 DMA,
29 pool IOVM, 39 accès RAM, sous ASan/UBSan ; zéro échec. **86 tests Python**,
zéro échec. Zéro warning, dix firmwares/deux shaders vérifiés. 324 noms
d'import présents dans BootKC : audit de noms seulement, pas qualification
ABI/chargement de 0.2.3.

Sauvegardes effectivement revérifiées :

| Arbre | Fichiers | Contenu |
|---|---:|---|
| `/Volumes/OPENCORE/EFI` | 101 | référence inchangée |
| `/Volumes/PROBE1401/EFI` | 112 | nouvelle native 0.2.3 |
| `EFI.BACKUP-native-0.2.3-descriptor-trial` | 112 | ancienne native 0.2.2 |
| `EFI.BACKUP-native-0.2.2-mapping-trial` | 112 | native 0.2.1 conservée |
| `EFI.BACKUP-native-0.2.1-diagnostics-trial` | 112 | native 0.2.0 conservée |
| `EFI.BACKUP-native-0.2.0-compute-trial` | 112 | native 0.1.2 conservée |
| `EFI.BACKUP-native-0.1.2-trial` | 104 | ancien observateur conservé |

Copie locale de 0.2.2 :
`out/efi-native/native-0.2.3-descriptor-trial/previous-trial/EFI` (112 fichiers).
Anciens manifeste/notice archivés et vérifiés, nouveaux manifeste/notice actifs
conformes. Les backups ne sont pas des entrées BIOS indépendantes.

## Produit du prochain boot

- Version **0.2.3**, exécutable SHA-256 :
  `61fc8ffc684fce66d8898db4f2b8dd7143b6dea288ecb25d7f1de613c1a4eeec`.
- **Trois fichiers EFI changés** : `OC/config.plist`, `Info.plist` et exécutable
  de Navi48Native. SMBIOS, sécurité, patches Ryzen, cinq kexts de référence,
  bornes Darwin 25.6.0, scratch et cinq arguments natifs conservés.

## Prochain boot physique, pas de reload

1. Enregistrer le travail puis **F12 → PROBE1401 UEFI → Tahoe installé**.
2. Ne pas mettre en veille, ne pas décharger/recharger le pilote.
3. Au retour, relever version, diagnostic persistant et service :

   ```sh
   kmutil showloaded | grep -i Navi48Native
   ioreg -r -c IOResources -l -w 0 | grep 'Navi48Native,BootDiagnostics'
   ioreg -r -c Navi48Native -l -w 0
   ```

   Ces lectures ne soumettent pas de commande GPU. Si `DescriptorMismatch`
   revient, relever `RereadMatch`/`DeclaredIsReread` avant toute hypothèse.
   Lire d'abord `PlatformObserved`/`DMAObserved`/`ComputeObserved`.
4. Si écran noir/panic/blocage : photographier, arrêt complet si nécessaire,
   puis **F12 → OPENCORE**. Ce retour n'est pas une récupération GPU qualifiée.

Les étapes matérielles 1/2 exigent toujours les preuves du
[protocole](../NATIVE-COMPUTE-ESSAI.md) : init/firmware, deux fences/tests IB,
64 comparaisons et résultat conforme. Signature, EFI et chargement seuls ne
suffisent pas. Metal/WindowServer et soumission libre ne sont pas raccordés.

## Preuves privées et empreintes

Répertoire : `out/efi-native/native-0.2.3-descriptor-trial/` ; EFI, SMBIOS,
relevés kernel/IORegistry et backups restent hors Git.

- Préparation :
  `7385db9897ae2041288514ac1e5a5ac9971e1dd5d175f05f8c16408902f85984`.
- Manifeste nouvelle EFI :
  `f01436ca553067e08279c2fb0eed9db0b49111b50adf679ea57b59c3b8319def`.
- Vérification finale :
  `22ba51b61c2d7b9a84fa31dea8c49c940501707f0f348693463637068119b805`.

Aucune installation système, modification SIP/NVRAM, recharge, commande GPU
ou redémarrage effectué par l'agent.
