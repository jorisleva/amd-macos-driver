# Déploiement du premier essai PCI sur PROBE1401

**9 octobre 2026, 09:46 UTC.** État : **profil OFF copié et vérifié sur la clé,
aucun redémarrage ni essai noyau exécuté**.

## Autorisation et identification

L'utilisateur a explicitement autorisé le formatage de **PROBE1401 sans
sauvegarde**, puis l'ajout de l'EFI avec notre module. Aucun ancien fichier
de cette clé n'est sauvegardé.

La cible est identifiée comme une **Flash Disk USB amovible de
15 634 268 160 octets**, alors `/dev/disk3`. Son volume PROBE1401 occupait
seulement 305 Mo, avec le reste non partitionné. L'ancien UUID de volume,
le disque parent, le nom, la capacité, le caractère amovible et la présence
d'une seule partition sont recontrôlés juste avant l'effacement.

Le disque OPENCORE/TAHOEFILES de 500 Go (`disk2`) et le SSD système/Windows
ne sont pas la cible. **Les numéros diskN ne sont valables que pour cette
session : ne pas réutiliser disk3 comme instruction de formatage.**

## Écriture effectuée

- Table **MBR**, une partition **FAT32 PROBE1401** sur la capacité de la clé.
- Copie exacte de `pci-probe-20261009-essai-c/package/01-disabled/EFI` vers
  **`/Volumes/PROBE1401/EFI`**, soit **104 fichiers**, AppleDouble compris.
- Le chemin UEFI de démarrage est `EFI/BOOT/BOOTx64.efi` ; aucune inscription
  dans la NVRAM de boot, aucun `bless` ou changement d'ordre de démarrage.
- Notre kext figure dans `Kernel/Add` avec `Enabled=true`, mais le boot-arg
  **`navi48-pci-probe=0`** maintient son refus d'attachement pour le premier essai.
- OpenCore 1.0.8 DEBUG et les cinq kexts habituels sont conservés ; pas de
  Navi48Bringup complet. Les coupe-circuits existants et la sécurité ne changent pas.
- `LIRE-ESSAI-OFF.txt` et `EFI-SHA256SUMS.json` sont ajoutés à la racine de
  la clé, hors du dossier EFI.

## Vérifications après écriture

Après synchronisation, **démontage puis remontage** de PROBE1401 :

- les 104 fichiers correspondent tous aux empreintes de la source préparée ;
- `ocvalidate` 1.0.8 : **aucune erreur** sur le plist de la clé ;
- `codesign --verify --strict` : signature du module valide sur la clé ;
- les **101 fichiers de l'EFI OPENCORE habituelle sont inchangés** ;
- arguments de la session courante inchangés, sans `navi48-pci-probe` ;
- SIP et authenticated root activés ; aucun Navi48PciProbe/Navi48Bringup chargé.

Config OFF SHA-256 :
`ecb7bf5ed55c9876daf1fe794abe8df5cab76c01eb3fb3a13b5bbfc76ff37aae`.

Exécutable du module SHA-256 :
`8146414908a073e9fe504ca89b285d3ee076f2e327eb606cecaf023d0cd1ca94`.

Le formatage et les validations réussis **ne prouvent pas que le firmware
amorcera cette clé ni que le noyau chargera le module**. Ce sont les essais suivants.

## Prochaine action utilisateur

1. Garder le disque OPENCORE habituel disponible pour revenir à la référence.
2. Redémarrer au moment choisi, **F12**, sélectionner la petite clé en **UEFI**
   (nom firmware possible **Flash Disk**, pas forcément le libellé PROBE1401).
3. Dans OpenCore, choisir **Macintosh HD / Tahoe installé**, pas l'installation
   ou la récupération, et ne pas lancer Reset NVRAM.
4. Au bureau, relever `sysctl -n kern.bootargs` : il doit contenir
   **`navi48-pci-probe=0`**. Conserver les `opencore-*.txt` de PROBE1401 et
   poursuivre les contrôles du [guide](../PCI-PROBE-EFI-ESSAI.md).
5. En cas de blocage, photographier les dernières lignes et revenir via F12
   au disque OPENCORE habituel. Ne pas passer au profil ON sans revue du résultat.

L'absence du nœud en OFF ne prouve pas à elle seule le chargement du module.
Aucune accélération graphique n'est attendue. La copie EFI contient le SMBIOS
privé : **ne pas publier les plists ou le dossier EFI**.

Journaux et empreintes locaux :
`out/efi-pci/deploy-probe1401-20261009T094513Z/`.
Preuves publiques sans identifiants privés :
[rapport JSON](2026-10-09-probe1401-deployment.json).
