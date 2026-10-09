# Accès natifs raccordés et code PSP durci — 9 octobre 2026

## Changement concret

La [première extraction](2026-10-09-native-isolation.md) compilait les fonctions
amont avec un modèle de préconditions séparé. Désormais, les unités AMD du
module utilisent **notre interface d'accès mémoire**, et trois chemins PSP
réels sont adaptés : création du layout, staging firmware, soumission GPCOM.

Ce n'est toujours **pas un kext chargeable**, une initialisation réelle ou un
calcul Radeon. Le produit reste une bibliothèque statique, sans IOService,
personnalité, user-client ou installation. Le plugin Metal/NVIDIA n'est pas
modifié pendant cette étape ; son raccordement AMD reste ultérieur.

## Code livré

- [`AmdGpuAccess.hpp`](../../native/Navi48FirmwareCore/AmdGpuAccess.hpp) :
  remplace `amd/amdgpu_regs.h` dans l'export compilé. Accès désactivés par
  défaut ; limites BAR0/BAR2/MMIO par soustraction, alignements, réservation
  VRAM, erreurs d'adressage IP/MC et sources hôte vérifiés. Les copies de
  taille non multiple de quatre sont refusées intégralement, pas arrondies.
- [`MappedAccess.cpp`](../../native/Navi48FirmwareCore/MappedAccess.cpp) :
  raccorde les préconditions à des fenêtres déjà possédées par l'appelant.
  Refuse absence de preuve, contexte utilisé, mappings invalides/chevauchants,
  origine BAR0 non nulle et MC inconnu/débordant. Aucun accès n'a lieu pendant
  cette liaison. **Il ne fabrique pas les preuves et ne mappe pas le PCI.**
- [`0003-psp-access-errors.patch`](../../native/Navi48FirmwareCore/patches/0003-psp-access-errors.patch) :
  modification explicite du vrai `amd/psp_v14_0.cpp`, contrôlée par empreinte.
  Le checkout épinglé reste intact ; seules les copies de build changent.

### Refus maintenant appliqués

Une erreur d'accès est mémorisée, journalisée et bloque les opérations
suivantes. Pas de remise à zéro/réactivation fournie. Les lectures indirectes
MM_INDEX, SMN et PCIe sont explicitement non prises en charge : elles auraient
écrit dans des registres d'index sans contrat de verrouillage qualifié.
Le flush HDP exige une configuration explicitement qualifiée ; il n'est plus
silencieusement sauté. Une sentinelle de lecture invalide ne devient pas un
acquittement dans les fonctions de polling.

Côté PSP :

- le layout entier de 24 Mio est contrôlé avant publication ; un MC observé
  différent de celui déclaré fait échouer l'opération, au lieu d'un avertissement ;
- le staging refuse dépassement/overflow, copie incomplète et MC incohérent ;
  une erreur ne renvoie aucune adresse ni nouvelle allocation ;
- la soumission refuse ring nul/tronqué, pointeur de ring mal aligné/hors plage,
  buffers qui se chevauchent, adresse MC incorrecte et rebouclage du compteur ;
- le pointeur de commande n'est pas publié après une erreur d'accès/flush ;
- une erreur d'accès pendant l'attente est propagée avant toute comparaison
  avec la fence attendue ; une réponse PSP d'erreur n'est pas un succès.

## Vérification du même code, sur RAM uniquement

[`access_test.cpp`](../../tests/native-core/access_test.cpp) est lié au
**même fichier PSP patché**, au binder et aux helpers utilisés par le build
noyau. Les fenêtres sont des allocations ordinaires du processus, pas des
mappings de la Radeon. Le banc compare les octets copiés, vérifie les gardes,
relit la trame GPCOM et injecte explicitement une réponse simulée dans cette RAM.
Il couvre aussi une disparition de mapping après publication et les erreurs
firmware renvoyées. Ce n'est ni une exécution de firmware ni une preuve de
cohérence DMA, de complétion ou de calcul par le GPU.

Résultats finaux : **398 contrôles de ce nouveau chemin sous ASan/UBSan**,
zéro échec ; les **1 582 contrôles du modèle/logger** restent réussis ;
**63 tests Python** passent. Aucune nouvelle campagne de mutations des accès
n'est revendiquée. Deux builds neufs x86_64 sont identiques, sans avertissement ;
dix firmwares sont vérifiés dans l'objet. Treize imports noyau restent,
sans symbole interne manquant ; liaison réelle avec Tahoe non qualifiée.

| Artefact identique dans les deux builds | SHA-256 |
| --- | --- |
| `Navi48FirmwareCore.o` | `0e4d37fa8add2924ab05e867cdc5fcfc5b7254ad46d79ced0d21cf7a37dd9fb3` |
| `libNavi48FirmwareCore.a` | `305e1f9235dbcc8d671a506e469bb4dce57f1a9c41bbab61379633a2fa763f07` |

Sorties retenues : `out/native-access/final-a/` et `final-b/`.
`build-a/` a échoué à la compilation du banc : la macro de substitution du
logger réécrivait l'appel libc du test ; corrigé par `#undef` dans ce test,
sans supprimer l'avertissement. `build-b/` est une itération intermédiaire.
Les sources firmware générées ne diffèrent entre builds finaux que par le
chemin de build dans deux lignes de commentaire ; les objets sont identiques.
[Rapport JSON](2026-10-09-native-access.json).

## Limites et prochaine tâche

Les cas positifs déclarent des preuves **synthétiques**. Aucun adaptateur IOKit
ne prouve actuellement propriété exclusive, console/réservations VRAM,
cohérence HDP ou DMA/IOMMU sur cette machine. Le binder ne transforme pas
ces affirmations en mesures. La validité/durée de vie des mappings et des
sources hôte ainsi que la sérialisation restent des obligations de l'appelant.
Les budgets de polling ne prouvent pas un délai mural face à un accès PCI bloqué.

Les autres phases PSP/SMU et le bootloader à callbacks de `psp.cpp` ne sont pas
encore qualifiés bout en bout. La prochaine tâche est le **contrôleur de
plateforme IOKit**, avec preuves, durée de vie et propagation des erreurs de
chaque phase ; pas l'ajout de tests à l'observateur PCI. Arrêt réel,
quarantaine, restauration et chemin mémoire/commandes compute restent ouverts.

Vérification finale : OPENCORE **101 fichiers** et PROBE1401 **104 fichiers**
inchangés. La session reste sur OPENCORE sans module d'essai ; la clé conserve
ON. Aucun firmware envoyé, chargement noyau, redémarrage, réglage de sécurité
ou changement EFI effectué. Les diagnostics et binaires restent sous `out/`.
