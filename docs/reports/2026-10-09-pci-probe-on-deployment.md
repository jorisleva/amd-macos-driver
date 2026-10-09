# Passage de PROBE1401 au profil ON

**9 octobre 2026, 10:30 UTC.** L'utilisateur autorise le passage à ON après
le [premier démarrage OFF observé](2026-10-09-pci-probe-off-boot.md).
État : **ON déployé et vérifié sur la clé, redémarrage ON non exécuté**.

## Changement effectué

La clé est réidentifiée par son nom PROBE1401, son UUID, son support USB
amovible Flash Disk de 15 634 268 160 octets et son disque parent, distinct
d'OPENCORE. Aucun ancien numéro diskN n'est repris pour la cibler.

L'EFI complète du profil `02-enabled` est préparée sur la clé, validée, puis
substituée à l'EFI OFF par renommages sur le même volume, **sans fusion**.
Les 104 fichiers correspondent au profil ON préparé. Le seul fichier différent
entre OFF et ON est `OC/config.plist` ; sa seule modification de valeur est :

```text
navi48-pci-probe=0  →  navi48-pci-probe=1
```

Le même exécutable 0.1.0 est conservé. Aucun firmware ou pilote GPU complet
n'est ajouté ; `navi48bringup=0 rdna4-off=1` restent présents.

- Config ON SHA-256 :
  `8c62c5f6a5e0cc58c873f4028437b8b2cced410865478064f62c3839078e059b`.
- Exécutable SHA-256 :
  `8146414908a073e9fe504ca89b285d3ee076f2e327eb606cecaf023d0cd1ca94`.

## Conservation du premier essai et contrôles

L'EFI OFF testée est conservée intégralement :

```text
PROBE1401/PROFILS-TAHOE/avant-on-20261009T102938Z/EFI/
out/efi-pci/enable-probe1401-20261009T102938Z/off-backup/EFI/
```

Le journal `opencore-2026-10-09-095215.txt` reste à la racine de la clé. Une copie
USB est conservée dans `avant-on-20261009T102938Z/`, et une copie locale dans
le dossier de preuves `enable-probe1401-20261009T102938Z/`. Elles sont
vérifiées ; sa SHA-256 reste
`8110936525da2651f4cae3a7843053cc085d239f1936a86ba467a111b15a0d05`.
L'ancien mémo OFF et ses empreintes sont archivés. La racine contient maintenant
`LIRE-ESSAI-ON.txt` et le manifeste des fichiers ON.

Après synchronisation, démontage et remontage de la clé :

- **104 fichiers ON** conformes et **104 fichiers OFF archivés** conformes ;
- `ocvalidate` 1.0.8 : aucune erreur ; signature ad hoc vérifiée strictement ;
- **101 fichiers de l'EFI OPENCORE habituelle inchangés** ;
- SIP et authenticated root activés, aucune commande de modification NVRAM,
  de chargement/déchargement à chaud, de formatage ou de redémarrage.

**La session en cours reste en OFF**, avec `navi48-pci-probe=0`. Modifier les
fichiers de la clé ne modifie pas les arguments déjà reçus par le noyau.
Le prochain boot depuis PROBE1401 doit recevoir `navi48-pci-probe=1`.

## Prochaine action

F12 → petite clé **UEFI Flash Disk / PROBE1401** → **Macintosh HD / Tahoe installé**.
Au bureau :

```sh
sysctl -n kern.bootargs
ioreg -r -c Navi48PciProbe -l -w 0
```

Attendre l'argument **1** et le dictionnaire **`Navi48PCI,RegistrySnapshot`**.
Conserver le nouveau journal OpenCore. En cas de blocage, photographier les
messages puis revenir au disque OPENCORE habituel via F12 ; pas de Reset NVRAM.
L'archive OFF n'est pas une entrée de boot sélectionnable par son nom.

**Aucun résultat ON, accès matériel direct ou accélération n'est encore validé.**
La suite reste une observation IORegistry, sans initialisation GPU.
[Guide complet](../PCI-PROBE-EFI-ESSAI.md) ·
[Rapport JSON](2026-10-09-pci-probe-on-deployment.json).

Preuves locales : `out/efi-pci/enable-probe1401-20261009T102938Z/`.
Les EFI contiennent le SMBIOS privé et restent hors Git.
