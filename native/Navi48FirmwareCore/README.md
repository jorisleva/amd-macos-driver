# Navi48FirmwareCore — premier sous-ensemble natif, non chargeable

Ce module construit **deux bibliothèques statiques de développement**, pas un kext :
le firmware core et, séparément, un contrôleur de ressources IOKit non activable.
Ils ne doivent être copiés ni dans OpenCore ni dans `/Library/Extensions`.
Il ne fournit ni accélération, ni interface N48N, ni autorisation d'initialiser
la carte. L'observateur `Navi48PciProbe` reste un module distinct et inchangé.

Depuis l'étape suivante, le [vrai bundle Navi48Native.kext](../../kexts/Navi48Native/)
lie ces objets dans un IOService de développement. Ce bundle est compilé/signé,
mais non installé/non chargé et sans initialisation GPU autorisée. Les `.a`
restent des produits distincts ; leur présence ne constitue pas une qualification
matérielle. [Livrable de l'étape 1](../../docs/reports/2026-10-09-native-kext.md).

## Ce qui est réellement séparé

Depuis Navi48 `696959753070e9a16677be485580dc53b06a7ab5` :

- neuf unités C++ : parseur d'IP discovery, adaptateur IP, démarrage PSP,
  extraction/table/chargement des firmwares, protocole PSP, SMU et IMU ;
- quinze en-têtes nécessaires, sélectionnés explicitement et hachés ;
- dix firmwares linux-firmware épinglés, vérifiés **dans l'objet lié** ;
- trois unités locales : journalisation bornée, préconditions/états et liaison
  des fenêtres mémoire ; un nouvel en-tête fournit les accès bornés réels.

La compilation n'utilise pas le Makefile global et n'ajoute pas `src/amd/*.cpp`
par joker. Seuls les fichiers du [manifeste](manifest.json) sont exportés vers
le répertoire d'inclusion. Les dépendances effectivement lues par Clang sont
contrôlées : aucun autre fichier amont, uniquement le MacKernelSDK vérifié.

**Exclus :** `Navi48Bringup.cpp`, son orchestration `amdgpu_init.cpp`, plist,
points d'entrée kmod, personnalités/hooks Apple, clients utilisateur, affichage
DCN, moteur mémoire/GMC/GART, implémentation sysmem/DMA, CP/MES/SDMA, soumission
native S1b/S1c et interface Metal. Le seul en-tête `amdgpu_sysmem.h` fournit
des types au PSP ; son allocateur `.cpp` n'est pas compilé.

Le résultat intermédiaire est un Mach-O **`MH_OBJECT`**, puis une archive `.a`.
Ni initialiseur automatique, ni dépendance dylib, ni point d'entrée de pilote
n'est accepté. Treize imports noyau restent, sur une liste explicite ; aucun
symbole interne non résolu. **Ce contrôle n'est pas une liaison avec Tahoe.**

### Changements amont explicites, pas de faux succès

`amd/amdgpu_log.h` est adapté dans l'export seulement : les messages passent
par [NativeLog.cpp](NativeLog.cpp), qui appelle réellement `IOLog`, indique
la troncature et l'échec de formatage. Le build produit le diff exact
`0001-isolate-logging.patch`. Depuis l'étape suivante, `amdgpu_regs.h` inclut
notre interface d'accès bornés (`0002-bounded-access.patch`) et le fichier PSP
reçoit le [patch explicite](patches/0003-psp-access-errors.patch), avec vérification
du SHA-256 résultant. Le reste des fichiers amont sélectionnés reste identique.

Cela retire le lien caché `n48log.h` → ancien ABI utilisateur et
`n48log.cpp` → instrumentation `Navi48Bringup`/Apple. L'ancien tampon de capture
et ses méthodes ne sont **pas implémentés par des stubs de succès** : ils sont
absents. Les messages de ce nouvel adaptateur portent `Navi48FirmwareCore`.
Les messages directs hérités de `psp.cpp` conservent leur libellé amont.
Aucune garantie de conservation des messages dans le journal macOS n'en découle.

## Accès bornés raccordés au code PSP

[AmdGpuAccess.hpp](AmdGpuAccess.hpp) remplace effectivement l'interface utilisée
par les blocs AMD compilés. [MappedAccess.cpp](MappedAccess.cpp) applique le
contrat de préconditions avant de lier des mappings déjà possédés par l'appelant.
Il ne réalise aucun mapping PCI et n'établit pas la vérité des preuves fournies.

- Accès désactivés par défaut ; refus des mappings nuls, mal alignés, débordants
  ou se chevauchant. Seule l'origine VRAM zéro de BAR0 est actuellement acceptée.
- Registres, BAR0 et doorbells : bornes calculées par soustraction, adresses
  64 bits, alignements vérifiés. BAR0 est restreint à la réservation déclarée,
  pas à toute la fenêtre PCI ; les adresses MC sont vérifiées contre le débordement.
- Copies : source hôte non nulle, sans débordement ni alias d'une fenêtre connue.
  L'appelant garantit toujours sa validité/durée de vie. Les tailles/offsets
  non multiples de quatre sont refusés **avant** copie, jamais tronqués/arrondis.
- Première erreur d'accès conservée et journalisée ; toutes les opérations
  suivantes sont bloquées. Pas d'API de remise à zéro/réactivation.
- Les lectures/écritures indirectes MM_INDEX, SMN et PCIe sont explicitement
  refusées : pas de verrouillage qualifié, pas de repli implicite hors BAR0.
- Le flush HDP exige des registres explicitement qualifiés et vérifiés avant
  sa première écriture. Il n'est plus silencieusement ignoré. Le binder ne le
  qualifie pas ; la plateforme réelle reste bloquée sur ce point.
- Les boucles d'attente vérifient l'erreur avant de comparer la valeur lue :
  une sentinelle d'échec ne devient pas un acquittement. Budget de polling borné,
  sans prétendre garantir un délai mural face à un blocage matériel.

Le PSP utilise ces accès pour son layout de 24 Mio, le staging des firmwares
et la soumission GPCOM. Sont notamment refusés : MC réel/déclaré divergent,
réutilisation implicite du layout, débordement de l'allocateur, ring nul ou
tronqué, pointeur de ring invalide, chevauchements et rebouclage du compteur de
fence. Une erreur de staging ne publie ni adresse MC ni nouvelle allocation ;
une erreur de flush ne publie pas le pointeur de commande.

Le banc compile le **même fichier PSP patché**, le binder et les accès avec
ASan/UBSan sur de la RAM ordinaire. Il vérifie les données copiées et la trame
PSP, ainsi qu'une réponse explicitement simulée. **Ce n'est ni une exécution
de firmware, ni une commande effectuée par la vraie Radeon.**

## Contrôleur IOKit : acquisition réelle implémentée, non exécutée sur la carte

[IOKitController.cpp](IOKitController.cpp) sait ouvrir le provider attaché sans
seize, lire les six champs PCI et contrôler BAR0/BAR2/BAR5 contre les descripteurs
et mappings IOKit. Il conserve leurs références, vérifie le placement console
rapporté par l'expert de plateforme, demande des mappings RO/UC/uniques et
nettoie les échecs partiels hors verrou, sans double libération ni réentrée bloquée.
Il est compilé dans **`libNavi48PlatformController.a`**, avec sa propre frontière
de quinze imports : aucun nouvel import PCI ajouté au firmware core.

Son contexte reste privé et désactivé. Un bail IOKit n'est pas la propriété
GPU exclusive ; les options de cache ne qualifient pas HDP ; être hors console
ne réserve pas la VRAM. L'API n'accepte pas de `Claims` positifs et ne renvoie
aucun pointeur. Le banc du même contrôleur passe **2 241 contrôles ASan/UBSan**
avec des doubles IOKit : **aucun vrai mapping ou accès Radeon n'a été exécuté**.
[Contrat, durée de vie et blocages](IOKIT-CONTROLLER.md).

## Préconditions : raccordement logiciel, preuves matérielles encore absentes

[Preflight.hpp](Preflight.hpp) / [Preflight.cpp](Preflight.cpp) évaluent des
**affirmations fournies par un futur contrôleur**, sans lire le matériel :

- demande explicite de valeur 1 et les six champs `1002:7550 / 1849:5417`,
  révision `c0`, classe `030000` ;
- position et étendue connues de la console, taille VRAM connue et origine
  VRAM de la fenêtre BAR0 connue ; ne pas confondre ces offsets avec une
  adresse PCI ni BAR0 256 Mio avec la VRAM totale ;
- réservation explicite, entièrement dans la fenêtre, assez grande pour la
  demande, alignée sur 4 Kio, sans débordement ni chevauchement console ;
  **aucun choix de secours à 64 Mio** ;
- propriété exclusive, DMA qualifié plutôt que « physique CPU = DMA »,
  restauration réellement essayée et arrêt/nettoyage qualifié.

Un refus efface le plan. `ClaimsConsistent` signifie uniquement que les
valeurs déclarées satisfont ce modèle ; ce n'est **jamais** `HardwareAuthorized`.
Les contraintes 4 Kio et `requiredBytes` ne remplacent pas la vérification du
layout PSP complet, de ses alignements particuliers ou des réservations UEFI.

Le modèle d'états interdit de libérer des ressources potentiellement détenues
par le GPU sans arrêt confirmé. Un échec après exposition au GPU mène à une
quarantaine terminale ; un échec avant exposition n'autorise que la libération,
pas la poursuite de l'initialisation. Pas de double libération ni reprise à chaud.

**Limite essentielle : aucun adaptateur de plateforme ne produit toutes ces
preuves sur Tahoe.** Le binder les consomme et les accès sont raccordés, mais
il ne valide pas leur provenance. Le contrôleur IOKit fournit maintenant le
code d'acquisition, de durée de vie et de nettoyage partiel des mappings privés,
sans autorisation firmware. Propriété GPU exclusive, réservation VRAM, cohérence,
DMA, quiescement et cycle de vie complet restent à implémenter/qualifier.
Les cas positifs du binder sont synthétiques ; les métadonnées PCI réelles et
le retour OPENCORE ne suffisent pas à passer.
D'autres routines PSP/SMU héritées restent à auditer : ce durcissement ciblé
ne qualifie ni tous leurs chemins ni un contrôleur d'initialisation complet.

## Reproduire, sans installation ni réseau

Prérequis : Command Line Tools, checkout et cache des
[dépendances épinglées](../../docs/AMD-DEPENDENCIES.md).

```sh
python3 -B tools/build-native-firmware-core.py --output out/native-isolation/essai-neuf
python3 -B tools/test-native-core-mutations.py --output out/native-isolation/mutations-neuves
python3 -B -m unittest discover -s tests/tools -v
```

Chaque sortie doit être neuve et sous `out/`. Le build réexporte le commit,
réextrait le SDK, conserve les notices et compile sans réutiliser d'objets.
Trois bancs ASan/UBSan sont exécutés : le modèle/logger local, les vrais
accès/binder/code PSP sur mémoire ordinaire et le contrôleur de ressources avec
doubles IOKit. Les mutations historiques portent
uniquement sur le modèle local, pas sur la nouvelle intégration PSP.

Les firmwares, archives et diagnostics restent dans `out/`, non publiés.
Licences amont conservées dans les sources et `notices/` : Navi48/MIT,
composants RDNA4FB/BSD-3, mac-amdgpu/MIT, MacKernelSDK/APSL-2.0 et notices
composants, `LICENSE.amdgpu`/`WHENCE`. Les nouveaux fichiers sont sous MIT.

## Suite avant tout essai GPU

1. Étendre la propagation des erreurs aux autres phases PSP/SMU et au futur
   contrôleur ; les accès bornés ne suffisent pas à qualifier une séquence GPU.
2. Prouver les placements console/réservations VRAM et le flush HDP sans les
   déduire du seul IORegistry ; aucun accès indirect « passif » n'est autorisé.
3. Compléter le contrôleur IOKit déjà codé pour l'acquisition/nettoyage : propriété
   GPU exclusive, réservations VRAM, cohérence, DMA/IOMMU, arrêt et événements
   du futur IOService. Ne pas transformer les observations de mappings ni les
   affirmations synthétiques des tests en preuves réelles.
4. Isoler GMC/GART et les moteurs. Autre couplage trouvé :
   `amdgpu_gmc.h` inclut `apple/gfx_tlb83.h` ; supprimer uniquement les
   `.cpp` Apple du Makefile ne constitue pas une isolation complète.
5. Reconstituer/documenter le contrat natif manquant, durcir soumission PM4,
   fermetures et concurrence ; ne pas exposer un client arbitraire.
6. Qualifier la récupération/restauration et obtenir une autorisation
   distincte avant un éventuel kext natif sur support d'essai.

Voir le [contrôleur IOKit](IOKIT-CONTROLLER.md),
le [rapport des accès raccordés](../../docs/reports/2026-10-09-native-access.md),
la [première extraction](../../docs/reports/2026-10-09-native-isolation.md)
et l'[audit initial](../../docs/reports/2026-10-09-navi48-build-audit.md).
