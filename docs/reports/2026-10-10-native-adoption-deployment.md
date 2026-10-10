# PROBE1401 mise à jour en 0.2.4 — adoption de l'objet déclaré

## État réellement vérifié

À la demande de l'utilisateur, **Navi48Native 0.2.4 est déployé sur PROBE1401**.
Préparation à **2026-10-10T14:25:46.309430+00:00**, vérification finale à
**2026-10-10T14:26:31.660542+00:00**. Signature ad hoc stricte et `ocvalidate`
réussis ; **112 fichiers EFI USB** conformes au manifeste et au staging local.

**Le noyau courant reste en 0.2.3**, zéro instance native, même boot
14:02:44 UTC. **0.2.4 n'est pas encore chargé ni testé au démarrage** ;
l'adoption et l'initialisation/calcul Radeon restent à observer. Le scénario
connu est toujours **mapping partagé déclarant un autre objet du boot 0.2.3**,
sans hardware/DMA/compute. Aucun chiffre matériel nouveau ni `FailedStage` /
`HardwareTouched` n'est supposé.

## Ce que 0.2.4 change : voie 1, sans rien masquer

Quand `checkMaps()` observe le scénario exact du boot 0.2.3
(relecture==retenu≠déclaré), le contrôleur adopte l'objet déclaré **après
revalidation totale** : longueur, segment physique contigu à la base config,
et provider stable rendant toujours le retenu. Tout autre cas
(substitution provider, objet déclaré non conforme, relecture absente) reste
terminal. L'adoption conserve les deux retains (déclaré + retenu d'origine) ;
`checkDescriptors()` continue de vérifier le provider contre le retenu, et
`checkMaps()` revérifie chaque propriété restante (tâche, longueur, segment,
options, adresse, chevauchement). Rien n'est sauté.

Le diagnostic ajoute `DescriptorOrigin` par BAR : 0 `Retained` (chemin
strict), 1 `DeclaredAdopted` (voie 1), 2 `NotAdopted` (adoption refusée).
Si l'acquisition réussit, le prochain boot doit montrer `DeclaredAdopted` sur
les BAR partagés, puis les rapports Resources/Compute et — peut-être — les
premiers résultats matériels.

## Remplacement strict et sauvegardes

Le préparateur accepte `--replace-native-0.2.3`, exclusif des anciennes
options. Chaîne revue : `0.2.0 → 0.2.1 → 0.2.2 → 0.2.3 → 0.2.4`, chacune avec
binaire épinglé, profil compute 64+64 Mio et double opt-in exacts. L'exception
de copie hors ligne exige le module/version/UUID exact du boot retiré
(**0.2.3 / 1E9DDB5A-0841-3DE1-9999-0D854E226E14** ici), aucun autre module
expérimental, aucun nœud natif et compteur zéro. Elle ne prouve pas que le
hardware est intact et n'autorise aucun unload/retry/GPU/reboot.

Contrôles C++ : **3 202** contrôleur, **1 311** service/lifecycle, 162 DMA,
29 pool IOVM, 39 accès RAM, sous ASan/UBSan ; zéro échec. **86 tests Python**,
zéro échec. Zéro warning, dix firmwares/deux shaders vérifiés. 324 noms
d'import présents dans BootKC : audit de noms seulement, pas qualification
ABI/chargement de 0.2.4.

Sauvegardes effectivement revérifiées :

| Arbre | Fichiers | Contenu |
|---|---:|---|
| `/Volumes/OPENCORE/EFI` | 101 | référence inchangée |
| `/Volumes/PROBE1401/EFI` | 112 | nouvelle native 0.2.4 |
| `EFI.BACKUP-native-0.2.4-adoption-trial` | 112 | ancienne native 0.2.3 |
| `EFI.BACKUP-native-0.2.3-descriptor-trial` | 112 | native 0.2.2 conservée |
| `EFI.BACKUP-native-0.2.2-mapping-trial` | 112 | native 0.2.1 conservée |
| `EFI.BACKUP-native-0.2.1-diagnostics-trial` | 112 | native 0.2.0 conservée |
| `EFI.BACKUP-native-0.2.0-compute-trial` | 112 | native 0.1.2 conservée |
| `EFI.BACKUP-native-0.1.2-trial` | 104 | ancien observateur conservé |

Copie locale de 0.2.3 :
`out/efi-native/native-0.2.4-adoption-trial/previous-trial/EFI` (112 fichiers).
Anciens manifeste/notice archivés et vérifiés, nouveaux manifeste/notice actifs
conformes. Les backups ne sont pas des entrées BIOS indépendantes.

## Produit du prochain boot

- Version **0.2.4**, exécutable SHA-256 :
  `9fe464d7cffac6c0cdd2b7dcf77244cad96d05d1c726b2059f8fc0fead2bef9e`.
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

   Ces lectures ne soumettent pas de commande GPU. Si l'acquisition réussit,
   exiger `DescriptorOrigin=1`, puis les rapports Resources/Compute avec les
   critères du protocole. Si refus, relever `MapCheck`/`DescriptorOrigin` et
   les valeurs avant toute hypothèse.
4. Si écran noir/panic/blocage : photographier, arrêt complet si nécessaire,
   puis **F12 → OPENCORE**. Ce retour n'est pas une récupération GPU qualifiée.

Les étapes matérielles 1/2 exigent toujours les preuves du
[protocole](../NATIVE-COMPUTE-ESSAI.md) : init/firmware, deux fences/tests IB,
64 comparaisons et résultat conforme. Signature, EFI et chargement seuls ne
suffisent pas. Metal/WindowServer et soumission libre ne sont pas raccordés.

## Preuves privées et empreintes

Répertoire : `out/efi-native/native-0.2.4-adoption-trial/` ; EFI, SMBIOS,
relevés kernel/IORegistry et backups restent hors Git.

- Préparation :
  `d671c80f264a841cd5188ab85345a06ced0228eb5ad91a0b33ebca0e96282c8b`.
- Manifeste nouvelle EFI :
  `330dade5fce2bc7e28f659ca06eb340f8eef71bdb75c4e87c4e2842b6a97aca4`.
- Vérification finale :
  `fa8b800eecbb868579585e9f3641ffa367c042f24a44258ea1e2c6d26b5244f5`.

Aucune installation système, modification SIP/NVRAM, recharge, commande GPU
ou redémarrage effectué par l'agent.
