# Navi48Native.kext — premier pilote natif, étape 1 en cours

**Le produit est maintenant un vrai bundle kext x86_64**, avec personnalité PCI,
classe `Navi48Native : IOService`, points d'entrée kmod et code firmware lié.
Ce n'est plus uniquement une bibliothèque. **Il n'a pas été installé ou chargé,
et la Radeon n'est pas encore initialisée par ce pilote.**

## Livrable de cette passe

- Identité : `com.amd-macos-driver.Navi48Native`, version actuelle `0.1.1`.
  Le livrable historique `stage1/` reste en `0.1.0`, inchangé.
- Une personnalité, ciblant `1002:7550 / 1849:5417`, classe graphique ; la
  révision `c0` et les six champs sont revalidés par lecture PCI au démarrage.
- Le vrai contrôleur de ressources est possédé par le service : bail PCI,
  BAR0/BAR2/BAR5, références aux mappings conservées, rapport sur notre nœud.
- Le core firmware/IP/PSP/SMU/IMU et **dix firmwares épinglés sont réellement
  liés dans le kext**, pas laissés dans une archive séparée ou simulés.
- `IOWorkLoop`/`IOCommandGate` sérialisent démarrage et retrait. Un arrêt
  récursif pendant acquisition annule le démarrage sans détruire le contrôleur
  en cours d'appel. Libération et `IOService::stop` sont hors du gate.
- Arrêt, terminaison du provider et messages de suspension/arrêt d'alimentation
  retirent les ressources jamais publiées au GPU. Pas de réactivation à chaud.
- Aucun client utilisateur, accélérateur, personnalité Apple ou hook NVIDIA.

Le service ne fournit pas encore un cycle de puissance/sommeil complet. Les
messages reçus sont traités, mais aucun pilote de puissance n'est enregistré
pour prétendre maîtriser le matériel. Cela reste bloquant avant initialisation.

## Mémoire DMA ajoutée, pas encore utilisée par le service

`DmaBuffer.cpp/.hpp` implémente les véritables appels `IOBufferMemoryDescriptor`,
`IOMapper::copyMapperForDevice` et `IODMACommand` en mode `kMapped`. Les pages
IOVM viennent de `gen64IOVMSegments`, **jamais** d'une conversion d'adresse CPU
physique. Les listes de pages non contiguës ne sont pas aplaties en une adresse
unique ; les copies et `synchronize` prennent en compte les bounce buffers.
Préparation/nettoyage hors du gate, protection contre réentrée et quarantaine
terminale si le GPU peut encore utiliser une adresse.

Ce code est compilé/lié dans `0.1.1`, mais **le service ne l'appelle pas encore**.
La réservation VRAM, GMC/GART et les moteurs CP/MES/calcul restent à raccorder.
Le chemin amont `amdgpu_sysmem.cpp` n'est pas remplacé dans un pilote exécuté ;
les pages IOVM générées ne constituent pas une preuve de transfert Radeon.
Aucun blocage du contrôleur n'a été supprimé. Les étapes matérielles 1 et 2
ne sont pas terminées. [Détails et limites](../../docs/reports/2026-10-09-native-dma.md).

## Activation volontaire, sans choix de VRAM implicite

Par défaut, le service **ne s'attache pas**. Même si le bundle est chargé, son
point d'entrée ne lance aucun firmware. Il faut les trois paramètres distincts :

- `navi48-native-platform=1` ;
- `navi48-native-scratch-offset` : candidat relatif à BAR0, entier 64 bits,
  alignement 64 Kio ;
- `navi48-native-scratch-bytes` : candidat de taille multiple de 4 Kio,
  au moins 24 Mio pour le layout PSP.

Ces paramètres sont documentés, **pas ajoutés à une EFI**. Il n'y a pas de
valeur de secours à 64 Mio. Le candidat n'est jamais interprété comme une
réservation VRAM ou une permission firmware. L'absence/mauvaise forme des
paramètres fait refuser `probe/start` avant la première opération PCI.

Le succès de `start` signifie **service d'acquisition démarré**, pas GPU prêt.
`Navi48Native,Resources` rapporte les adresses CPU physiques, options de
mapping, console, candidat et blocages. `FirmwareExecuted`, `GPUInitialized`
et `AccessEnabled` restent à zéro. Il n'existe aucun argument « force » qui
transforme les sept familles de preuves manquantes en autorisation.

## Compiler le livrable, pas une campagne de tests

À partir des objets natifs déjà construits et vérifiés :

```sh
python3 -B tools/build-native-kext.py --output out/native-kext/mon-build-neuf
```

Par défaut, `out/native-platform/final-a/` fournit les deux objets. Pour un
checkout sans ces artefacts, construire le core une fois, puis le réutiliser :

```sh
python3 -B tools/build-native-firmware-core.py --output out/native-core/preparation
python3 -B tools/build-native-kext.py --core-build out/native-core/preparation --output out/native-kext/mon-build-neuf
```

Le build kext vérifie les empreintes/code/firmwares des objets d'entrée, exécute
**des contrôles ciblés** service/contrôleur et adaptateur DMA, compile le service,
l'adaptateur DMA et les entrées kmod, lie les objets natifs, signe ad hoc et contrôle le Mach-O et
les dix firmwares **dans le binaire final**. Pas de rebuild global Navi48,
de mutations ni de campagne sur l'observateur. SDK/cache utilisés offline.

Sortie actuelle : **`out/native-kext/dma-integration/Navi48Native.kext`**, et ZIP voisin.
La sortie historique `out/native-kext/stage1/` est conservée. La signature et le format `MH_KEXT_BUNDLE` sont vérifiés. Cela ne prouve pas
l'acceptation par le noyau Tahoe, la compatibilité des imports ou l'accélération.
Les notices amont et firmware sont incluses dans le bundle.

## Ce qui termine réellement l'étape 1

1. Préparer/autoriser séparément un essai de boot du kext, sans modifier la
   référence OPENCORE, et observer son attachement au provider réel.
2. Établir placement/réservations et origine/taille/base MC de la VRAM,
   retrait des utilisateurs GPU, protocole HDP, chemin DMA applicable et arrêt.
   Les mappings RO et un bail IOKit ne produisent pas ces preuves.
3. Raccorder une autorisation matérielle fondée sur ces résultats au contexte
   et à l'orchestration PSP/SMU, puis initialiser réellement les blocs nécessaires.
   Les autres phases/bootloader demandent encore une propagation d'erreurs complète.

**L'étape 1 n'est donc pas déclarée terminée.** La cible suivante n'est pas un
nouveau rapport synthétique : c'est le raccordement matériel permettant cette
initialisation, avant la première commande GPU de l'étape 2.

Aucune copie dans OpenCore ou `/Library/Extensions`, aucun chargement/déchargement
à chaud effectué par le build. OPENCORE, PROBE1401 et Navi48PciProbe restent
inchangés. Ne pas déployer ce bundle comme un pilote graphique fonctionnel.

[Rapport du livrable](../../docs/reports/2026-10-09-native-kext.md) ·
[Contrat du contrôleur](../../native/Navi48FirmwareCore/IOKIT-CONTROLLER.md)
