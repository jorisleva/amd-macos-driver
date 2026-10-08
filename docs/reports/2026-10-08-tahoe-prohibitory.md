# Symbole d'interdiction Tahoe et vérification SMBIOS — 8 octobre 2026

**L'essai de récupération reste non qualifié.** Un nouveau journal situe
le symbole d'interdiction avant le lancement du noyau, lors de son chargement
en mémoire. Un profil normal avec traces Lilu est maintenant actif pour le
prochain essai. Après le profil `-x`,
l'utilisateur transmet une photo du symbole d'interdiction et confirme
avoir choisi `Installer macOS Tahoe (mode sans echec)`. La photo ne montre
pas les messages qui précèdent ce symbole.

## SMBIOS vérifié

La configuration relue sur D: utilise **MacPro7,1** avec `Automatic=true`,
`UpdateDataHub=true`, `UpdateNVRAM=true`, `UpdateSMBIOS=true` et
`UpdateSMBIOSMode=Custom`. Aucune section manuelle DataHub, PlatformNVRAM ou
SMBIOS ne contredit les valeurs générées automatiquement. Les formats des
identifiants personnels sont valides, et la configuration passe les
vérifications du générateur. `SecureBootModel=Disabled`.

Le journal du premier chargement de récupération, le 7 octobre à 19:17, confirme :

```text
OC: New SMBIOS: Acidanthera model MacPro7,1
OC: Setting HW_BID Mac-27AD2F918AE68F61 - Success
AAPL: #[EB|BRD:NV] Mac-27AD2F918AE68F61
```

Apple identifie le [Mac Pro 2019 comme MacPro7,1](https://support.apple.com/fr-fr/102887)
et le déclare [compatible avec macOS Tahoe 26](https://support.apple.com/fr-fr/122867).
Le choix du modèle est donc compatible avec la version visée ; aucune
modification du SMBIOS n'est effectuée. Lors de cette première vérification,
les deux journaux sur la clé étaient identiques octet par octet à ceux
précédemment sauvegardés et ne documentaient pas le nouvel essai.

## Vérifications du support et essai USB

Le même disque USB de test a été identifié avant copie. Le fichier
BaseSystem.dmg relu sur D: conserve son SHA-256 vérifié à la préparation :
`edddd0d5869caaa12e29e6996a04f11590280580976a119dbd42c24fa62fe18e`.
Il n'y a pas d'écart détecté pour cette image. Son accès réussi sous Windows
ne prouve pas sa disponibilité après le passage au noyau macOS.

Le support est actuellement attaché au contrôleur du chipset B550
`1022:43EE`, au port racine 3, chemin ACPI
`\_SB.PCI0.GPP1.PTXH.RHUB.POT3`. L'injecteur provisoire UTBDefault et
USBToolBox sont activés dans la configuration ; la cartographie physique
des prises reste à qualifier.

Le symbole peut correspondre à une erreur d'accès au volume, notamment
USB, dans le contexte d'un installateur OpenCore. C'est une hypothèse,
pas une cause confirmée ; voir le
[guide de diagnostic Dortania](https://dortania.github.io/OpenCore-Install-Guide/troubleshooting/extended/kernel-issues.html#waiting-for-root-device-or-prohibited-sign-error).

Un essai a été activé sur D: avec **un seul changement de configuration**
par rapport au profil `-x` : `UEFI.Quirks.ReleaseUsbOwnership=true`.
Cet essai tente le passage du contrôle USB à macOS. Le SMBIOS, les arguments,
patches, kexts et SSDT sont conservés. Ce réglage est documenté par
[OpenCore 1.0.8](https://github.com/acidanthera/OpenCorePkg/blob/1.0.8/Docs/Configuration.tex)
et proposé par le guide Dortania pour ce type de symptôme.

La configuration copiée et son libellé ont été vérifiés après relecture,
et `ocvalidate` 1.0.8 ne trouve aucun problème. Le menu affiche
**Installer macOS Tahoe (test USB)**. L'utilisateur confirme ensuite le
même symbole d'interdiction avec ce profil, sur un port USB 3 ou en façade.
Le transfert USB seul n'a donc pas résolu cet essai. Les deux journaux relus
sur la clé restent identiques aux anciens fichiers ; aucune nouvelle ligne
du noyau ne permet d'attribuer ce résultat à l'USB, au stockage ou à l'affichage.

Un essai sur **port USB 2.0 arrière direct, sans hub** a ensuite été demandé
avec le même profil, pour isoler le changement de prise. Les trois photos
transmises ensuite et le nouveau journal permettent de préciser l'arrêt
ci-dessous. Le réglage par défaut du générateur reste inchangé.

Un nouveau relevé USBToolBox sous Windows retrouve 14 ports racine pour
`1022:43EE` et huit pour `1022:149C`. Aucun de ces contrôleurs ne dépasse
15 ports dans ce relevé ; cela ne valide pas leur fonctionnement sous macOS.
Le champ `OSBundleRequired=Root` des kexts USB n'est pas à lui seul une
incompatibilité avec `-x` : la
[politique XNU](https://github.com/apple-oss-distributions/xnu/blob/main/libkern/c%2B%2B/OSKext.cpp)
autorise cette valeur en mode sans échec. Aucun Info.plist de kext n'est modifié.

Les deux anciens profils sont sauvegardés sous `D:/PROFILS-TAHOE/` :
`restaurer-safe-x.ps1` revient à l'essai `-x` seul ;
`restaurer-normal.ps1` revient au profil initial.

## Nouveau journal : arrêt du chargeur Apple avant le noyau

Le fichier `opencore-2026-10-08-011117.txt` est désormais présent sur la clé.
Il correspond au libellé **Installer macOS Tahoe (test USB)** des photos.
Le chemin firmware utilise `USB(0x4,0x0)`, différent du premier essai.
La récupération est chargée en RAM, les huit kexts sont injectés et
`OC: Prelinked status - Success` est atteint. Le journal confirme le bon
board-id MacPro7,1 et ces lignes :

```text
OCABC: Valid slides - 88-255
OCABC: Patching safe mode sur-2 at off 5F83
AAPL: #[EB|MBA:OUT] <"... -x slide=137">
AAPL: #[EB.MM.AKM|!] Err(0xE) <- EB.MM.MKP
AAPL: #[EB.LD.DS64|SEG!] __KREMLIN_START
AAPL: #[EB.LD.DS64|!] 0 <- EB.MM.AKMr2 0x454000 0xffffff8000cf8000
AAPL: #[EB|KMA:P] 0x0000000000200000 0x0000000000cf8000
AAPL: #[EB|STOP] 0x16
OCB: StartImage failed - Aborted
```

Il n'atteint pas `EXITBS:START`. Ce symptôme est antérieur au blocage
CoreAnalytics observé en mode normal : le noyau ne démarre pas lors de
cet essai `-x`. Les adresses restent à la base non décalée malgré l'argument
`slide=137`. Cela oriente vers le chargement mémoire en mode sans échec ;
la raison exacte de l'échec d'allocation reste à confirmer. Le
[guide OpenCore 1.0.8](https://github.com/acidanthera/OpenCorePkg/blob/1.0.8/Docs/Configuration.tex)
explique que le mode sans échec force normalement un slide nul et que
`EnableSafeModeSlide` tente de lever cette restriction. Le journal montre
que ce patch a été appliqué, ce qui ne prouve pas son effet sur cette build.

Le profil actif retire **`-x`** et ajoute **`-liludbgall`** aux arguments.
Le second active les traces Lilu et de ses plugins dans les versions DEBUG,
selon la [documentation Lilu 1.7.2](https://github.com/acidanthera/Lilu/blob/1.7.2/README.md).
MacPro7,1, `ReleaseUsbOwnership=true`, les autres quirks, les patches,
kexts et SSDT sont conservés. Aucun slide fixe ni bloc de relocation
n'est ajouté. Le différentiel structuré ne contient que ce champ boot-args.
Les invariants du générateur et `ocvalidate` passent ; la configuration et
son libellé sont relus et comparés après copie sur la clé.

Choisir désormais **Installer macOS Tahoe (diagnostic normal)**, en gardant
la prise utilisée pour le dernier essai. Le but est de franchir cet arrêt
du chargeur puis de documenter le résultat, y compris un éventuel retour
au blocage CoreAnalytics. Le résultat matériel reste à relever.
`D:/PROFILS-TAHOE/restaurer-usb-test.ps1` restaure le profil `-x` avec
transfert USB, sauvegardé avant cette modification.

Les photos, configurations privées, relevés et preuves de copie restent
sous `out/diagnostics/tahoe-prohibitory-20261008/` et
`out/diagnostics/tahoe-usb-trial-failed-20261008/`, ainsi que
`out/diagnostics/tahoe-booter-abort-20261008/`, ignorés par Git.
