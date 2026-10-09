# Navi48PciProbe — observateur IORegistry, pas un pilote GPU

Prototype **0.1.0**, x86_64. Bundle indépendant
`com.amd-macos-driver.Navi48PciProbe`. Il ne compile **aucune source de
Navi48Bringup, NVIDIA, RADV ou des hooks Apple**, et n'embarque aucun firmware.

## Contrat limité

- Une seule personnalité, catégorie propre `Navi48PciProbe`, sans concurrence
  dans les catégories framebuffer/accélérateur.
- Cible exacte : vendor/device **`1002:7550`**, subsystem **`1849:5417`**,
  révision **`c0`**, classe **`030000`**. Le plist filtre les IDs/classe ; le
  code recontrôle les six propriétés, y compris la révision.
- **Désactivé par défaut.** Seul `navi48-pci-probe=1` l'autorise. Absence, 0,
  autres valeurs ou autres arguments Navi48 : refus. Le test précède les
  lectures du provider et se répète dans `start()`.
- L'unique opération sur le provider est **`copyProperty()`**. Les données
  retenues sont libérées après lecture. Aucun `configRead`/`configWrite`,
  activation PCI, ouverture du provider, mapping BAR, interruption, DMA,
  changement d'alimentation ou chargement de firmware.
- Aucun `registerService()` ni enfant publié. `start()` dépose seulement
  `Navi48PCI,RegistrySnapshot` sur **son propre nœud IORegistry**, jamais
  `LoadAccelerator`, une capacité Metal ou une propriété de la Radeon.
- Les deux variantes de `newUserClient()` retournent toujours
  `kIOReturnUnsupported` et mettent le pointeur résultat à zéro.
- `stop()` retire la propriété locale et appelle la classe de base. Il n'y a
  pas d'état GPU, allocation DMA ou registre à restaurer par ce module.

Cela limite les actions de **notre code**, pas tous les effets possibles du
chargement d'un kext dans IOKit. La présence d'un binaire signé ad hoc ne
prouve pas sa compatibilité avec le noyau en cours.

## Données et refus

`ProbePolicy.hpp` décode les propriétés `OSData` du registre x86_64 : six
mots little-endian de quatre octets, puis les enregistrements PCI/OF à cinq
cellules de `assigned-addresses`. Maximum sept ressources (six BAR + ROM).

Refus des types/longueurs incorrects, identités différentes, registres
inconnus ou dupliqués, BDF contradictoires, plages nulles/débordantes ou se
chevauchant dans un même espace, et absence des BAR mémoire 0, 2 ou 5.
L'ordre des ressources, leurs adresses et leurs tailles ne sont pas figés.
La ROM est optionnelle. Les espaces I/O et mémoire sont distincts.

**Important :** dans le relevé réel, le code d'espace 2 accompagne une adresse
supérieure à 4 Gio. Le parseur conserve les 64 bits ; ce code n'est pas une
mesure de la largeur électrique du BAR. Il ne sonde jamais sa taille par
écriture de `0xffffffff` et ne tente aucune correction des données.

Le dictionnaire publié contient `SchemaVersion=1`, les six identifiants,
`ResourceCount`, `PCIBDF` (bits BDF de phys.hi), puis `BAR0`…`BAR5`/`ROM` :
`ConfigRegister`, `RegistryFlags`, `Base` et `Length`. Ce sont **des métadonnées
rapportées par macOS**, pas une vérification des accès matériels, de la VRAM
ou de la cohérence DMA. Elles ne constituent pas une capture atomique du GPU.

Sur le vrai noyau, les `RegistryFlags` OSNumber 32 bits sont exposés comme des
entiers signés dans `ioreg -a` (et de grands entiers dans le rendu texte). Leurs
32 bits doivent être conservés, sans confondre ces masques avec des tailles.
`tools/pci_probe_snapshot.py` compare l'export XML au provider du même boot,
avec normalisation signée/non signée bornée par la largeur déclarée. Les champs
Base/Length restent en 64 bits ; aucune troncature arbitraire n'est admise.

## Construction et tests, sans installation

Depuis la racine du dépôt, avec les Command Line Tools et l'archive du
[MacKernelSDK épinglé](../../docs/AMD-DEPENDENCIES.md) déjà en cache :

```sh
python3 -B tools/build-pci-probe.py --output out/pci-probe/new-a --live-registry
python3 -B tools/build-pci-probe.py --output out/pci-probe/new-b
python3 -B tools/test-pci-probe-mutations.py \
  --build out/pci-probe/new-a --output out/pci-probe/new-mutations
python3 -B -m unittest discover -s tests/tools -v
```

Sorties neuves sous `out/` uniquement. Aucun chemin EFI, installateur, `sudo`,
chargement de kext ou changement de sécurité dans ces outils. Le builder
copie et hache les sources/tests/outils, réextrait le SDK vérifié, conserve
ses notices, compile les deux unités C/C++, contrôle la personnalité, les
imports, l'architecture et la signature, et conserve tous les journaux.
Il utilise `-Werror` ; les binaires noyau n'incluent pas les chemins de debug.

Les tests compilent **le même `Navi48PciProbe.cpp` et le même parseur** avec un
faux IOKit ne proposant aucune API d'accès matériel. ASan/UBSan, refus des
entrées, injections d'échec d'allocation/insertion, rééquilibrage start/stop,
références et absence d'écriture sur le provider sont contrôlés.
`--live-registry` lance `ioreg`, ne conserve que les sept propriétés pertinentes
puis les rejoue dans ce programme hôte ; **il ne charge pas le kext**.

Les contrôles de surface et d'imports sont des garde-fous, pas une preuve
formelle. Beaucoup de symboles de la vtable héritée d'IOService (dont des
méthodes de mapping/interruptions) restent importés même sans être appelés.
L'unique import direct IOPCIDevice autorisé est sa **métaclasse**, pour le test
de type. Toute nouvelle API ou personnalité impose une nouvelle revue.

## État et essai ultérieur

Le [rapport du 9 octobre](../../docs/reports/2026-10-09-pci-probe-isolation.md)
conserve deux bundles identiques, 1 048 contrôles hôtes et 25 mutations détectées.
**À la fin de ces builds : non installé, non chargé, liaison contre la
collection noyau Tahoe non qualifiée.** Le nom `release-a` du dossier de build n'est pas une autorisation
d'installation. La sauvegarde EFI reste vérifiée ; son essai de restauration
est différé à la demande de l'utilisateur, pas considéré comme réussi.
Une modification de `OC/config.plist` distincte du build a depuis été détectée
et conservée : le rapport en décrit les clés, sans publier l'identité SMBIOS.
La configuration actuelle n'est donc plus identique à la copie initiale.

Un futur essai devra concerner **ce seul bundle**, avec les autres pilotes
expérimentaux désactivés, et distinguer : module absent/désactivé, activation
explicite, lecture du nœud, puis retour à la configuration précédente.
L'inspection est `ioreg -r -c Navi48PciProbe -l -w 0`. Elle ne retourne aucun
nœud lors du premier boot OFF, avec module pourtant confirmé chargé. Lors du
premier ON, elle retourne un nœud actif attaché au provider Radeon, avec le
dictionnaire conforme aux données du même démarrage.

Les profils EFI de référence/OFF/ON sont maintenant préparés séparément,
avec `tools/prepare-pci-probe-efi.py`. Le profil OFF a démarré depuis PROBE1401 ;
le profil ON est depuis déployé, vérifié et démarré une première fois.
L'EFI habituelle OPENCORE reste intacte. Les 294 noms d'imports existent dans le BootKC de Tahoe ; ce
contrôle ne qualifie pas leur liaison ni l'ABI. Le profil OFF est désormais
copié et vérifié sur **PROBE1401** :
[mode d'emploi](../../docs/PCI-PROBE-EFI-ESSAI.md),
[préparation](../../docs/reports/2026-10-09-pci-probe-efi.md) et
[déploiement USB](../../docs/reports/2026-10-09-probe1401-deployment.md).
**Premier boot OFF effectué à 09:53 UTC :** OpenCore annonce l'injection,
`kmutil showloaded` confirme la version 0.1.0 et son UUID, l'argument 0 est reçu
et aucun nœud ne s'attache. C'est une preuve de chargement par OpenCore pour
ce boot, pas une validation de `start()`/du chemin ON ou de toute l'ABI.
L'avertissement de notification est conservé dans le
[rapport OFF](../../docs/reports/2026-10-09-pci-probe-off-boot.md).
**Premier boot ON effectué à 11:05 UTC :** six champs d'identité et cinq
ressources conformes au provider du même boot ; le chemin d'observation et de
publication est exercé. Les flags signés sont documentés et le comparateur
passe huit nouveaux tests (52 tests Python au total). Aucun changement du kext.
[Résultat ON](../../docs/reports/2026-10-09-pci-probe-on-boot.md).

Le premier retour au disque OPENCORE est vérifié à 11:31 UTC : argument,
module chargé et nœud observateur absents. PROBE1401 reste ON, OFF est archivé ;
les deux EFI sont inchangées. Restauration de sauvegarde, répétabilité et
stabilité prolongée restent à vérifier. Aucun changement de sécurité ou de
collection noyau sur disque n'a été effectué ; l'injection a lieu au boot par
OpenCore. Aucun accès matériel direct ou calcul GPU n'est qualifié.
L'isolation native commence dans un module séparé, sans modifier cet observateur :
[rapport](../../docs/reports/2026-10-09-native-isolation.md).
