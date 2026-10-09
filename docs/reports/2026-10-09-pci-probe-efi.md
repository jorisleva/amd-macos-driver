# Préparation EFI d'essai — Navi48PciProbe

**9 octobre 2026, 09:34 UTC — Tahoe 26.7.1 / 25G241, Darwin 25.6.0.**
État : **préparé et copié, non activé, aucun essai de démarrage exécuté**.

## Paquet retenu

```text
Local : out/efi-pci/pci-probe-20261009-essai-c/package/
USB   : /Volumes/OPENCORE/PROFILS-TAHOE/pci-probe-20261009-essai-c/
```

**315 fichiers identiques par SHA-256 dans les deux copies**, dont :

| Profil | Fichiers EFI | Effet prévu au futur démarrage |
| --- | ---: | --- |
| `00-reference/EFI` | 101 | Copie exacte de l'EFI actuelle, sans le module |
| `01-disabled/EFI` | 104 | Injection proposée, coupe-circuit `navi48-pci-probe=0` |
| `02-enabled/EFI` | 104 | Même module, observation autorisée par `navi48-pci-probe=1` |

Le dossier **`/Volumes/OPENCORE/EFI` n'est pas modifié**. Les changements de
configuration constatés lors de la session précédente sont conservés, pas
remplacés par la sauvegarde plus ancienne de 98 fichiers. La nouvelle copie
`00-reference` sauvegarde les 101 fichiers actuels, dont `oldConfig.plist`.
Aucun contenu SMBIOS n'est publié ; **ne pas partager les paquets privés**.

## Modifications strictement limitées

Dans chaque essai, les seuls écarts avec la référence sont :

1. Ajout du bundle signé `Navi48PciProbe.kext` : trois fichiers.
2. Dans `OC/config.plist`, ajout de son entrée `Kernel/Add` et du boot-arg
   OFF ou ON. Toutes les autres valeurs sont comparées à la source.

L'entrée est `Enabled=true` dans les deux profils, `Arch=x86_64`,
`MinKernel=MaxKernel=25.6.0`. Les cinq kexts de référence, patches Ryzen,
identité SMBIOS, langue et réglages de sécurité sont inchangés.
Les coupe-circuits `navi48bringup=0 rdna4-off=1` sont conservés. Aucun firmware,
Navi48Bringup complet ou composant NVIDIA/Apple expérimental n'est ajouté.
Le dossier de référence reste identique même au niveau des octets du plist.

| Élément | SHA-256 |
| --- | --- |
| Config actuelle / référence | `e88ce3555003f97fee228e647114b92ae52a4f40038da14c983d97fed2c27487` |
| Config OFF | `ecb7bf5ed55c9876daf1fe794abe8df5cab76c01eb3fb3a13b5bbfc76ff37aae` |
| Config ON | `8c62c5f6a5e0cc58c873f4028437b8b2cced410865478064f62c3839078e059b` |
| Exécutable du module, inchangé | `8146414908a073e9fe504ca89b285d3ee076f2e327eb606cecaf023d0cd1ca94` |

## Contrôles exécutés

- Le bootstrap et OpenCore actuels correspondent exactement aux binaires
  officiels **1.0.8 DEBUG**, archive épinglée et SHA-256 revérifié.
  Ils ne sont ni mis à jour ni remplacés.
- `ocvalidate` **1.0.8** accepte l'EFI actuelle et les trois profils locaux ;
  les deux profils d'essai sont revérifiés après copie USB.
- Signature ad hoc de notre bundle vérifiée strictement avant copie,
  dans les deux profils locaux, puis dans les deux profils USB.
- Présence des **294 noms de symboles importés** dans le BootKC de cette
  installation : noyau Apple et IOPCIFamily. Aucun nom manquant.
  **Ce contrôle n'effectue pas les relocations et ne valide ni l'ABI des
  classes/vtables, ni l'admission ou le chargement du kext.**
- **44 tests Python réussis**, dont 13 nouveaux tests du préparateur :
  OFF/ON, conservation des réglages, filtres de version/architecture,
  refus d'arguments contradictoires, modifications de sécurité/identité/CPU,
  chemin de dépôt USB, copie des fichiers cachés et analyse des symboles.
- Relecture finale : les **101 fichiers de l'EFI active sont inchangés** ;
  arguments courants inchangés, SIP et authenticated root activés,
  aucun Navi48PciProbe/Navi48Bringup chargé, aucun kext de ce nom dans
  `/Library/Extensions`. Aucun redémarrage, chargement à chaud, reconstruction
  de collection noyau, commande NVRAM ou modification de sécurité effectué.

### Essais de préparation rejetés

- `essai-a` : arrêt local avant les copies EFI ; le parseur attendait le
  préfixe `U`, mais Apple `nm -u` affiche les noms seuls. Support des deux
  formats ajouté avec régression et vérification du nombre de symboles.
- `essai-b` : profils locaux valides, mais copie USB refusée : FAT32 avait
  supprimé 18 fichiers AppleDouble `._` écrits avant leurs fichiers principaux.
  Ce dossier porte **`INCOMPLETE-NE-PAS-UTILISER.txt`**.
- `essai-c` : copie corrigée, avec création des répertoires, fichiers normaux,
  puis fichiers AppleDouble en dernier. Régression simulant ce comportement
  FAT32 et vérification réelle de tous les fichiers réussies. **Seul C est retenu.**

Aucun de ces essais de préparation n'a modifié le dossier EFI actif.
L'ancien outil `kextlibs -arch` a refusé son option malgré son aide ; son
résultat n'est pas utilisé comme preuve. Le relevé des noms repose sur `nm`
du BootKC, pas sur une déclaration de liaison réussie par cet outil.

## Prochaine action concrète

Le [mode d'emploi](../PCI-PROBE-EFI-ESSAI.md) décrit la disposition du support,
le premier essai OFF, l'essai ON, les diagnostics et le retour à la référence.
Le paquet dans **PROFILS-TAHOE n'est pas amorcé par le menu F12** : le disque
habituel continue à charger son EFI inchangée.

Il reste à désigner une seconde clé USB FAT32 pour y copier `01-disabled/EFI`
à la racine, vérifier la copie, puis choisir explicitement ce support au boot.
À défaut, un remplacement temporaire de l'EFI actuelle exigerait une action
séparée et un retour arrière préparé ; rien de tel n'est effectué ici.

L'absence de nœud en OFF n'est pas une preuve suffisante du fonctionnement du
coupe-circuit si le module n'a pas été chargé. Les critères du guide séparent
injection, chargement, attachement et observation. Les futurs résultats doivent
être consignés sans transformer une compilation ou une copie en validation GPU.

Preuves structurées : [rapport JSON](2026-10-09-pci-probe-efi.json).
Outil : `tools/prepare-pci-probe-efi.py`. Aucun outil d'activation automatique.
