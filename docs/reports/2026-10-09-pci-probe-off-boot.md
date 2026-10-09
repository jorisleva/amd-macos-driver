# Premier démarrage OFF — module réellement chargé

**9 octobre 2026, démarrage du noyau à 09:53:12 UTC.**
Tahoe **26.7.1 / 25G241**, Darwin **25.6.0**, sur le Ryzen/Radeon cible.
L'utilisateur confirme le retour au bureau et `navi48-pci-probe=0`.
Les contrôles ci-dessous sont réalisés dans cette nouvelle session, en lecture
seule pour le matériel et les EFI. **La clé n'est pas passée en ON.**

## Résultat observé

| Vérification | Résultat |
| --- | --- |
| Amorçage PROBE1401 / retour au système installé | Réussi pour ce démarrage |
| Argument reçu par le noyau | `navi48-pci-probe=0` |
| Injection OpenCore | `Prelinked injection Navi48PciProbe.kext (...) - Success`, version `0.1.0` |
| Chargement réel du module | Présent dans `kmutil showloaded` |
| Identifiant / version | `com.amd-macos-driver.Navi48PciProbe`, `0.1.0` |
| UUID chargé | `B4232B58-A8D2-3D83-9CBB-5E077EE0627E`, identique à l'exécutable de la clé |
| Dépendances liées affichées | IOPCIFamily, kpi.unsupported, kpi.libkern, kpi.iokit |
| Nœud `Navi48PciProbe` | Aucun, conformément au mode OFF |
| Service IOAccelerator | Aucun observé |
| EFI de PROBE1401 | 104 fichiers toujours identiques au profil OFF préparé |
| EFI habituelle OPENCORE | 101 fichiers inchangés |
| SIP / authenticated root | Activés |

Le résultat ne repose donc **pas seulement sur le boot-arg** ou l'absence
ambiguë d'un service : le module est effectivement chargé dans le noyau.
Le comportement observé est celui attendu pour le premier essai OFF :
**chargement sans attachement ni dictionnaire d'observation**.

Le journal de cette clé est `opencore-2026-10-09-095215.txt`.
Il annonce l'injection réussie à `54:602`, puis la version à `54:674`.
Son empreinte SHA-256 est :
`8110936525da2651f4cae3a7843053cc085d239f1936a86ba467a111b15a0d05`.

## Notification de chargement et avertissement conservé

Le journal unifié contient, à **11:53:20.500 heure locale** :

```text
kernelmanagerd: Received kext load notification: com.amd-macos-driver.Navi48PciProbe
kernelmanager_helper: Could not process load notification in helper: Could not find:
Did not find identifier (com.amd-macos-driver.Navi48PciProbe) in helper to process load notification
```

La seconde ligne est une erreur de traitement de notification : elle est
consignée, **pas présentée comme un journal sans erreur**. Elle n'a pas empêché
la présence observée du module dans `showloaded`. Sa cause exacte n'est pas
établie ici ; aucune installation dans `/Library/Extensions` ou modification
de sécurité n'est tentée pour la faire disparaître.

## Ce que ce premier résultat qualifie — et ne qualifie pas

- Le firmware de ce PC a démarré la clé MBR/FAT32, OpenCore a injecté ce bundle
  et le noyau a chargé sa version 0.1.0 lors de **ce boot OFF**.
- C'est une preuve de chargement via **OpenCore**, pas une qualification de
  l'installation macOS standard, de l'admission par `kmutil load` ou d'une
  collection noyau reconstruite hors ligne.
- Aucun nœud attaché n'est observé avec l'argument 0. Il n'y a pas de trace
  instrumentant chaque appel de `probe()` ni de mesure matérielle de toutes
  les transactions PCI. Le refus avant lecture reste également établi par
  le code et ses tests logiciels, pas par une capture du bus.
- `start()`, la lecture des propriétés de la Radeon et la publication du
  dictionnaire doivent encore être exercés en **ON**. Le chargement OFF ne
  valide pas tous les appels/vtables utilisés par ce chemin.
- Aucun accès BAR, MMIO, DMA, firmware ou calcul GPU n'est testé. Navi48Bringup
  complet, RADV et Metal sous Tahoe restent non qualifiés.
- Un démarrage réussi n'est pas une qualification de stabilité prolongée,
  de veille, de tous les périphériques ou du retour à l'EFI habituelle après panne.

## Suite

**PROBE1401 reste en OFF.** Prochaine action : déployer le profil `02-enabled`
sur cette seule clé, après accord, puis refaire un démarrage contrôlé et
vérifier le nœud et son dictionnaire. Ne pas charger/décharger à chaud.
Suivre le [guide d'essai](../PCI-PROBE-EFI-ESSAI.md).

Les numéros de disques ont changé après le redémarrage : PROBE1401 est
maintenant `disk2s1`, contrairement au `disk3s1` du formatage. Toujours
réidentifier le support par son volume et ses caractéristiques ; ne pas
réutiliser une ancienne commande de formatage ou un numéro diskN mémorisé.

Preuves locales : `out/efi-pci/off-boot-20261009T095312Z/`.
Le journal OpenCore brut reste local ; seuls les extraits pertinents et les
empreintes sont publics. [Rapport JSON](2026-10-09-pci-probe-off-boot.json).
