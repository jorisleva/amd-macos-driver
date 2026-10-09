# Étape 1 — Navi48Native.kext créé, initialisation matérielle encore ouverte

## Livrable concret

**`out/native-kext/stage1/Navi48Native.kext`**, version **0.1.0**, est un vrai
bundle noyau **x86_64 `MH_KEXT_BUNDLE`**, compilé et signé ad hoc.
`Navi48Native.zip` est fourni dans le même répertoire, avec le bundle et ses notices.

Il contient la classe IOService native, une personnalité PCI, les entrées kmod,
le contrôleur de mappings et le bloc firmware/IP/PSP/SMU/IMU déjà isolé.
**Les dix firmwares épinglés sont vérifiés dans le binaire final.** Le résultat
n'est plus seulement deux archives `.a`, ni un kext vide ou le pilote amont
complet renommé.

**Non installé, non chargé ; aucun firmware envoyé, aucune initialisation GPU
observée. L'étape 1 n'est pas déclarée terminée.**

## Raccordement codé

Sources : [`Navi48Native.cpp`](../../kexts/Navi48Native/Navi48Native.cpp),
[en-tête](../../kexts/Navi48Native/Navi48Native.hpp),
[plist](../../kexts/Navi48Native/Info.plist),
[entrées kmod](../../kexts/Navi48Native/kmod_info.c).

- Cible `1002:7550 / 1849:5417`, révision revalidée `c0`, classe graphique.
  Aucun service framebuffer/accélérateur Apple ni hook NVIDIA.
- `probe/start` exigent un opt-in distinct et un candidat mémoire explicite :
  aucune sélection implicite à 64 Mio ou ailleurs. Le candidat n'est pas une
  réservation VRAM et n'autorise aucune écriture firmware.
- Le service **possède** le contrôleur IOKit réel et conserve son bail, ses
  mappings BAR0/BAR2/BAR5 et son contexte privé désactivé.
- `IOWorkLoop`/`IOCommandGate` sérialisent les opérations. Un arrêt récursif
  durant acquisition annule le démarrage sans détruire le contrôleur en cours.
  Le retrait précède les libérations/fermetures et le `stop` parent, hors gate.
- Arrêt, terminaison et messages reçus de suspension/alimentation invalident
  les ressources. Aucun mécanisme de reprise GPU à chaud ou de puissance complète
  n'est revendiqué ; il reste requis avant publication de ressources au GPU.
- Rapport `Navi48Native,Resources` sur **notre nœud**, jamais le provider :
  BAR/adresses CPU physiques, console, candidat et masque des blocages.
  `FirmwareExecuted`, `GPUInitialized` et `AccessEnabled` restent à zéro.
- Les deux entrées de client utilisateur refusent les requêtes. Aucun service
  de commandes arbitraires ou capability d'accélération n'est publié.

Le bloc firmware est réellement lié, mais **pas invoqué** : propriété GPU,
réservations/géométrie VRAM/base MC, HDP, DMA applicable, arrêt et restauration
ne sont pas encore établis. Aucun argument « force » ne supprime ces blocages.

## Vérifications limitées au livrable

Le build réutilise les deux objets natifs déjà vérifiés, après contrôle des
empreintes, sources, imports et firmware. Il compile seulement le service et
les entrées kmod. **Pas de campagne sur l'observateur, de mutations ou de rebuild
global Navi48.**

Un smoke test du même service et contrôleur avec doubles IOKit vérifie : refus
avant PCI sans activation/placement, acquisition effective dans le modèle,
rapport 64 bits et absence de succès GPU, refus des clients, arrêt/réentrée,
échec partiel/publication, terminaison et libération d'initialisation partielle.
**109 contrôles ASan/UBSan, zéro échec.** Il ne charge pas le kext et ne simule
aucune réponse de firmware GPU.

Compilation **sans avertissement**, signature stricte valide sur disque,
format Mach-O noyau correct, dix firmwares comparés octet par octet. Les entrées
kmod privées, la classe native, le contrôleur et `psp_init` sont présents.
**308 imports noyau**, principalement la vtable héritée IOService, sont sur les
frontières contrôlées ; aucun symbole interne non résolu. **La résolution avec
le Kernel Collection Tahoe et le chargement ne sont pas qualifiés.**

La première inspection des symboles ne cherchait que les exports globaux,
alors que les trampolines SDK et `_realmain/_antimain` sont privés. Inspection
corrigée pour inclure les symboles locaux, **sans recompiler le binaire ni relancer
les tests**. Le rapport conserve cette correction de diagnostic.

| Artefact | SHA-256 |
| --- | --- |
| `Contents/MacOS/Navi48Native` | `fafc866c1ed8c330e464876d8ffecd6786f8775e14fc925055ec29596b715972` |

Rapports, imports, signatures, source exportée, sommes du bundle et firmwares
comparés : `out/native-kext/stage1/`. [Résumé JSON](2026-10-09-native-kext.json).

## Prochain objectif de l'étape 1

**Obtenir le raccordement matériel nécessaire pour initialiser réellement la
carte**, pas augmenter le nombre de tests hôte :

1. Essai de boot du nouveau kext, préparé/autorisé séparément, pour valider
   chargement et attachement réels tout en conservant l'EFI de référence.
2. Établir les données mémoire et les conditions manquantes permettant une
   réservation GPU et le protocole HDP, sans déduire « mémoire libre » de BAR0.
3. Brancher cette autorisation et le cycle de vie complet sur l'orchestration
   PSP/SMU/bootloader ; faire démarrer les blocs nécessaires. Les autres phases
   firmware restent à durcir avant invocation réelle.

Ensuite seulement : étape 2, première commande/calcul exécuté par la Radeon
sous macOS et résultat relu, puis rendu, Metal et bureau accéléré.

**OPENCORE : 101 fichiers ; PROBE1401 : 104 fichiers, inchangés** par comparaison
chemins/SHA-256 avant/après. Aucun changement de sécurité ou redémarrage ;
Navi48PciProbe n'est pas modifié. Le build écrit uniquement sous `out/`.

[Module et reproduction](../../kexts/Navi48Native/README.md)
