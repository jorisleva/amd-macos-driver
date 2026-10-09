# Premier démarrage ON — observation PCI conforme

**9 octobre 2026, noyau démarré à 11:05:25 UTC.**
Tahoe **26.7.1 / 25G241**, Darwin **25.6.0**, Ryzen/Radeon cible.
L'utilisateur transmet un nœud Navi48PciProbe actif avec l'argument **1**.
Le collecteur confirme le résultat sur cette session, sans modifier les EFI,
ouvrir un client GPU ou accéder directement à la configuration PCI/MMIO.

## Résultat

**OFF puis ON observés sur le vrai noyau :** le même module 0.1.0 est chargé
mais non attaché en OFF ; il est chargé, attaché à la Radeon et publie son
dictionnaire en ON. Cela qualifie ce premier chemin d'observation IORegistry,
**pas un pilote GPU accéléré ni l'ensemble de l'étape 3 de la roadmap**.

| Contrôle | Résultat |
| --- | --- |
| Boot-arg réellement reçu | `navi48-pci-probe=1` |
| Injection OpenCore | Succès, version 0.1.0 |
| Chargement noyau | `com.amd-macos-driver.Navi48PciProbe (0.1.0)` présent |
| UUID | `B4232B58-A8D2-3D83-9CBB-5E077EE0627E`, identique au build OFF |
| Nœud observateur | Un seul, enfant de l'IOPCIDevice Radeon de cette session |
| Rapport publié | `Navi48PCI,RegistrySnapshot`, schéma 1, cinq ressources |
| Vendor/device | `1002:7550` |
| Subsystem vendor/device | `1849:5417` |
| Révision / classe | `c0` / `030000` |
| BDF déclaré | Bus 7, device 0, fonction 0 (`PCIBDF=0x00070000`) |
| IOAccelerator | Aucun service observé |
| EFI PROBE1401 / OPENCORE | 104 / 101 fichiers inchangés |
| SIP / authenticated root | Activés |

Les six champs d'identité et **tous les champs des cinq ressources** sont
comparés à un nouveau relevé `IOPCIDevice` du même démarrage, pas seulement
à l'ancien inventaire. La relation parent/enfant est vérifiée par l'identifiant
de l'entrée IORegistry. Aucune propriété du provider n'est écrite.

| Ressource | Base | Longueur | RegistryFlags sur 32 bits |
| --- | --- | --- | --- |
| BAR0 | `0x440000000` | 256 Mio | `0xc2070010` |
| BAR2 | `0x450000000` | 2 Mio | `0xc2070018` |
| BAR4, I/O | `0xe000` | 256 octets | `0x81070020` |
| BAR5 | `0xfcb00000` | 512 Kio | `0x82070024` |
| ROM déclarée | `0xfcb80000` | 128 Kio | `0x82070030` |

**BAR0 décrit une fenêtre PCI, pas la quantité totale de VRAM.** Ces nombres
ne prouvent ni l'accessibilité des plages, ni le contenu de la ROM, ni la
cohérence mémoire CPU/GPU. Les adresses supérieures à 4 Gio sont conservées.

## `!registered`, `!matched` et entiers signés

Le nœud est `active` et indique `IOMatchedAtBoot=Yes`. Les mentions
`!registered`/`!matched` ne signifient pas que la Radeon a été rejetée : le
module ne fait volontairement pas `registerService()` pour proposer son nœud
comme fournisseur d'autres services. Son attachement et le dictionnaire
publié établissent ici le succès du chemin `start()`.

Le rendu texte `ioreg` affiche par exemple `RegistryFlags=18446744072669822992`.
L'export **`ioreg -a`** du même champ contient **`-1039728624`**. Ces représentations
correspondent au masque 32 bits **`0xc2070010`** ; ce n'est pas une taille mémoire.
Le même phénomène concerne les cinq champs RegistryFlags dont le bit 31 vaut 1.

Le contrat utilise des OSNumber **32 bits pour les flags** et **64 bits pour
Base/Length**. Le transport XML/CFNumber expose les valeurs signées. Le nouveau
comparateur `tools/pci_probe_snapshot.py` accepte une représentation signée ou
non signée uniquement dans la largeur déclarée, puis compare tous les bits.
Il **refuse** les valeurs hors largeur au lieu de tronquer arbitrairement ;
il ne perd pas les bits hauts des adresses. Il consomme l'export XML, pas les
grands entiers issus du rendu texte. Aucun correctif ni nouveau build du kext
n'est appliqué pour changer cette présentation.

Une fixture publique nettoyée conserve les valeurs réellement observées :
`tests/tools/fixtures/pci-probe-on-snapshot.json`. Les **52 tests Python** passent,
dont huit nouveaux tests : valeurs XML signées, variante non signée, corruption
de chaque champ, adresse tronquée, types/largeurs invalides, schéma et relevé
provider incorrects. Les tests hôtes du pilote restent distincts de ce contrôle.

## Journaux et limites

OpenCore annonce l'injection réussie à `65:158`, puis la version à `65:243`,
dans `opencore-2026-10-09-110415.txt`. Le journal brut est conservé localement.
`kernelmanagerd` reçoit la notification à **13:05:31.582 heure locale**.
Comme en OFF, `kernelmanager_helper` indique ne pas trouver cet identifiant
pour traiter la notification. L'erreur est conservée ; elle n'a pas empêché
le chargement et l'attachement observés. Sa cause précise n'est pas établie.

La ligne `IOLog` de notre `start()` n'apparaît pas dans l'extrait unifié recueilli.
Sa présence n'est pas inventée : le nœud actif et son dictionnaire sont les
preuves du chemin ON, indépendamment de la rétention de ce message de boot.

Ce premier boot ne qualifie pas les scénarios de pression mémoire, de
concurrence, de veille, de débranchement ou de stabilité prolongée. Les erreurs
et refus testés dans le faux IOKit ne sont pas tous reproduits dans le noyau.
Aucun test matériel sur une autre carte, accès BAR, DMA, firmware, commande GPU,
RADV, Metal ou rendu accéléré n'est exécuté.

## Suite

**PROBE1401 reste en ON et la session actuelle est ON.** Aucun nouveau réglage
n'est appliqué pendant l'observation. OPENCORE habituel reste intact.

Prochaine vérification de boot : revenir via F12 au disque OPENCORE habituel,
puis constater l'absence de l'argument d'essai et du module. Cela vérifiera
le retour à la référence après ON ; ce n'est pas encore un essai de réparation
d'une EFI corrompue ou d'une panne matérielle.

Le prochain chantier de code est l'isolation et le durcissement de la voie
native Navi48 (console, DMA/IOMMU, firmwares, mémoire et commandes), selon
[l'audit initial](2026-10-09-navi48-build-audit.md). Ne pas transformer cet
observateur en pilote GPU complet ni charger directement Navi48Bringup amont.

Preuves locales : `out/efi-pci/on-boot-20261009T110525Z/`.
[Rapport JSON](2026-10-09-pci-probe-on-boot.json) ·
[Guide d'essai](../PCI-PROBE-EFI-ESSAI.md).
