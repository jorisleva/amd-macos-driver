# Essai natif 0.2.0 : initialisation et calcul Radeon

> **Premier boot effectué le 10 octobre :** module 0.2.0 chargé, UUID/arguments
> conformes, mais service retiré, pas de rapport compute ni résultat Radeon
> validé. [Rapport du boot](reports/2026-10-10-native-compute-boot.md).
> **Suite : capture privilégiée lue, messages du boot écrasés.** Refus exact
> inconnu. Correctif diagnostic **0.2.1** compilé/testé hors ligne, **non chargé
> et non déployé** ([rapport et décodage](reports/2026-10-10-native-boot-diagnostics.md)).
> Revoir/sauvegarder la mise à jour USB avant un futur boot diagnostic ; aucun
> reload/retry dans le noyau courant. Ce document décrit le profil 0.2.0 encore
> présent sur PROBE1401, pas des opérations matérielles déjà réussies.

## Ce qui change

**Ne pas redémarrer l'ancien essai 0.1.2 pour espérer un calcul.** Le bundle
0.2.0 contient le chemin matériel appelé par `Navi48Native::start()` : découverte
IP → bootloader PSP → GMC/GART → PSP/ring/TMR/firmwares → SMU/IMU/RLC → CP/MES/GFX
→ fences → deux dispatchs gfx1201 avec relecture/comparaison de 64 lanes.

Les deux shaders publics épinglés (assembleur et LLVM/HSA) calculent chacun une
rampe de 32 entiers à partir de l'identifiant de lane. Ce sont de **premiers
calculs GPU internes au démarrage**, pas encore une API permettant de soumettre
librement ses programmes. Les `UserClient` restent refusés ; Metal et
WindowServer ne sont pas implémentés. Aucune réussite Radeon n'est présumée
avant ce démarrage et la relecture des diagnostics.

## Périmètre et risque acceptés

Cet essai autorise explicitement des **mappings RW, écritures MMIO/VRAM,
firmwares, changement de puissance/clocks et commandes GPU**. Il ne fabrique
pas les anciens `Claims` et ne retire pas les blockers du contrôleur RO.
L'absence d'un accélérateur et un bail PCI ne prouvent pas une réservation de
VRAM ou une propriété exclusive de tout le matériel. La restauration GPU et le
cycle sommeil/réveil ne sont pas qualifiés : écran noir, panic ou blocage restent
possibles. Une simple redirection vers OPENCORE ne rétablit pas nécessairement
un GPU déjà bloqué ; un arrêt électrique complet peut être nécessaire.

Le scratch expérimental BAR0 est **offset 64 Mio, taille 64 Mio**, dans les
256 Mio visibles. Ce n'est ni une réservation macOS de VRAM ni le buffer RAM de
64 Kio du socle. Le code protège l'allocation entière de la console, vérifie
l'identité/les BAR, refuse un autre accélérateur et vérifie l'origine BAR0/MM
avec des mots uniques. Il refuse une console/base ambiguë plutôt que d'inventer
une adresse MC. Aucun allocateur « high VRAM » hors scratch n'est activé.

`SysMem` utilise des buffers IOKit et les pages IOVM réelles de `IODMACommand`.
Chaque PTE GART est construit depuis sa propre page, pas depuis une adresse
physique CPU ni une contiguïté supposée. Les rings/fences empruntent uniquement
le descripteur CPU original avec sortie DMA 64 bits sans bounce de largeur,
validée sous 48 bits. Le bon fonctionnement DMA Radeon reste à démontrer par
les fences et résultats matériels.

**Après la première opération hardware, aucune libération de mémoire/mapping,
fermeture PCI, désinstallation à chaud ou seconde tentative pendant ce boot.**
Le service/module et les ressources restent retenus jusqu'au reboot, même après
échec/timeout. L'assertion PM empêche la veille automatique/de l'affichage ; elle
ne bloque pas la veille forcée : **ne pas mettre cette session en veille**.

## Bundle prévu

- Produit : `out/native-kext/compute-trial-0.2.0/Navi48Native.kext`, x86_64.
- UUID : `AB1F0B3A-865C-33FC-BC6B-5CA0056BBF45`.
- Exécutable SHA-256 :
  `34ce08473ac2acedd0a9094420cbd4ebf159bf48b21a0134095715cfd95809f9`.
- Dix firmwares et deux shaders vérifiés dans le Mach-O ; signature ad hoc
  stricte ; zéro avertissement. **Cela ne valide pas la liaison/ABI/charge noyau.**
- 321 contrôles service/lifecycle, 162 adaptateur DMA, 29 pool IOVM et 39 accès
  bornés, sur RAM/doubles IOKit avec ASan/UBSan ; pas une simulation réussie de
  shader Radeon. 78 tests Python sur outils/profils, sans accès GPU.

Le profil conserve SMBIOS, patches Ryzen, sécurité et les cinq kexts de
référence ; seuls `Kernel/Add` et les arguments natifs changent. Le double
opt-in de calcul est :

```text
navi48-native-platform=1
navi48-native-scratch-offset=0x4000000
navi48-native-scratch-bytes=0x4000000
navi48-native-compute=1
navi48-native-risk=1
```

Aucun SIP/NVRAM/paramètre de sécurité modifié, aucune installation système,
aucun hot-load et aucun reboot automatique. `OPENCORE` reste la référence.
Les EFI/SMBIOS privés et backups ne sont pas publiés dans Git.

## Premier démarrage et retour

1. Enregistrer le travail. Garder un téléphone prêt pour les dernières lignes
   `Navi48Native: GPU stage=...` ou une panic.
2. **F12 → PROBE1401 UEFI → Tahoe installé**, pas Récupération.
3. Laisser les étapes bornées terminer. Ne pas mettre en veille, ne pas
   décharger/recharger le kext et ne pas ajouter un autre pilote GPU.
4. Si écran noir/blocage/panic : photographier, arrêter la machine ; si nécessaire
   couper complètement l'alimentation quelques secondes. Puis **F12 → OPENCORE**.
   Cette procédure est un retour prévu, pas une restauration GPU déjà testée.
5. Conserver les logs OpenCore et éventuelles panic reports. Ne pas effacer les
   sauvegardes USB ; une EFI.BACKUP n'est pas une entrée BIOS indépendante.

## Vérifications sur le vrai noyau

```sh
sw_vers
uname -r
sysctl -n kern.bootargs
kmutil showloaded | grep -i Navi48Native
ioreg -r -c Navi48Native -l -w 0
```

Vérifier module **0.2.0 / UUID ci-dessus**, les cinq arguments natifs et le
rapport **`Navi48Native,Compute`**, distinct de `Navi48Native,Resources` (RO/DMA
non qualifiés, dont les anciens flags peuvent rester zéro).

Pour un résultat matériel positif, exiger simultanément :

- `HardwareTouched=1`, `FirmwareLoaded=1`, `GPUInitialized=1` ;
- `ComputePassed=1`, `Result=0`, `FailedStage=0`, `Stage=18` ;
- `LanesChecked=64`, `LanesWrong=0` ;
- `AssemblyShader` **et** `LLVMShader` : `IBTestPassed=1`, `FenceLanded=1`,
  quatre `ObservedN` identiques aux quatre `ExpectedN` ; le code vérifie les
  32 lanes de chaque shader, pas seulement les quatre publiées ;
- `ResourcesRetainedUntilReboot=1`. Qualification globale et Metal restent zéro.

Chargement seul, `HardwareTouched=1`, firmware seul, fence seule, ou comparaison
sans fence ne terminent pas les étapes 1/2. Un échec après publication laisse le
service chargé pour conserver les ressources et publier `FailedStage`/`Result`.
Un refus avant la première écriture peut se désattacher/décharger : absence du
nœud ne prouve pas que `start()` n'a jamais été tenté.

| FailedStage | Bloc/condition |
|---|---|
| 1 | opt-in/scratch/console/accélérateur/mappings/PM/bail |
| 2 | RCC/IP discovery/version/MC framebuffer |
| 3 | NBIF/HDP/doorbell et test d'origine BAR0/MM |
| 4 | bootloader SOS |
| 5 | GMC/GART et concordance géométrie |
| 6–9 | PSP, ring, réservation firmware, TMR, RLC/firmwares |
| 10–12 | SMU, IMU, RLC |
| 13–15 | doorbells/GFXHUB, CP, MES |
| 16 | GFX, queue MES, démarrage CP et fences |
| 17 | dispatchs/résultats shader |
| 18 | retour clocks bas après calculs |

Si `FailedStage=18` mais `ComputePassed=1`, les calculs ont passé les fences et
comparaisons, mais le retour de clocks a échoué : session non validée, conserver
les preuves puis revenir à la référence. Ne pas répéter dans ce boot.

## Reproduction, sans chargement

```sh
python3 -B tools/build-native-kext.py \
  --core-build out/native-platform/service-dma \
  --output out/native-kext/<repertoire-neuf>
python3 -B -m unittest discover -s tests/tools -v
python3 -B tools/prepare-native-kext-efi.py \
  --build out/native-kext/boot-diagnostics-0.2.1-final \
  --output out/efi-native/native-compute-<repertoire-neuf> \
  --candidate-offset 0x4000000 --candidate-bytes 0x4000000 \
  --experimental-compute
```

Les sources actuelles produisent **0.2.1**, non déployé ; l'exemple prépare
seulement une nouvelle EFI locale. Il ne doit pas être suivi d'un boot de la
clé 0.2.0 en pensant lire le nouveau diagnostic.

Le déploiement est distinct : `--deploy-probe1401 --replace-native-0.1.2` ne peut
remplacer que le profil 0.1.2/hashes précédemment revus. Après remplacement, le
préparateur refusera de recommencer sans une nouvelle revue de l'état USB.
Rapport de suivi : [native-compute](reports/2026-10-09-native-compute.md).
