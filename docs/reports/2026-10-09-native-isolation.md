# Retour OPENCORE et première isolation native Navi48 — 9 octobre 2026

## Résultat

**Le retour à OPENCORE après l'essai ON est observé.** Le premier sous-ensemble
natif firmware/IP/PSP/SMU/IMU est ensuite construit séparément, **en bibliothèque
non chargeable**, pas en nouveau kext.

- Deux exports/builds neufs donnent un objet et une archive identiques octet
  par octet, sans avertissement.
- Neuf unités C++ amont, deux unités locales et dix unités firmware ; aucun
  contrôleur de pilote, hook Apple, client utilisateur ou affichage lié.
- Dix firmwares vérifiés dans l'objet lié ; treize imports noyau explicites,
  aucun symbole interne manquant. Liaison réelle avec Tahoe **non qualifiée**.
- **1 582 contrôles ASan/UBSan** du modèle local et du logger ; **22/22 mutations
  compilées et détectées** ; **61 tests Python** réussis.
- Aucun code matériel exécuté, firmware envoyé, kext installé/chargé, EFI
  modifiée ou réglage de sécurité changé par ces outils.

**Il ne s'agit pas de l'isolation complète d'un pilote fonctionnel**, ni d'une
initialisation GPU. Les contrôles positifs du modèle sont synthétiques et
n'autorisent pas l'utilisation matérielle des fonctions amont.

## 1. Retour à la référence

Boot : **11:31:49 UTC**, Tahoe **26.7.1 / 25G241**, Darwin **25.6.0**, x86_64.
Collecte initiale terminée à 11:48 UTC, puis nouvelle vérification des EFI et
des modules chargés après le travail local.

| Vérification | Résultat |
| --- | --- |
| Argument `navi48-pci-probe` | Absent |
| `com.amd-macos-driver.Navi48PciProbe` dans `kmutil showloaded` | Absent |
| Service `Navi48PciProbe` dans IORegistry | Absent |
| `com.navi48.bringup` chargé | Non |
| Nœud `IOAccelerator` | Aucun observé |
| SIP et authenticated root | Activés |
| EFI habituelle OPENCORE | 101 fichiers identiques au profil `00-reference` |
| EFI PROBE1401 | 104 fichiers identiques au profil `02-enabled` |

Arguments reçus :

```text
-v keepsyms=1 debug=0x100 navi48bringup=0 rdna4-off=1 -liludbgall
```

La **session est donc revenue à la référence**, tandis que **la clé PROBE1401
reste ON**. Aucun de ses fichiers n'a été modifié. L'essai OFF archivé et les
sauvegardes ne sont pas restaurés pendant cette étape.

Ce retour ne simule **pas** la réparation d'une EFI corrompue. La restauration
de sauvegarde, les démarrages répétés/à froid, la stabilité prolongée et la
couverture complète des périphériques restent à qualifier.

## 2. Coupe native réellement compilée

Module : [`native/Navi48FirmwareCore`](../../native/Navi48FirmwareCore/).
Provenance : Navi48 `696959753070e9a16677be485580dc53b06a7ab5` ; SDK et
firmwares inchangés par rapport au manifeste épinglé.

La liste explicite comprend :

```text
ipdiscovery.cpp                  psp.cpp
amd/amdgpu_discovery.cpp          amd/amdgpu_ucode_extract.cpp
amd/fw_loader.cpp                 amd/fw_table.cpp
amd/psp_v14_0.cpp                  amd/smu_v14_0.cpp
amd/imu_v12_0.cpp
```

Quinze en-têtes amont sont nécessaires. Le build exporte seulement ces fichiers
vers son arbre d'inclusion ; le reste de l'export amont n'est pas une racine
`-I`. Les traces `-MD` de Clang confirment les **38 fichiers source/en-têtes**
attendus, plus les en-têtes du SDK vérifié. Le checkout amont n'est pas modifié.

Sont exclus de cette coupe :

- `Navi48Bringup`, ses personnalités, son `start()` et ses points d'entrée kmod ;
- l'orchestrateur `amdgpu_init.cpp`, dont le rappel
  `navi48_sdmadcc_default()` dépend encore du contrôleur monolithique ;
- `.cpp` Apple, hooks privés, anciens clients, DCN/affichage et Metal ;
- GMC/GART, allocation sysmem/DMA, CP/MES/SDMA, tests compute et transport
  natif S1b/S1c. Le type sysmem inclus par PSP ne constitue pas son implémentation.

Ces éléments ne sont ni remplacés par un GPU logiciel ni simulés par des
fonctions qui renverraient un faux succès. Ils sont **absents du produit**.

### Liens cachés identifiés

1. Le logger amont dépend de l'ancien ABI utilisateur et de callbacks de
   mesure de performance définis dans `Navi48Bringup`/la couche Apple.
   La seule modification amont du sous-ensemble est le routage de
   `amd/amdgpu_log.h` vers `NativeLog.cpp`. Le diff exact est conservé dans
   chaque sortie. Le nouveau logger utilise réellement `IOLog`, sans tampon
   global récupérable par ancien user-client, et signale la troncature.
2. `amd/amdgpu_gmc.h` inclut **`apple/gfx_tlb83.h`**, malgré son emplacement
   dans le répertoire AMD. Ce bloc est laissé hors de la première coupe.
   Retirer les `.cpp` Apple du Makefile ne suffit donc pas à isoler la suite.

Le build exige un **Mach-O `MH_OBJECT`**, sans constructeur/destructeur
automatique, dépendance dylib ni point d'entrée de pilote. Il crée ensuite une
archive statique déterministe. Il n'y a ni plist, ni packaging/signature kext,
ni commande d'installation.

## 3. Modèle de refus et de propriété

Le nouveau code `Preflight` vérifie les six champs d'identité exacts,
l'activation explicite, des géométries VRAM/console/fenêtre connues, la
réservation explicite et non chevauchante, des bornes 64 bits sans débordement,
la propriété exclusive, une qualification DMA et les preuves de
restauration/nettoyage. Il efface le plan sur tout refus.

**Aucun choix implicite de VRAM à 64 Mio.** Un retour au boot de référence
n'est pas assimilé à une restauration testée ; une adresse physique CPU
n'est pas assimilée à une adresse DMA qualifiée.

Le modèle d'états interdit :

- de continuer l'initialisation après un échec avant exposition au GPU ;
- de libérer une ressource potentiellement détenue par le GPU ;
- de transformer une panne en preuve d'arrêt ;
- de sortir de quarantaine par déchargement/rechargement à chaud ;
- de libérer deux fois la même ressource.

**Ce code n'est pas encore un contrôleur matériel.** Aucun adaptateur Tahoe
ne mesure les preuves ni ne raccorde les fonctions amont à ces refus. Les
verrous, l'arrêt effectif du GPU et le nettoyage d'allocations partielles
restent à implémenter/qualifier. L'issue positive s'appelle
`ClaimsConsistent`, pas « matériel autorisé ».

## 4. Vérifications et artefacts

Outil : [`tools/build-native-firmware-core.py`](../../tools/build-native-firmware-core.py).
Command Line Tools, Clang 21, SDK macOS 26.5, cible noyau x86_64/macOS 11 ABI.
Pas de réseau, pas de réutilisation des objets du build Navi48Bringup complet.

| Artefact identique dans les deux builds | SHA-256 |
| --- | --- |
| `Navi48FirmwareCore.o` | `7917284c17022ac2d6b7d3b9064140cf74a811783e589a3977d3c451376cbafb` |
| `libNavi48FirmwareCore.a` | `839eaf844bb4886828094a7482ee7a8f9009d8af1a4d10a69916706af1b823c1` |

Sorties retenues : `out/native-isolation/final-a/` et `final-b/`.
Le premier `build-a/` est une itération antérieure : le modèle permettait
encore de poursuivre après un échec avant publication. La relecture a conduit
à un état terminal libérable, couvert par le banc et une mutation dédiée.
**Les chiffres et empreintes ci-dessus portent sur les builds finaux.**

Les 1 582 contrôles exécutent le **code local** de planification, d'états et de
journalisation, compilé sous ASan/UBSan. Ils ne lancent pas les neuf unités
matérielles amont et ne prouvent ni la sûreté de tous leurs accès mémoire ni
leur comportement dans le vrai noyau. Les 22 mutations altèrent les refus et
transitions du modèle ; toutes compilent, échouent sur des assertions du banc,
aucune ne s'échappe ou n'expire. Ce n'est pas la campagne de 160 mutations
amont précédemment exécutée.

Les 61 tests Python comprennent neuf nouveaux tests d'audit de la coupe :
manifeste, chemins/dépendances, transformation du logger, type Mach-O,
initialiseurs, imports et intégrité des firmwares. Les régressions de
l'observateur et des EFI restent incluses. L'observateur binaire n'est pas modifié.

Preuves privées : `out/native-isolation/reference-20261009/`, `final-a/`,
`final-b/`, `mutations/`, `python-tests.log`, `final-state.json`. Synthèse
publique : [JSON](2026-10-09-native-isolation.json). Ne pas publier les EFI,
SMBIOS, journaux OpenCore bruts ou collectes privées.

## 5. Prochaine coupe, sans activation GPU

1. Durcir les accès réellement utilisés dans `amdgpu_regs.h`. L'extraction
   conserve notamment des additions `offset + taille` avant comparaison,
   des erreurs silencieuses et une dernière écriture pouvant être arrondie
   à un mot. Le modèle extérieur ne répare pas ces fonctions.
2. Définir une façon démontrable de connaître console et réservations VRAM,
   sans reprendre les lectures indirectes qui écrivent dans MM_INDEX.
3. Concevoir propriété, DMA/IOMMU et arrêt ; raccorder le contrôleur aux refus,
   tester les échecs partiels, puis extraire les blocs mémoire et commandes.
4. Documenter le contrat S1c dont la note normative manque dans le commit
   public ; ne pas exposer la soumission PM4 à un client non fiable.
5. Essayer la récupération/restauration avant de demander une autorisation
   distincte pour un premier kext matériel. Pas de chargement du bundle amont
   complet ou de déchargement à chaud comme méthode de récupération.

Les [constats A1–A7](2026-10-09-navi48-build-audit.md) ne sont donc pas déclarés
résolus globalement. RADV Darwin et l'intégration Metal restent ultérieurs.
