# PROBE1401 mise à jour en 0.2.1 — prochain boot diagnostic prêt

## État réellement vérifié

À la demande de l'utilisateur, **Navi48Native 0.2.1 est déployé sur PROBE1401**.
Préparation à **2026-10-10T12:18:20.513462+00:00**, vérification finale à
**2026-10-10T12:22:06.783127+00:00**. Signature ad hoc stricte et `ocvalidate`
réussis ; **112 fichiers EFI USB** conformes au manifeste et au staging local.

**Le noyau courant reste en 0.2.0**, UUID
`AB1F0B3A-865C-33FC-BC6B-5CA0056BBF45`, zéro instance native, même boot
11:20:06 UTC. **0.2.1 n'est pas encore chargé ni testé au démarrage** ; sa
persistance IOResources réelle et l'initialisation/calcul Radeon restent à
observer. Le refus du boot 0.2.0 est toujours inconnu. Aucun chiffre matériel
ni `FailedStage` / `HardwareTouched` n'est supposé.

## Remplacement strict et sauvegardes

Le préparateur ajoute `--replace-native-0.2.0`, exclusif de l'ancienne option
0.1.2. Ce remplacement exige :

- cible USB PROBE1401 exacte (UUID, USB externe, FAT writable), jamais OPENCORE ;
- ancienne EFI 0.2.0 entière identique à son manifeste revu, binaire épinglé
  `34ce08473ac2acedd0a9094420cbd4ebf159bf48b21a0134095715cfd95809f9` ;
- profil compute 64+64 Mio et double opt-in exacts, référence conforme au
  manifeste du déploiement précédent ; cible limitée au correctif 0.2.1 ;
- build/source/hashes/signature contrôlés, publisher diagnostic réellement lié ;
- staging EFI complète, validation avant/après swap, sauvegarde et rollback
  en cas d'échec, aucun écrasement des backups existants.

Le garde de session conserve le refus des boots expérimentaux non revus.
Une **exception de copie hors ligne uniquement** permet ce remplacement depuis
le boot 0.2.0 exact : arguments/version/UUID attendus, pas d'autre module
expérimental, aucun nœud natif et compteur de classe explicitement zéro.
Cela **ne prouve pas que le hardware n'a jamais été touché** et n'autorise
aucun unload, retry, appel GPU, changement de sécurité ou reboot automatique.

Les **86 tests Python** passent, dont six nouveaux tests pour cette exception,
le refus d'une instance détachée encore vivante, d'un UUID/version/argument
modifié, d'un autre module expérimental ou d'une ancienne EFI divergente. Les
419 + 162 + 29 + 39 contrôles C++/ASan/UBSan du bundle restent logiciels.

Sauvegardes effectivement revérifiées :

| Arbre | Fichiers | Contenu |
|---|---:|---|
| `/Volumes/OPENCORE/EFI` | 101 | référence inchangée |
| `/Volumes/PROBE1401/EFI` | 112 | nouvelle native 0.2.1 |
| `EFI.BACKUP-native-0.2.1-diagnostics-trial` | 112 | ancienne native 0.2.0 |
| `EFI.BACKUP-native-0.2.0-compute-trial` | 112 | native 0.1.2 conservée |
| `EFI.BACKUP-native-0.1.2-trial` | 104 | ancien observateur conservé |

Copie locale de 0.2.0 :
`out/efi-native/native-0.2.1-diagnostics-trial/previous-trial/EFI` (112 fichiers).
Anciens manifeste/notice archivés et vérifiés, nouveaux manifeste/notice actifs
conformes. Les backups ne sont pas des entrées BIOS indépendantes.

## Produit du prochain boot

- Version **0.2.1**, UUID **62893962-8984-3EE0-B636-40E33C9F073D**.
- Exécutable SHA-256 :
  `441140b8f86095f0bd06e7c77d61f90ae20518e0fa40171e35dd270bd49e01da`.
- **Trois fichiers EFI changés** : `OC/config.plist`, `Info.plist` et exécutable
  de Navi48Native. Dans config, **seul le commentaire/version de l'entrée
  native change** ; SMBIOS, sécurité, patches Ryzen, cinq kexts de référence,
  bornes Darwin 25.6.0, scratch et les cinq arguments natifs sont conservés.
- Dix firmwares/deux shaders publics, zéro warning, signature stricte.
- 324 noms d'import présents dans BootKC : audit de noms, pas preuve globale
  ABI/relocation/vtable/chargement du nouveau produit.

## Prochain boot physique, pas de reload

1. Enregistrer le travail puis **F12 → PROBE1401 UEFI → Tahoe installé**.
2. Ne pas mettre en veille, ne pas décharger/recharger le pilote.
3. Au retour, relever version/UUID, service et diagnostic persistant :

   ```sh
   kmutil showloaded | grep -i Navi48Native
   ioreg -r -c IOResources -l -w 0 | grep 'Navi48Native,BootDiagnostics'
   ioreg -r -c Navi48Native -l -w 0
   ```

   Ces lectures ne soumettent pas de commande GPU et ne nécessitent pas le
   mot de passe dmesg. Le diagnostic est best effort ; s'il manque, capturer
   rapidement le buffer privilégié dans Terminal avant qu'il soit écrasé.
4. Si refus, interpréter le **premier code réellement lu**, pas contourner un
   garde. Décodage : [BootDiagnostics](2026-10-10-native-boot-diagnostics.md).
5. Si écran noir/panic/blocage : photographier, arrêt complet si nécessaire,
   puis **F12 → OPENCORE**. Ce retour n'est pas une récupération GPU qualifiée.

Les étapes matérielles 1/2 exigent toujours les preuves du
[protocole](../NATIVE-COMPUTE-ESSAI.md) : init/firmware, deux fences/tests IB,
64 comparaisons et résultat conforme. Signature, EFI et chargement seuls ne
suffisent pas. Metal/WindowServer et soumission libre ne sont pas raccordés.

## Preuves privées et empreintes

Répertoire : `out/efi-native/native-0.2.1-diagnostics-trial/` ; EFI, SMBIOS,
relevés kernel/IORegistry et backups restent hors Git.

- Préparation :
  `64364f3c4d733599b8f0b4985646b6f93bbfbcac3068341c30c0cfab86530890`.
- Manifeste nouvelle EFI :
  `685f424dfa78b4181f1228d66553f704e79eecf77e133deeae07d3e36478a08b`.
- Vérification finale :
  `a9c558de253664ddcbede48a1168e86dd5f0c6c105d582b2424f3cba93d3e96f`.

Aucune installation système, modification SIP/NVRAM, recharge, commande GPU
ou redémarrage effectué par l'agent.
