# Essai natif 0.2.4 : initialisation, calcul Radeon et adoption déclarée

> **Boot 0.2.4 effectué le 10 octobre à 14:30:29 UTC.** Module/UUID/arguments
> conformes. **Adoption des 3 BAR (`DescriptorOrigin=1`) + DMA préparée**,
> puis refus logiciel au préflight accélérateur (`FailedStage=1`,
> `PreflightCheck=7`), aucun hardware.
> [Boot 0.2.4](reports/2026-10-10-native-0.2.4-boot.md).
> Prochaine action : corriger le préflight (itérateur null = cas nominal),
> sans autoriser un concurrent. Aucun retry/reload dans le noyau courant.
> [Décodage BootDiagnostics](reports/2026-10-10-native-boot-diagnostics.md).

## Ce qui change

**Ne pas redémarrer l'ancien essai 0.1.2 pour espérer un calcul.** Le bundle
0.2.1 conserve le chemin matériel de 0.2.0 appelé par `Navi48Native::start()` : découverte
IP → bootloader PSP → GMC/GART → PSP/ring/TMR/firmwares → SMU/IMU/RLC → CP/MES/GFX
→ fences → deux dispatchs gfx1201 avec relecture/comparaison de 64 lanes.

Le correctif ajoute `Navi48Native,BootDiagnostics` dans IOResources : diagnostic
synthétique destiné à survivre au refus/retrait du service et au buffer dmesg
écrasé. Valeurs génériques uniquement, pas de référence au service/provider.
Best effort sur OOM, pas d'historique de toutes les tentatives ; persistance
noyau encore à observer. Aucun garde/Claim/chemin de commande GPU changé.

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

## Bundle déployé pour le prochain boot

- Produit : `out/native-kext/declared-adoption-0.2.4/Navi48Native.kext`, x86_64.
- Exécutable SHA-256 :
  `9fe464d7cffac6c0cdd2b7dcf77244cad96d05d1c726b2059f8fc0fead2bef9e`.
- Dix firmwares et deux shaders vérifiés dans le Mach-O ; signature ad hoc
  stricte ; zéro avertissement. **Cela ne valide pas la liaison/ABI/charge noyau.**
- 3 202 contrôles contrôleur, 1 311 service/lifecycle, 162 adaptateur DMA,
  29 pool IOVM et 39 accès bornés, sur RAM/doubles IOKit avec ASan/UBSan ;
  pas une simulation réussie de shader Radeon. 86 tests Python sur
  outils/profils/remplacements, sans accès GPU.

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
ioreg -r -c IOResources -l -w 0 | grep 'Navi48Native,BootDiagnostics'
ioreg -r -c Navi48Native -l -w 0
```

Vérifier module **0.2.4 / UUID 4F894D5F-FDC9-384C-8688-C6B4BAA68674**, les cinq arguments natifs.
Même si le service se retire, chercher **`Navi48Native,BootDiagnostics`** dans
IOResources. Lire `PlatformObserved`, `DMAObserved`, `ComputeObserved` avant
Checkpoint/PlatformDecision/FailedBar/DMAResult/FailedStage/PreflightCheck :
les valeurs par défaut d'un bloc non observé ne sont pas des preuves hardware.
Si l'acquisition réussit, exiger **`DescriptorOrigin=1`** sur les BAR partagés,
puis les rapports Resources/Compute avec les critères du protocole. Si refus,
relever **`MapCheck`/`DescriptorOrigin`** et les valeurs observées. Si la
publication best effort manque,
capturer rapidement `sudo dmesg` dans Terminal, jamais transmettre le mot de
passe. Ne pas retry/reload dans ce boot.

Le rapport complet **`Navi48Native,Compute`** reste requis pour les preuves de
calculs, distinct de `Navi48Native,Resources` (RO/DMA non qualifiés, dont les
anciens flags peuvent rester zéro) et du diagnostic synthétique BootDiagnostics.

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
  --build out/native-kext/descriptor-identity-0.2.3 \
  --output out/efi-native/native-compute-<repertoire-neuf> \
  --candidate-offset 0x4000000 --candidate-bytes 0x4000000 \
  --experimental-compute
```

Les sources produisent **0.2.3**, maintenant déployé ; ces commandes ne chargent
pas le pilote. La préparation EFI locale seule exige un boot de référence.

Le remplacement déjà effectué a utilisé `--deploy-probe1401
--experimental-compute --replace-native-0.2.3` : profil/hashes 0.2.3 exacts,
64+64 Mio, build 0.2.4 vérifié. Une exception de **copie hors ligne seulement**
autorise le boot retiré/version/UUID/arguments exacts avec zéro instance native,
pas une reprise GPU. Les anciennes options restent limitées à leurs sources
respectives (`0.2.0 → 0.2.1 → 0.2.2 → 0.2.3 → 0.2.4`).
**La clé est désormais en 0.2.4 : ne pas rejouer ces options de remplacement**,
qui refuseront cet état sans nouvelle revue. OPENCORE et backups préservés.
[Déploiement actuel](reports/2026-10-10-native-adoption-deployment.md) ·
[Déploiement 0.2.3](reports/2026-10-10-native-descriptor-deployment.md) ·
[Préparation historique 0.2.0](reports/2026-10-09-native-compute.md).
