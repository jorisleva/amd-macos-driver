# PROBE1401 mise à jour en 0.2.5 — préflight accélérateur corrigé

## État réellement vérifié

À la demande de l'utilisateur, **Navi48Native 0.2.5 est déployé sur PROBE1401**.
Préparation à **2026-10-10T14:49:56.462115+00:00**, vérification finale à
**2026-10-10T14:51:16.898497+00:00**. Signature ad hoc stricte et `ocvalidate`
réussis ; **112 fichiers EFI USB** conformes au manifeste et au staging local.

**Le noyau courant reste en 0.2.4**, zéro instance native, même boot
14:30:29 UTC. **0.2.5 n'est pas encore chargé ni testé au démarrage** ; le
préflight corrigé et l'initialisation/calcul Radeon restent à observer. Le
refus connu est toujours **préflight accélérateur du boot 0.2.4**
(`FailedStage=1`, `PreflightCheck=7`, itérateur null), sans hardware. Aucun
chiffre matériel nouveau ni `FailedStage` / `HardwareTouched` n'est supposé.

## Ce que 0.2.5 change, sans autoriser un concurrent

Le préflight d'exclusion (`AcceleratorPreflight.hpp`, fonction exacte appelée
par `run()`, couverte sur host avec doubles IOKit) : itérateur null =
ensemble vide présumé (marqué `AcceleratorIteratorNull=1`), itérateur vide =
passage, objet trouvé = refus `ExclusiveAccess` maintenu, dictionnaire null =
`NoMemory`. Un accélérateur réel reste rejeté, jamais autorisé. Le risque
résiduel (échec d'énumération masquant un concurrent) est borné par les
fences et comparaisons lane par lane : un concurrent actif corromprait les
résultats vérifiés.

## Remplacement strict et sauvegardes

Le préparateur accepte `--replace-native-0.2.4`, exclusif des anciennes
options. Chaîne revue : `0.2.0 → 0.2.1 → 0.2.2 → 0.2.3 → 0.2.4 → 0.2.5`,
chacune avec binaire épinglé, profil compute 64+64 Mio et double opt-in
exacts. L'exception de copie hors ligne exige le module/version/UUID exact du
boot retiré (**0.2.4 / 4F894D5F-FDC9-384C-8688-C6B4BAA68674** ici), aucun autre
module expérimental, aucun nœud natif et compteur zéro. Elle ne prouve pas que
le hardware est intact et n'autorise aucun unload/retry/GPU/reboot.

Contrôles C++ : **3 207** contrôleur (+5 matrice préflight), **1 311**
service/lifecycle, 162 DMA, 29 pool IOVM, 39 accès RAM, sous ASan/UBSan ;
zéro échec. **86 tests Python**, zéro échec. Zéro warning, dix firmwares/deux
shaders vérifiés. 324 noms d'import présents dans BootKC : audit de noms
seulement, pas qualification ABI/chargement de 0.2.5.

Sauvegardes effectivement revérifiées :

| Arbre | Fichiers | Contenu |
|---|---:|---|
| `/Volumes/OPENCORE/EFI` | 101 | référence inchangée |
| `/Volumes/PROBE1401/EFI` | 112 | nouvelle native 0.2.5 |
| `EFI.BACKUP-native-0.2.5-preflight-trial` | 112 | ancienne native 0.2.4 |
| `EFI.BACKUP-native-0.2.4-adoption-trial` | 112 | native 0.2.3 conservée |
| `EFI.BACKUP-native-0.2.3-descriptor-trial` | 112 | native 0.2.2 conservée |
| `EFI.BACKUP-native-0.2.2-mapping-trial` | 112 | native 0.2.1 conservée |
| `EFI.BACKUP-native-0.2.1-diagnostics-trial` | 112 | native 0.2.0 conservée |
| `EFI.BACKUP-native-0.2.0-compute-trial` | 112 | native 0.1.2 conservée |
| `EFI.BACKUP-native-0.1.2-trial` | 104 | ancien observateur conservé |

Copie locale de 0.2.4 :
`out/efi-native/native-0.2.5-preflight-trial/previous-trial/EFI` (112 fichiers).
Anciens manifeste/notice archivés et vérifiés, nouveaux manifeste/notice actifs
conformes. Les backups ne sont pas des entrées BIOS indépendantes.

## Produit du prochain boot

- Version **0.2.5**, exécutable SHA-256 :
  `c1e6c4bb565380bb4a7585a212de6b4e90dca4320c532bebb467716ec403ddd0`.
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

   Ces lectures ne soumettent pas de commande GPU. Le préflight doit passer
   (`AcceleratorIteratorNull=1` attendu) ; si refus, relever
   `FailedStage`/`PreflightCheck` avant toute hypothèse. Si l'orchestration
   continue, exiger les rapports Resources/Compute avec les critères du
   protocole.
4. Si écran noir/panic/blocage : photographier, arrêt complet si nécessaire,
   puis **F12 → OPENCORE**. Ce retour n'est pas une récupération GPU qualifiée.

Les étapes matérielles 1/2 exigent toujours les preuves du
[protocole](../NATIVE-COMPUTE-ESSAI.md) : init/firmware, deux fences/tests IB,
64 comparaisons et résultat conforme. Signature, EFI et chargement seuls ne
suffisent pas. Metal/WindowServer et soumission libre ne sont pas raccordés.

## Preuves privées et empreintes

Répertoire : `out/efi-native/native-0.2.5-preflight-trial/` ; EFI, SMBIOS,
relevés kernel/IORegistry et backups restent hors Git.

- Préparation :
  `abf83fe039dd2a190b2fb39c3e7628eb024180816497281017698ab6213a780c`.
- Manifeste nouvelle EFI :
  `1cac501332dbe876b5503dbb88726f980a7145c64a465c67c0dc23bf51be31be`.
- Vérification finale :
  `dae853db2e982f5e07ab754d2d20f1ad3f1fffc556ec6910ce83d362f73c0d15`.

Aucune installation système, modification SIP/NVRAM, recharge, commande GPU
ou redémarrage effectué par l'agent.
