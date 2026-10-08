# Premier démarrage Tahoe — Ryzen 5600X / B550M DS3H / RX 9070 XT

Profil préparé le 7 octobre 2026 pour **la machine relevée sous Windows**.
Objectif : atteindre l'installateur puis un bureau avec affichage de base,
sans accélération Metal et sans installer le pilote du projet.
**Le démarrage matériel n'est pas encore qualifié.** Une configuration OpenCore
valide ne prouve pas que le framebuffer EFI de cette RX 9070 XT sera repris
correctement par Tahoe. Si l'écran devient noir, le relevé du dernier message
et le connecteur utilisé permettront de traiter ce blocage.

## Machine et fichiers

| Élément | Configuration relevée / choix |
| --- | --- |
| CPU | Ryzen 5 5600X, 6 cœurs physiques / 12 processeurs logiques |
| Carte mère / BIOS | Gigabyte B550M DS3H, FD du 22 mars 2024 ; révision PCB à relever |
| Mémoire | 16 Gio, deux modules à 3200 MT/s |
| GPU | RX 9070 XT `1002:7550`, sous-système `1849:5417` ; ASRock, VBIOS `023.008.000.068.000001` selon ACPI VFCT ; modèle commercial à relever |
| Stockage présent | Crucial P3 Plus CT1000P3PSSD8, 1 To, firmware P9CR413, contrôleur NVMe `1344:5416` |
| SATA | Contrôleur AHCI AMD `1022:43EB` ; aucun RAID observé |
| Réseau | Ethernet Realtek `10EC:8168` ; RealtekRTL8111 3.0.0 inclus |
| USB | AMD `1022:43EE` : 14 ports racine ; `1022:149C` : 8, selon USBToolBox sous Windows |
| OS visé | Récupération Tahoe 26.6.2 / 25G83 ; paquet complet 26.7.1 / 25G241 disponible ; relever le build réellement installé |
| Identité SMBIOS | MacPro7,1 ; identité générée localement, jamais ajoutée à Git |

Le SSD présent est un **inventaire**, pas une sélection de disque à effacer.
Le support USB Hitachi HTS545050A7E de 500 Go, anciennement D:, est préparé.
La partition d'installation sur le SSD reste à désigner. L'écran LG est
branché en DisplayPort sur la Radeon ; son modèle exact reste à relever.
Le 5600X n'a pas d'iGPU : brancher l'écran sur la RX 9070 XT, pas sur la carte mère.

La table VFCT expose une image ATOMBIOS de 58 880 octets pour `1002:7550`,
part number `113-APM107819-101` et chaîne `ASRock Navi48 XTX G292 16GB 304W`.
Ce relevé identifie le VBIOS fourni par le firmware ; il ne constitue pas
une sauvegarde complète de la ROM PCI utilisable pour flasher la carte.
Reproduction en lecture seule : `python tools/collect-acpi-vbios.py`.

Le générateur Windows écrit uniquement dans le répertoire ignoré `out/` du
projet. Il ne monte aucune partition EFI et ne copie rien sur un disque USB.

```powershell
python tools/build-opencore-kit.py
# Reproduction sans réseau, une fois les téléchargements en cache :
python tools/build-opencore-kit.py --offline --output out/opencore/reproduction
```

Le profil par défaut utilise désormais l'USB natif : aucun USBToolBox,
UTBDefault ou USBX n'est injecté. `--usb-mode toolbox` reproduit l'ancien
profil provisoire pour un essai explicitement choisi ; il ne fournit pas
une cartographie physique validée.

Le mode graphique par défaut, `--graphics-mode firmware`, ne copie aucun
kext graphique et n'injecte aucune propriété GPU. `--graphics-mode whatevergreen`
reproduit le précédent essai WhateverGreen avec `-radvesa` et `agdpmod=pikera`.
Le nom « firmware » exprime l'objectif de reprise de l'affichage initial ;
il ne garantit pas que Tahoe le conservera.

Résultat par défaut : `out/opencore/ryzen5600x-b550-rx9070xt/` et l'archive
voisine `.zip`. Dans le kit :

- `EFI/` : profil sans kext graphique ajouté, menu OpenCore avec choix manuel.
- `recovery/EFI/` : même identité et mêmes composants, avec `-x` (mode sans échec).
- `com.apple.recovery.boot/.contentDetails` : libellé de la récupération,
  à copier avec les fichiers Apple sur le support USB ; aucun DMG dans le kit.
- `validation/` : journaux de compilation ACPI, deux validations `ocvalidate`
  et vérification des invariants du profil.
- `SHA256SUMS.json`, `machine-profile.json`, `NOTICES/` : empreintes,
  inventaire sans numéros de série matériels et provenance des composants.

Le kit complet contient des identifiants SMBIOS personnels : **conserver
l'archive localement**. Le dépôt contient le générateur, le profil et les
versions épinglées. Le builder conserve l'identité dans
`out/opencore/private-identity.json` pour les reconstructions et refuse
d'écraser un kit existant.

## Réglages retenus

Le 7 octobre, le support USB demandé a été repartitionné en GPT après
sauvegarde vérifiée de l'ancien installateur Umbrel. **D: OPENCORE**, FAT32,
8 Gio, contient `EFI/` et `com.apple.recovery.boot/`. **E: TAHOEFILES**, exFAT,
contient le paquet complet et `OpenCore-Secours/EFI/` avec `-x`.
Les empreintes ont été relues sur le support et les deux EFI copiés passent
`ocvalidate`. Le SSD Windows n'a pas été modifié par cette préparation.
Preuve : [rapport USB et shaders Radeon](reports/2026-10-07-metal-graphics-radeon.md).

La récupération Apple 26.6.2 / 25G83 est amorçable avec cette disposition,
mais son installation nécessite Ethernet et Internet. Le paquet complet
26.7.1 / 25G241 sur E: sert au transfert vers le Mac pour préparer un
installateur hors ligne ; il n'est pas utilisé hors ligne par la récupération.
Le démarrage physique et l'affichage de base restent à qualifier.

Les deux essais USB du 7 octobre sont maintenant relevés : le premier a
sélectionné une entrée qui relançait OpenCore, le second a chargé la récupération
jusqu'à `EXITBS:START`. Ce dernier message est une frontière de journalisation,
pas à lui seul la preuve de la cause du blocage signalé. La photo transmise
ensuite montre le noyau jusqu'à `CoreAnalyticsHub start completed`, sans
progression pendant plus de cinq minutes en DisplayPort.
Voir le [diagnostic](reports/2026-10-07-tahoe-boot.md).

Le menu corrigé masque cette entrée OpenCore avec
`EFI/BOOT/.contentVisibility` contenant `Disabled`, et nomme la récupération
**Installer macOS Tahoe (Recovery)** avec
`com.apple.recovery.boot/.contentDetails`. Ces marqueurs sont documentés par
[OpenCore 1.0.8](https://github.com/acidanthera/OpenCorePkg/blob/1.0.8/Docs/Configuration.tex).
Ils ne modifient pas le fichier `config.plist` ni le démarrage USB depuis F12.

Le journal `opencore-2026-10-08-011117.txt` documente un arrêt du chargeur
avec `-x`. Le retour au mode normal atteint de nouveau CoreAnalytics puis
bloque ; l'utilisateur observe l'extinction de la LED du disque USB et un
clavier qui ne répond plus. Ces symptômes ne suffisent pas à établir la cause.

À la demande de l'utilisateur, **D: retire maintenant les ajouts USB** :
USBToolBox et UTBDefault sont sortis de `EFI/OC/Kexts` et de `Kernel/Add` ;
`SSDT-EC-USBX` est remplacé par `SSDT-EC`, conservant le même faux EC sans
USBX. Les arguments USBToolBox sont retirés et `ReleaseUsbOwnership=false`.
Le SMBIOS **MacPro7,1**, le mode normal et les traces Lilu sont conservés.
Cet essai bloque encore, selon le retour utilisateur. Le nouvel essai
**Installer macOS Tahoe (sans kext graphique)** conserve le retrait USB et
retire aussi WhateverGreen de Kernel/Add et du dossier Kexts, avec ses deux
arguments `-radvesa` et `agdpmod=pikera`. Les cinq composants restants sont
Lilu, VirtualSMC, RestrictEvents, AppleMCEReporterDisabler et RealtekRTL8111.
Lilu est une dépendance déclarée de VirtualSMC et RestrictEvents.
Les paramètres mémoire, CPU, SMBIOS et GOP restent identiques. La configuration
copiée et les fichiers restants sont validés ; le résultat matériel reste à qualifier.
Voir le [relevé du 8 octobre](reports/2026-10-08-tahoe-prohibitory.md).
Le profil précédent, sans ajouts USB mais avec WhateverGreen, se restaure avec :

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File D:\PROFILS-TAHOE\restaurer-avant-retrait-graphique.ps1
```

Le profil antérieur et ses fichiers USB se restaurent ensemble avec :

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File D:\PROFILS-TAHOE\restaurer-avant-retrait-usb.ps1
```

Ce second script restaure aussi WhateverGreen s'il manque. Les anciens scripts
qui restaurent uniquement `config.plist` nécessitent d'abord les restaurations
complètes correspondantes, sinon ils référencent des kexts absents.

OpenCore **1.0.8 DEBUG**, Lilu **1.7.2**, VirtualSMC **1.3.8**,
RestrictEvents **1.1.6**, RealtekRTL8111 **3.0.0** et disabler
AppleMCEReporter codeless **1.2**. USBToolBox **1.2.0** et son UTBDefault
restent épinglés pour l'option `toolbox`, sans être inclus dans l'EFI native.
WhateverGreen **1.7.1** reste épinglé pour l'option `whatevergreen`, sans être
inclus dans l'EFI active.
Les archives officielles sont vérifiées par SHA-256 avant extraction.
Les versions, notices et sources sont dans
[`boot/ryzen5600x-b550/sources.lock.json`](../boot/ryzen5600x-b550/sources.lock.json).

Les patches [AMD_Vanilla](https://github.com/AMD-OSX/AMD_Vanilla/blob/eaf52ef292abf4ebec899df6d48626569ba50cc6/README.md)
incluent Tahoe. Les quatre remplacements du nombre de cœurs valent **6**, pas 12.
Le générateur conserve les plages Darwin et la seule variante PAT activée en
amont (algrey), active `ProvideCurrentCpuInfo` et `DummyPowerManagement`.
Pour cette B550, `SetupVirtualMap=false`, `EnableWriteUnprotector=false`,
`RebuildAppleMemoryMap=true` et `SyncRuntimePermissions=true`.
Référence : [configuration Ryzen de Dortania](https://dortania.github.io/OpenCore-Install-Guide/AMD/zen.html).

Deux SSDT sont compilés par iASL **20250807** :

- `SSDT-EC` utilise le pont LPC réellement trouvé, `\_SB.PCI0.SBRG`,
  et expose le même EC fictif uniquement sous Darwin, sans propriétés USBX.
  Aucun contrôleur PNP0C09 n'est présent dans le DSDT capturé.
- `SSDT-CPUR` utilise les douze chemins `\_SB.PLTF.C000` à `C00B`
  confirmés par les propriétés PnP Windows. Il expose les définitions Processor
  attendues par macOS sur cette B550, uniquement sous Darwin.

Le checksum du DSDT BIOS FD est enregistré dans le profil. Windows ne permet
pas ici de distinguer toutes les tables SSDT homonymes par
`GetSystemFirmwareTable` : le relevé n'est pas un dump exhaustif du firmware.
Les éventuelles collisions ACPI doivent donc être contrôlées au premier boot.
La compilation EC seul n'émet aucun avertissement. CPUR émet douze
avertissements iASL 3168 pour `Processor()`, une syntaxe ancienne volontairement
utilisée pour la compatibilité macOS de ce correctif B550, et un message
« No parent method » lié aux retours de références du modèle CPUR amont.
Les deux tables compilent sans erreur ; cette preuve reste distincte d'un boot.

Les ports de chaque contrôleur restent sous la limite de 15 dans le relevé
Windows. Le profil d'installation ne contient aucune carte USB injectée ;
clavier et stockage USB restent à qualifier. `XhciPortLimit=false`.
La cartographie prise par prise et les types de connecteurs sont différés.
L'ancien profil `toolbox` utilise UTBDefault pour l'énumération provisoire,
sans constituer une cartographie validée.
Référence : [procédure USBToolBox](https://github.com/USBToolBox/kext/tree/1.2.0).

Arguments de démarrage :

```text
-v keepsyms=1 debug=0x100 navi48bringup=0 rdna4-off=1
```

Le nouvel essai ne contient aucun kext graphique ajouté ni ses arguments.
Le retrait de WhateverGreen ne supprime pas les pilotes graphiques intégrés
à macOS ; aucun blocage de ces pilotes n'est ajouté sans preuve de leur rôle.
Dans l'ancien mode `whatevergreen`, `-radvesa` désactive l'accélération AMD via
[WhateverGreen](https://github.com/acidanthera/WhateverGreen/blob/1.7.1/README.md).
Cet argument n'a plus de consommateur lorsque ce kext est absent.
`navi48bringup=0` refuse le probe du kext Navi48 à notre révision ;
`rdna4-off=1` est le coupe-circuit documenté de RDNA4FB.
Aucun de ces deux kexts n'est inclus. Il n'y a aucun spoof PCI de la Radeon.
Ne pas ajouter `-wegnoegpu` : cela désactiverait l'unique GPU d'affichage.

Le menu utilise le GOP existant, demande 1920 × 1080 sans forcer un mode
non disponible et conserve la résolution firmware si nécessaire. SIP reste
activé, SecureBootModel OpenCore est désactivé pour ce profil initial,
les DMG doivent être signés et les journaux sont écrits sur le support EFI.
La sélection ne lance pas d'OS automatiquement et OpenCore ne s'enregistre
pas comme démarrage firmware par défaut (`LauncherOption=Disabled`).
`NVRAM.WriteFlash=false` limite la persistance des variables injectées ;
le démarrage effectif d'un OS peut néanmoins modifier la NVRAM.

## Prochaines étapes sur le PC, dans l'ordre

1. **Désigner la partition ou le disque de test.** Le support USB est prêt ;
   préférer un disque dédié pour Tahoe. Ne pas effacer le Crucial Windows sur la seule base de ce profil.
   Conserver une copie du kit EFI de référence séparée du support d'essai.
2. **Conserver les versions préparées et choisir la méthode d'installation.**
   La récupération USB est 26.6.2 / 25G83, le paquet complet est 26.7.1 / 25G241. Un
   installateur complet créé sur le Mac évite le téléchargement pendant
   l'installation ; à défaut, OpenCore livre `Utilities/macrecovery` pour
   une récupération Internet signée. Un téléchargement « latest » n'est pas
   une version figée : relever la version/build effectivement obtenue avant
   de qualifier l'essai. Le kit généré seul ne contient aucun installateur ;
   le support USB préparé contient désormais la récupération Apple séparée.
3. **Vérifier les réglages UEFI sans flasher le BIOS.** CSM désactivé,
   démarrage UEFI, Fast Boot désactivé, Secure Boot firmware désactivé,
   Above 4G Decoding activé, XHCI Hand-off activé si l'option existe,
   SATA en AHCI. Désactiver Re-Size BAR pour le premier essai ; le profil
   contient aussi `ResizeAppleGpuBars=0` pour macOS. Conserver le TPM et
   disposer de la clé de récupération Windows si BitLocker est actif avant
   les changements de Secure Boot/UEFI. Relever la révision PCB sur la carte.
4. **Utiliser `EFI/` déjà copié sur D: OPENCORE**, validé après copie.
   Pour reconstruire un autre support, identifier d'abord la clé avant copie.
   Utiliser le menu Gigabyte **F12**
   pour ce premier démarrage et sélectionner la clé en UEFI. Le générateur
   ne réalise pas cette copie et n'altère pas l'EFI Windows.
5. **Choisir l'entrée de récupération dans le menu OpenCore.** Pour l'essai
   actif sans `-x`, sans kext graphique ajouté, sans kexts USB ni USBX et avec
   traces Lilu, elle est nommée **Installer macOS Tahoe (sans kext graphique)** ;
   le profil normal affiche **Installer macOS Tahoe (Recovery)**. Un seul écran directement
   connecté à la Radeon, clavier USB filaire et câble Ethernet. Si un blocage
   survient : relever le dernier message, le connecteur, le build Tahoe et
   conserver le journal `opencore-*.txt` de la clé. Ne pas activer de pilote
   expérimental pour contourner un premier blocage de démarrage.
6. **Qualifier la référence.** Vérifier écran, clavier/souris, disque cible,
   Ethernet puis trois démarrages à froid et redémarrages. Sous macOS,
   conserver `sw_vers`, l'inventaire PCI/IORegistry et les journaux ; le
   rapport doit dire explicitement que l'accélération n'est pas présente.
7. **Essayer le secours avant les kexts du projet.** Depuis Windows ou une
   seconde clé, remplacer le dossier EFI de la clé d'essai par
   `recovery/EFI/` pour essayer `-x`. Le menu F12 permet toujours de sélectionner
   Windows directement. Les coupe-circuits valent uniquement pour les
   révisions de Navi48/RDNA4FB documentées : les revalider avant une mise à jour.

Pour restaurer le profil normal, remettre le dossier `EFI/` de référence.
Le mode sans échec peut réduire les services disponibles, dont le réseau ;
ce profil est un moyen de diagnostic à tester, pas un secours déjà prouvé.
Une panne causée par un kext installé dans le système peut aussi nécessiter
le retrait de ce kext et la reconstruction de l'AuxKC depuis Recovery ;
documenter ce retour arrière **avant** toute future installation.

La première qualification reste l'étape **3** de la ROADMAP : démarrage,
identification et récupération. L'étape **6** de la ROADMAP concerne Metal
et reste indépendante de ce numéro de point opérationnel.
