# Premier essai Tahoe sur le PC AMD — 7 octobre 2026

**Le démarrage de la récupération reste bloqué selon le retour utilisateur.**
Deux journaux OpenCore ont été récupérés sur D: OPENCORE. Le premier révèle
une sélection qui relance OpenCore ; le second charge effectivement la
récupération Apple et atteint le passage vers le noyau. Aucun bureau ou
installateur graphique n'est qualifié à ce stade.

## Journaux observés

| Journal | Résultat observable |
| --- | --- |
| `opencore-2026-10-07-191623.txt` | Entrée automatique `OPENCORE`, chemin `EFI/BOOT/BOOTX64.EFI` ; arrêt `Found previous image, aborting` / `Already started` |
| `opencore-2026-10-07-191720.txt` | Entrée Recovery également nommée `OPENCORE` ; DMG chargé, patches et injections réussis, dernier message `#[EB\|LOG:EXITBS:START]` à 19:18:29, heure du firmware |

Le second essai détecte le Ryzen 5600X avec six cœurs et douze threads.
La version de noyau traitée vaut `250600` (Darwin 25.6.0), correspondant à la
récupération 26.6.2 préparée. Les patches AMD applicables sont enregistrés
comme réussis, dont le nombre de cœurs et `MSR 35h` remplacé par `0006000C`.
Les huit kexts sont injectés avec succès ; le résultat global vaut
`OC: Prelinked status - Success`. Cela confirme leur insertion par OpenCore,
sans prouver leur fonctionnement une fois le noyau démarré.

Le firmware annonce `MAT support is 1`. Les paramètres utilisés sont
`SetupVirtualMap=false`, `EnableWriteUnprotector=false`,
`RebuildAppleMemoryMap=true`, `SyncRuntimePermissions=true`,
`DevirtualiseMmio=false` et `ProvideConsoleGop=true`.
Ils correspondent aux vérifications recommandées pour B550 et MAT dans le
[guide Dortania](https://dortania.github.io/OpenCore-Install-Guide/troubleshooting/extended/kernel-issues.html).

La fin à `EXITBS:START` ne permet pas de distinguer un blocage à cette ligne
d'un blocage plus tard dans le noyau, dont les messages ne sont pas conservés
par ce journal OpenCore. Les erreurs de lecture des fichiers de hachage de
la récupération ne sont pas retenues comme cause : le chargement continue
jusqu'à cette transition. Il manque la dernière ligne réellement visible
sur l'écran pour cibler un correctif mémoire, ACPI, PCI, USB ou affichage.

## Correction du menu

Les deux entrées avaient le même nom. Le marqueur
`EFI/BOOT/.contentVisibility` avec le contenu ASCII `Disabled` masque
l'entrée qui relançait OpenCore. Le marqueur
`com.apple.recovery.boot/.contentDetails` donne le nom
`Installer macOS Tahoe (Recovery)` à la récupération. Le firmware peut
toujours amorcer `BOOTx64.EFI` depuis son menu F12.
Le générateur reproduit ces marqueurs dans les prochains kits, y compris
le profil de secours pour la visibilité. Référence :
[documentation OpenCore 1.0.8](https://github.com/acidanthera/OpenCorePkg/blob/1.0.8/Docs/Configuration.tex).

Cette correction traite la sélection ambiguë du premier essai. Elle ne
prétend pas résoudre le blocage signalé pendant le second. Aucun quirk,
argument de démarrage ou kext n'est changé sur cette seule base.

Les trois marqueurs ont été copiés sur D: (visibilité et libellé) et dans
`E:/OpenCore-Secours/EFI/BOOT/` (visibilité), après identification du même
disque USB. Leurs SHA-256 sont conformes après relecture. Les deux
`config.plist` correspondent toujours octet par octet aux références
préparées et passent `ocvalidate` 1.0.8. Le kit avec les marqueurs a également
été reconstruit hors ligne et ses deux configurations validées. Le résultat
visuel du menu corrigé reste à confirmer au prochain démarrage physique.

## Conservation des preuves

Les journaux bruts, leurs copies lisibles sans bourrage NUL et le
`config.plist` effectivement utilisé sont sauvegardés localement sous
`out/diagnostics/tahoe-boot-20261007-192433/`, ignoré par Git. Les journaux
complets et la configuration contiennent des identifiants privés et ne sont
pas ajoutés au dépôt.

SHA-256 des journaux bruts :

```text
53f065a1cb86840aaa20649882c9a7b37fe2b2a7ea4099a83331be6ff610072b  opencore-2026-10-07-191623.txt
c4e5a89ba9b4db5f8c0b021515f33fbd035b96772df06d6c9b8c53b7c1c0ca40  opencore-2026-10-07-191720.txt
```

Pour le prochain essai, sélectionner la récupération explicitement nommée,
conserver une photo lisible des dernières lignes au blocage et noter si
des messages macOS apparaissent après `EXITBS:START`. Le démarrage de base,
le réseau, les périphériques et le secours restent à qualifier avant toute
installation d'un pilote expérimental.
