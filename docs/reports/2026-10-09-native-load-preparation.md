# Navi48Native 0.1.2 — DMA raccordée et chargement OpenCore préparé

> **Historique conservé :** cette EFI 0.1.2 a été remplacée, avant tout boot
> natif, par l'essai 0.2.0 initialisation/calcul. Les preuves de cette passe
> restent valides pour son déploiement, pas pour l'état actuel de PROBE1401.
> Voir [la suite](2026-10-09-native-compute.md).

## Verdict

**Le pilote natif est réellement déployé sur l'EFI d'essai PROBE1401**, activé
pour le prochain démarrage, et non plus seulement livré en bibliothèque/bundle.
**Il n'est pas chargé dans le noyau courant.** Aucune allocation IOKit sur la
Radeon, écriture firmware, initialisation GPU, commande ou calcul matériel
n'est observé dans cette passe. Les étapes matérielles 1 et 2 restent ouvertes.

L'action nécessaire est physique : **redémarrage → F12 → PROBE1401 UEFI →
Tahoe installé**, puis relever module, UUID, ressources et erreurs éventuelles.
[Procédure et récupération](../NATIVE-KEXT-ESSAI.md).

## Raccordement implémenté

`Navi48Native::start()` exécute maintenant le vrai `DmaBuffer::allocate()`
après acquisition PCI. **64 Kio de RAM, pas les 24 Mio du candidat BAR0.**

- Acquisition sous gate ; préparation IODMACommand hors gate.
- Service/provider/workloop/gate retenus pendant l'opération bloquante.
- Arrêt ou invalidation pendant préparation : annulation sous gate, aucun
  retrait concurrent du bail ; finalisation prend en charge le rollback.
- Revalidation `revalidateHeld()` : divergence terminale, observations invalides,
  bail conservé jusqu'au nettoyage DMA hors gate puis fermeture PCI.
- Propriété du buffer transférée au service seulement après validation et
  publication réussies. Arrêt/terminaison/suspension retirent les ressources.
- Pas de publication d'adresse GPU, GART, firmware ou client utilisateur.
  Le masque des blocages matériels n'est pas artificiellement effacé.

Le schéma 2 de `Navi48Native,Resources` expose l'appel DMA, phase/résultat,
taille/pages et mapper choisi. Les champs de qualification GPU restent zéro.
Cette instrumentation ne remplace pas l'observation du premier boot réel.

## Tests et produit contrôlé

| Vérification | Résultat | Portée |
| --- | --- | --- |
| Service + contrôleur + vrai adaptateur DMA | 234 contrôles, 0 échec, ASan/UBSan | Doubles IOKit, pas matériel |
| Contrôleur, dont revalidation à retrait différé | 2 271 contrôles, 0 échec, ASan/UBSan | Doubles mappings/PCI |
| Adaptateur DMA, pages/bounce buffers/quarantaine | 162 contrôles, 0 échec, ASan/UBSan | Doubles IODMACommand |
| Modèle/logger et accès/PSP | 1 582 + 398 contrôles, 0 échec | Modèle et RAM, ACK PSP simulés |
| Outils Python, dont profil/swap/rollback | 76 tests, 0 échec | Pas de boot |
| Kext x86_64, signature ad hoc et dix firmwares | Conformes, zéro avertissement final | Build/signature/inspection |
| Imports BootKC | 312 noms présents, aucun manquant | Ni liaison, ni ABI, ni chargement validé |

Les tests du service incluent arrêt depuis **un autre thread** pendant prepare,
échecs de préparation, changement de configuration/console durant la phase
hors gate, échec de publication, mapper par défaut, pages discontiguës et
nettoyage avant fermeture PCI. Aucun résultat GPU n'est substitué par ces tests.

Le premier build kext `out/native-kext/service-dma/` contenait un warning de
conversion `IOReturn` signé vers OSNumber ; le préparateur l'a refusé avant
écriture EFI. Conversion 32 bits corrigée, builder durci pour refuser tout
warning noyau. **Seul `kernel-trial/` sans warning est déployé.**

Artefacts :

- core/controller : `out/native-platform/service-dma/` ;
- kext/ZIP finaux : `out/native-kext/kernel-trial/` ;
- préparation sans activation : `out/efi-native/native-0.1.2-preflight/` ;
- préparation et déploiement : `out/efi-native/native-0.1.2-trial/`.

Binaire SHA-256 :
`ce1f4e39a2795bdabd3789fb8d632fdac3ed3209deab851f30429ca418696e23`.
ZIP SHA-256 :
`4552e73c63d83a9d7f51dafd631406847d62af5fe9f1b35c0747f84da188f276`.
UUID attendu au chargement : **`E54A318B-F675-3DF6-A4A5-4D25332E6A27`**.

## Déploiement réel, pas essai noyau

Le préparateur conserve l'EFI OPENCORE comme source : mêmes cinq kexts,
patches CPU, paramètres mémoire, SMBIOS et sécurité. Seuls sont ajoutés une
entrée `Kernel/Add` et trois arguments natifs. Les coupe-circuits amont
`navi48bringup=0 rdna4-off=1` restent présents. Aucun kext observateur ou amont
complet n'est injecté dans le nouvel essai.

Le candidat d'observation est `BAR0+0x4000000`, `0x1800000` octets. Ce n'est
**pas une réservation VRAM**, un accès RW ou une adresse MC/DMA.

Remplacement effectué uniquement sur la clé USB identifiée PROBE1401,
par staging puis échange de l'EFI entière avec sauvegarde ; pas de fusion,
formatage, `bless`, modification NVRAM ou sécurité, redémarrage, installation
système ou chargement à chaud. Tests du rollback sur répertoires temporaires,
**pas restauration matérielle qualifiée**.

Vérification indépendante après déploiement :

- **101 fichiers OPENCORE inchangés** ;
- **112 fichiers du nouvel essai identiques au paquet préparé** ;
- **104 fichiers de l'ancien essai conservés**, en copie locale et sous
  `/Volumes/PROBE1401/EFI.BACKUP-native-0.1.2-trial` ;
- `ocvalidate` 1.0.8 réussi sur l'EFI réellement amorçable ;
- signature stricte du kext copié vérifiée ;
- manifeste à la racine de la clé mis à jour, ancien manifeste/notice archivés ;
  nouvelle notice `LIRE-ESSAI-NATIF.txt` fournie.

Les EFI et `config.plist` contiennent les identifiants SMBIOS privés et restent
hors Git. Les rapports publics ne publient pas ces configurations.

À **16:26:58 UTC**, contrôle du noyau courant : aucun module natif dans
`kmutil showloaded`, aucune classe `Navi48Native` dans `ioreg` (exit 0, sortie
vide), aucun argument natif reçu. La session est toujours celle de référence,
**pas le boot de l'EFI nouvellement déployée**. Captures dans
`out/efi-native/native-0.1.2-trial/current-session/`.

## Sauvegardes Git et suite

Checkpoints poussés sur `codex/amd-validation-bootstrap` :

- `707aa8f` : travaux natifs/firmware/contrôleur/kext/DMA antérieurs ;
- `2cf5f4e` : allocation DMA raccordée au service, tests et retrait différé ;
- `cf8657c` : correction de signedness, préparation EFI contrôlée et rollback.

Après le boot, distinguer injection OpenCore, chargement du module, attachement
PCI, préparation RAM/IOVM et stabilité. Ensuite restent à raccorder/qualifier
VRAM/propriété/HDP/DMA GPU/puissance/quiescement, orchestration firmware,
GMC/GART, moteurs, commande avec fence et shader dont le résultat est relu.
Metal, RADV Darwin et WindowServer ne sont pas implémentés par ce kext.
