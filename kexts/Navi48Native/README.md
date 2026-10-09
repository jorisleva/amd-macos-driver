# Navi48Native.kext — pilote natif, premier essai noyau préparé

**Vrai bundle noyau x86_64 0.1.2**, avec personnalité PCI, service IOKit,
points d'entrée kmod, contrôleur PCI et adaptateur DMA raccordés.
Il est maintenant **déployé dans l'EFI de la clé d'essai PROBE1401**, activé
pour le prochain démarrage. **Pas encore chargé dans le noyau courant.**
Aucune installation dans `/Library/Extensions`, aucune initialisation GPU,
commande Radeon ou accélération Metal observée.

[Redémarrage et vérification du vrai noyau](../../docs/NATIVE-KEXT-ESSAI.md) ·
[Rapport et empreintes](../../docs/reports/2026-10-09-native-load-preparation.md)

## Chemin réellement implémenté

- Identité `com.amd-macos-driver.Navi48Native`, cible PCI
  `1002:7550 / 1849:5417`, révision `c0`, classe `030000`.
- Le service possède `IOKitController` : ouverture PCI non-seize, mappings
  privés RO/UC BAR0/BAR2/BAR5 conservés, contrôle de configuration et console.
- Le code IP/PSP/SMU/IMU et dix firmwares épinglés sont réellement liés dans le
  binaire. **Ils ne sont pas encore appelés pour initialiser le matériel.**
- `start()` acquiert les ressources sous `IOCommandGate`, puis prépare
  **64 Kio de RAM DMA hors gate** avec le vrai `DmaBuffer`. Le service,
  provider, workloop et gate sont retenus pendant cette phase bloquante.
- Arrêt/terminaison/suspension pendant préparation annulent le démarrage sans
  fermer le bail utilisé par l'allocateur. Finalisation/revalidation et transfert
  de propriété sont sous gate ; DMA/mappings/PCI/base-stop sont nettoyés hors gate.
- `revalidateHeld()` invalide les observations en cas de divergence, mais
  conserve le bail pour nettoyer DMA avant la fermeture PCI.
- Les ressources DMA incertaines restent en quarantaine ; aucune adresse
  n'est publiée au GPU. Aucun client utilisateur, accélérateur, hook NVIDIA
  ou personnalité graphique Apple. Pas de réactivation à chaud.

Le cycle complet de puissance/sommeil n'est pas implémenté. Les messages reçus
sont traités, mais aucun pilote de puissance enregistré ne prétend maîtriser
le matériel. Cela reste bloquant avant l'initialisation réelle.

## RAM DMA : appelée par le service, pas qualifiée par la Radeon

`DmaBuffer.cpp/.hpp` emploie `IOBufferMemoryDescriptor`,
`IOMapper::copyMapperForDevice` et `IODMACommand::kMapped`. Les pages IOVM
proviennent de `gen64IOVMSegments`, jamais d'une conversion CPU physique.
Pages non contiguës conservées ; écritures/lectures du descripteur original et
synchronisation des bounce buffers ; préparation/nettoyage hors gate,
protection contre réentrée et quarantaine terminale après publication/incertitude.

**En 0.1.2, `allocate()` est effectivement dans le chemin de démarrage.**
Une réussite matérielle n'a toutefois pas encore été observée : elle demandera
le boot de l'EFI préparée. GMC/GART et les moteurs CP/MES/calcul restent à
raccorder. L'amont `amdgpu_sysmem.cpp` n'est pas remplacé dans un pilote exécuté.
Une liste IOVM, même obtenue sur IOKit réel, ne prouvera pas un transfert GPU.
Aucun blocage firmware n'est supprimé à la seule réussite de l'allocation.

## Activation explicite et limites

Le service ne s'attache pas sans trois paramètres distincts :

- `navi48-native-platform=1` ;
- `navi48-native-scratch-offset`, candidat BAR0-relatif aligné 64 Kio ;
- `navi48-native-scratch-bytes`, multiple de 4 Kio, au moins 24 Mio.

L'EFI d'essai choisit explicitement `0x4000000` / `0x1800000`.
**Ce candidat d'observation RO ne réserve aucune VRAM**, n'autorise pas de
firmware et n'est pas la taille RAM DMA. Aucun placement implicite ni argument
« force » ne transforme les sept familles de preuves manquantes en autorisation.

`start()` réussi signifie **ressources PCI/DMA préparées**, pas GPU prêt.
`Navi48Native,Resources`, schéma 2, publie les observations sur notre nœud,
jamais sur le provider. `DMAAllocatorInvoked=1`, `DMAPhase=Prepared` et les
64 Kio/16 pages permettent de distinguer le raccordement DMA d'un simple bundle
chargé. `DMAAddressPublished`, `GPUDMAValidated`, `FirmwareExecuted`,
`GPUInitialized` et `AccessEnabled` restent zéro.

## Compiler et reproduire

Bundle actuel : **`out/native-kext/kernel-trial/Navi48Native.kext`**, ZIP voisin.
Core/controller actuel : `out/native-platform/service-dma/`. Les sorties
historiques `stage1/` (0.1.0) et `dma-integration/` (0.1.1) restent conservées.
Le premier `service-dma/` kext avait un avertissement de signedness ; il n'a
pas été déployé. L'image `kernel-trial/` corrige cette conversion, zéro warning.

```sh
python3 -B tools/build-native-firmware-core.py --output out/native-platform/nouveau
python3 -B tools/build-native-kext.py \
  --core-build out/native-platform/nouveau --output out/native-kext/nouveau
```

Le builder vérifie les objets/source/SDK/firmwares, teste le vrai service et
l'adaptateur sur doubles IOKit sous ASan/UBSan, compile/lie/signe le kext et
revérifie les dix firmwares dans le Mach-O final. Tout avertissement noyau
fait maintenant refuser le build. La signature et les noms d'imports trouvés
dans le BootKC ne prouvent pas la liaison/ABI/acceptation par Tahoe.
Les notices amont et firmware sont incluses dans le produit.

## Ce qui reste pour les étapes matérielles 1 et 2

1. Observer le chargement, l'attachement PCI et l'allocation IOKit au boot réel.
2. Établir propriété/quiescement, placement/réservations et géométrie/origine/base
   MC de la VRAM, HDP, chemin DMA applicable et récupération après panne.
3. Raccorder le contexte autorisé et l'orchestration PSP/SMU/GMC/GART/moteurs.
4. Soumettre une commande puis un shader, attendre une fence bornée, relire et
   comparer le résultat réellement produit par la Radeon.

**Aucune de ces deux étapes matérielles n'est déclarée terminée.**
`OPENCORE` reste intact. PROBE1401 est désormais l'essai natif, pas l'ancien
observateur. Le builder n'effectue ni copie EFI ni chargement à chaud ; le
préparateur séparé ne redémarre jamais. Ne pas présenter ce bundle comme un
pilote graphique fonctionnel ou un bureau accéléré.

[Historique du kext](../../docs/reports/2026-10-09-native-kext.md) ·
[Historique DMA 0.1.1](../../docs/reports/2026-10-09-native-dma.md) ·
[Contrôleur](../../native/Navi48FirmwareCore/IOKIT-CONTROLLER.md)
