# Symbole d'interdiction Tahoe et vérification SMBIOS — 8 octobre 2026

**L'essai de récupération reste non qualifié.** Après le profil `-x`,
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

Le dernier journal disponible, celui du 7 octobre à 19:17, confirme :

```text
OC: New SMBIOS: Acidanthera model MacPro7,1
OC: Setting HW_BID Mac-27AD2F918AE68F61 - Success
AAPL: #[EB|BRD:NV] Mac-27AD2F918AE68F61
```

Apple identifie le [Mac Pro 2019 comme MacPro7,1](https://support.apple.com/fr-fr/102887)
et le déclare [compatible avec macOS Tahoe 26](https://support.apple.com/fr-fr/122867).
Le choix du modèle est donc compatible avec la version visée ; aucune
modification du SMBIOS n'est effectuée. Cela ne prouve pas la cause du
symbole du dernier essai : les deux journaux sur la clé sont identiques
octet par octet à ceux précédemment sauvegardés et ne documentent pas ce
nouvel essai.

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
et `ocvalidate` 1.0.8 ne trouve aucun problème. Le menu doit maintenant
afficher **Installer macOS Tahoe (test USB)**. Le résultat physique de cet
essai reste à relever ; le réglage par défaut du générateur reste inchangé.

Tester d'abord le même port USB. Si le symbole persiste, essayer un port
USB 2.0 arrière direct, sans hub, et relever les messages avant le symbole.
Les deux anciens profils sont sauvegardés sous `D:/PROFILS-TAHOE/` :
`restaurer-safe-x.ps1` revient à l'essai `-x` seul ;
`restaurer-normal.ps1` revient au profil initial.

Les photos, configurations privées, relevés et preuves de copie restent
sous `out/diagnostics/tahoe-prohibitory-20261008/`, ignoré par Git.
