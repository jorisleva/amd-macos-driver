# Contrôleur de ressources IOKit — mappings, pas autorisation GPU

[`IOKitController.hpp`](IOKitController.hpp) / [`IOKitController.cpp`](IOKitController.cpp)
implémentent les appels IOKit réels. Le build les compile avec le MacKernelSDK
épinglé dans **`libNavi48PlatformController.a`**, séparément du firmware core.
Aucun de ces appels n'a encore été exécuté sur le provider Radeon réel.

Ce n'est pas un IOService enregistrable, un kext, une personnalité PCI ni un
client utilisateur. Le futur IOService attaché devra posséder ce contrôleur,
l'appeler dans un contexte pouvant dormir et gérer les événements de cycle de
vie. Il n'y a pas de point d'entrée, d'initialiseur global ou d'installation.
L'observateur PCI et les EFI ne sont pas concernés.

**Raccordement maintenant livré :** [Navi48Native.kext](../../kexts/Navi48Native/)
possède ce contrôleur dans un vrai IOService, avec gate, démarrage/arrêt et
terminaison. Le kext est construit, pas exécuté sur le provider réel. Puissance,
publication GPU, autorisation firmware et quiescement restent incomplets ; ce
raccordement ne supprime aucun des blocages ci-dessous.

## Acquisition implémentée

Une seule tentative par objet ; pas de remise à zéro après refus ou libération.
L'API n'accepte **aucun `Claims` ni booléen de qualification matérielle**.
La requête contient seulement un intervalle candidat relatif à BAR0 et une
quantité de mémoire nécessaire. Aucun choix automatique à 64 Mio ou ailleurs.

1. Lire l'opt-in distinct `navi48-native-platform=1`. Absence ou autre valeur :
   refus avant toute ouverture/lecture PCI. Cet argument n'est ajouté à aucune EFI.
2. Vérifier le type IOPCIDevice, l'attachement de l'owner et leur activité.
   Refuser un provider déjà ouvert, même par cet owner. `open(owner, 0, nullptr)` :
   pas de `seize`, pas de reprise forcée d'un autre pilote. Retenir provider et owner.
3. Lire les six champs PCI réels `1002:7550 / 1849:5417 / c0 / 030000`, le BDF,
   le header et la commande PCI. Exiger un endpoint et le décodage mémoire déjà
   actif. Observer le bus mastering existant, **ne jamais le modifier**.
4. Lire BAR0/BAR2 en 64 bits (haut/bas/haut), BAR5 en 32 bits. Vérifier leurs
   types, alignements et adresses complètes ; jamais écrire `0xffffffff` pour
   obtenir une taille. Le profil accepté est volontairement limité aux fenêtres
   observées : **256 Mio / 2 Mio / 512 Kio**. Les adresses restent dynamiques.
   Un autre profil ReBAR est refusé, pas traité comme une nouvelle taille VRAM.
5. Obtenir les `IODeviceMemory` par registre, ajouter notre propre retain, puis
   contrôler longueur et **segment CPU physique contigu complet**, avec
   `kIOMemoryMapperNone`. Comparer à l'adresse PCI et refuser les chevauchements.
6. Contrôler le candidat : non nul, aligné 4 Kio, assez grand, dans BAR0 et sans
   overflow. Son adresse CPU physique est calculable ; **ce n'est pas une adresse
   MC, une adresse DMA ou une allocation VRAM réservée**.
7. Interroger `IOPlatformExpert::getConsoleInfo`, sans lire les pixels. Quand la
   géométrie est interprétable et entièrement contenue dans BAR0, refuser un
   candidat qui chevauche la console.
8. Mapper les **descripteurs exactement retenus**, avec `kIOMapUnique`,
   `kIOMapReadOnly`, `kIOMapInhibitCache` et placement quelconque. Conserver les
   références retournées, sans libération anticipée.
9. Contrôler le backing, la tâche noyau, la longueur et le segment physique du
   mapping, ses options rapportées, les VAs 64 bits et l'absence de chevauchement.
   Relire configuration, descripteurs et console ; refuser toute divergence.

Le contexte AMD privé reçoit les VAs de ces mappings, **mais reste désactivé** :
ni MC, ni taille/origine VRAM, ni réservation ou HDP qualifié ne sont inventés.
Aucun pointeur, mapping ou `DeviceContext` n'est renvoyé à l'appelant. Aucun
helper MMIO/VRAM/doorbell, chemin PSP ou binder d'autorisation n'est invoqué.
BAR4 et ROM ne sont jamais mappés.

## Portée exacte des observations

| Observation produite par les API | Ce qu'elle ne prouve pas |
| --- | --- |
| `open` accepté et `isOpen(owner)` | Retrait de tous les utilisateurs GPU, GOP/WindowServer, réservation VRAM exclusive |
| PCI → descripteur → mapping concordants | Origine BAR0 en VRAM, taille VRAM totale, base MC, DMA/IOMMU |
| Options UC/RO/unique demandées et rapportées | PTE/MTRR effectifs, absence d'autres alias, protocole HDP, cohérence CPU/GPU |
| Intervalle console rapporté dans BAR0 | Toutes les réservations UEFI/firmware ou la liberté du reste de la VRAM |
| Candidat dans BAR0 et hors console connue | Réservation acquise, layout PSP qualifié ou droit d'écrire |
| Nettoyage de mappings jamais publiés au GPU | Arrêt des moteurs, récupération après commande ou droit de recycler des pages GPU |

`ConsoleObservation` conserve base et géométrie brutes. `rowBytes * height`,
largeur/profondeur, `v_offset` et `v_length` sont contrôlés sans débordement.
Si une longueur explicite existe, **l'allocation entière** est protégée, même
les octets hors des pixels visibles. Une base nulle ou non alignée sur 4 Kio
est ambiguë : les bits bas ne sont pas masqués comme dans le pilote amont,
faute de contrat documenté. Absence d'expert, échec de l'appel, console hors
BAR0 ou géométrie ambiguë restent des blocages de placement. Aucun intervalle
console fictif n'est substitué.

La réussite de l'acquisition s'appelle **`MappingsHeldUnqualified`**, pas
`HardwareAuthorized`. Même avec console interprétable, sept familles de preuves
manquent toujours : propriété GPU exclusive, géométrie VRAM/origine/base MC,
réservation VRAM, cohérence HDP, DMA/IOMMU, restauration et quiescement GPU.
Le masque `blockers` ne peut donc jamais être nul dans cette implémentation.
Une console inconnue n'empêche pas de conserver les mappings RO, mais ajoute
son blocage et n'autorise aucun accès à leur contenu.

## Durée de vie et sérialisation

`acquire`, `revalidate`, `snapshot` et le retrait des ressources sont sérialisés
par `IOLock`. La variante `revalidateHeld()` (service 0.1.2) effectue les mêmes
contrôles mais, en cas de divergence, rend les observations terminales/invalides
**sans fermer immédiatement le bail**. Le service retire ainsi DMA hors de son
gate avant `release()`/fermeture PCI ; les mappings retenus ne donnent aucune
permission d'accès. Pas de retry après cet échec. `snapshot` ne fait aucun nouvel appel matériel : il renvoie des
valeurs copiées et les blocages. `observationsValid` signifie que le dernier
contrôle a réussi et que le contrôleur conserve le bail, **pas** que le système
entier est verrouillé ou que ces observations seront vraies indéfiniment.

`revalidate` détecte notamment activité/attachement/perte du bail, remplacement
des descripteurs, changement de configuration PCI, VA, segment physique ou
console. Une divergence rend la session terminale et retire ses mappings.
Ce n'est pas une interception automatique des événements macOS : le futur
IOService devra propager terminaison, arrêt, sommeil et changements de console.
Même une relecture identique à deux instants n'est pas une preuve de stabilité
entre ces instants ; c'est une autre raison de ne pas autoriser le GPU.

En cas d'échec partiel ou d'arrêt :

- vider/désactiver le contexte **avant** de retirer les références ;
- adopter les ressources retirées dans un objet local, puis libérer le verrou ;
- libérer maps/descripteurs en ordre inverse, fermer notre bail, rendre les
  retains du provider et de l'owner ;
- ne jamais forcer `unmap()` sur un mapping potentiellement partagé ;
- permettre les réentrées de `close` sans verrou détenu ni double libération ;
- exiger que l'appelant ait joint les utilisateurs avant de détruire l'objet.

Ce nettoyage est valable parce qu'**aucune ressource de ce contrôleur n'est
exposée au GPU**. La future publication devra être marquée avant sa première
opération visible et utiliser arrêt confirmé/quarantaine ; ce contrôleur ne
fournit ni fausse méthode de quiescement ni succès de reprise à chaud.

## Banc et reproduction

```sh
python3 -B tools/build-native-firmware-core.py --output out/native-platform/essai-neuf
python3 -B -m unittest discover -s tests/tools -v
```

Le build exporte/revérifie sources, SDK et firmwares épinglés. Le firmware core
conserve ses neuf unités amont et treize imports noyau. Le contrôleur a une
unité locale et quinze imports sur sa **propre** liste ; aucune extension
implicite de la frontière du core. Les deux objets sont des `MH_OBJECT`, sans
constructeur global, point d'entrée, dylib ou packaging kext. Ce n'est pas une
liaison qualifiée avec le noyau Tahoe.

[`controller_test.cpp`](../../tests/native-platform/controller_test.cpp) compile
le **même contrôleur**, avec des doubles IOKit séparés des stubs de l'observateur.
Les VAs sont des sentinelles non déréférençables, pas la Radeon ni une simulation
qui peut acquitter un accès GPU. Le banc vérifie refus, géométrie, références,
acquisitions partielles, dérives, réentrées de fermeture et snapshots concurrents
avec libération. ASan/UBSan contrôlent le code hôte ; ils ne qualifient pas IOKit,
les caches PCI, la mémoire du GPU ou la stabilité d'un kext.

Résultats et artefacts : [rapport de cette étape](../../docs/reports/2026-10-09-native-platform.md).

## Raccordement encore nécessaire avant le firmware

Établir un contrat vérifiable pour les bits/adresses console Tahoe, l'origine
et la géométrie VRAM, toutes les réservations, le retrait des utilisateurs GPU,
HDP, DMA/IOMMU et l'arrêt/restauration. Implémenter leurs producteurs de preuves
et le cycle de vie du futur IOService ; **ne pas convertir les observations
ci-dessus en `Claims` positifs par convention**. Ensuite seulement concevoir
le passage aux mappings écrivable/accès bornés, propager les erreurs de chaque
phase PSP/SMU/bootloader et qualifier le chemin mémoire/commandes.
