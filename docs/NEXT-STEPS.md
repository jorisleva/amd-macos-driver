# Travail restant après le premier banc Windows

État du 7 octobre 2026. Le traducteur Rust et le banc Vulkan compilent sur
le PC Ryzen/Radeon ; 15 tests Rust ciblés, 13 CTest et huit tests Python sont validés. Le contrôle
GLSL et la fixture synthétique traduite passent chacun 36 cas sur RX 9070 XT,
y compris les transferts staging/VRAM, sans écart ni erreur Vulkan/synchronisation.
Le Mac est désormais inventorié : `vector_add.metal` est compilé en AIR Apple,
lié en metallib, traduit et validé en SPIR-V Vulkan 1.2. Les 11 tests Rust
ciblés passent sur Mac et les sept artefacts sont reproduits octet par octet.
Le SPIR-V issu de cet AIR Apple passe maintenant les 36 cas sur la Radeon,
avec les mêmes résultats que la référence CPU et le contrôle GLSL rejoué.
Preuves : [rapport Apple AIR/Radeon](reports/2026-10-07-apple-air-radeon.md),
[rapport Mac/AIR](reports/2026-10-07-apple-air.md) et
[rapport Radeon initial](reports/2026-10-07-radeon-windows.md).
Le rendu hors écran de contrôle GLSL passe également 48 cas : copies et
échantillonnage de textures, triangle et mélange alpha. Les 330 984 pixels
RGBA correspondent exactement à la référence CPU, sans erreur Vulkan ni
corruption. Les trois rejets graphiques et la régression Apple/GLSL passent.
Preuves : [rapport graphique avec images](reports/2026-10-07-offscreen-radeon.md).
Les quatre shaders graphiques Metal sont maintenant compilés et traduits sur
Mac ; 28 artefacts sont reproduits octet par octet. La nouvelle sélection
passe 15 tests Rust, 13 CTest GPU-free et 8 tests Python sur Mac.
Preuve : [rapport graphique Metal/Mac](reports/2026-10-07-metal-graphics.md).
Ces graphiques passent maintenant 48 cas sur Radeon, sans écart ni erreur
Vulkan : leurs 96 fichiers RGBA correspondent au contrôle GLSL rejoué.
Les deux calculs passent 36 cas chacun et les trois rejets attendus passent.
Preuve : [rapport Metal/Radeon et support USB](reports/2026-10-07-metal-graphics-radeon.md).
Le support USB est préparé ; le boot Tahoe et le pilote macOS restent ouverts.
Le premier essai charge la récupération jusqu'au passage vers macOS, mais
l'utilisateur signale un blocage. Le menu a été corrigé ; la dernière ligne
visible à l'écran manque pour cibler le correctif suivant.
Preuve : [diagnostic des journaux USB](reports/2026-10-07-tahoe-boot.md).

## Prochaines actions, dans l'ordre

1. **Compléter l'inventaire du PC.** B550M DS3H, BIOS FD, Ryzen 5600X,
   16 Gio et IDs `1002:7550` / `1849:5417` sont relevés. Il reste la révision
   matérielle de la carte mère, le modèle commercial de la carte,
   les supports d'installation et l'écran/connecteur à retenir pour Tahoe.
   ASRock et VBIOS `023.008.000.068.000001` sont identifiés via ACPI VFCT ;
   Crucial P3 Plus 1 To, Ethernet Realtek et USB AMD sont désormais relevés.
2. **Conserver le banc Radeon comme contrôle de régression.** Exécuter
   `tools/run-windows-probe.ps1 -DeviceId 0x7550` après un changement pertinent.
   Les 36 cas couvrent mémoire hôte et staging/VRAM. Rejouer aussi le corpus
   réel Apple de `tests/shaders/apple/`, avec `-Shader`, `-Reflection` et
   origine `metal-air`, selon la [procédure Mac](VALIDATION-MACOS.md).
   Ce calcul est validé sous le pilote AMD Windows ; RADV Darwin reste ouvert.
   Rejouer aussi `tools/run-windows-graphics-probe.ps1 -DeviceId 0x7550` pour
   les 48 cas hors écran et `tools/test-windows-graphics-rejections.ps1`
   pour leurs scénarios d'échec, selon la [procédure graphique](VALIDATION-GRAPHICS.md).
3. **Essayer le premier démarrage Tahoe depuis le support USB préparé.**
   D: OPENCORE contient l'EFI sans accélération et la récupération Apple
   26.6.2 / 25G83 ; E: TAHOEFILES contient le paquet complet 26.7.1 / 25G241
   et l'EFI de secours. Régler l'UEFI selon le [guide](OPENCORE-TAHOE.md),
   choisir le disque USB via F12 puis **Installer macOS Tahoe (Recovery)**,
   Ethernet branché. Au blocage, relever les dernières lignes visibles à
   l'écran : le journal OpenCore seul s'arrête à `EXITBS:START`.
   Qualifier affichage, clavier, stockage, réseau, build installé et secours.
   Les fichiers et les deux EFI copiés sont validés ; le boot physique reste ouvert.
4. **Construire et auditer l'assemblage AMD sur Mac.** Qualifier Navi48,
   MacKernelSDK et Mesa/RADV avant toute installation du pilote sur Tahoe.
   Les dispatchs dépendants, files multiples et mémoire non cohérente du
   banc Windows restent également à étendre après un changement pertinent.

Commandes détaillées : [banc Windows](VALIDATION-WINDOWS.md) et
[compilation AIR sur Mac / transfert](VALIDATION-MACOS.md). Le MacBookAir7,2
x86_64 sous Sequoia est qualifié pour ce premier shader ; la compilation de
l'assemblage AMD reste à faire. Le nouveau test `apple_vector_add` est ajouté
aux scripts Mac/Windows et ses trois traductions de référence sont désormais
vérifiées sous Windows également. Les quatre régressions `apple_graphics`
portent la sélection à 15 tests, désormais tous rejoués avec succès sous Windows.

## Compilation et dépendances encore ouvertes

- Obtenir les trois fixtures de validation manquantes par une provenance
  autorisée, ou organiser leur couverture avec des cas écrits pour le projet.
  La suite unitaire complète ne compile pas ; les quatre balayages du corpus
  absent sont explicitement filtrés dans les suites sélectionnées.
- Construire et auditer `translator/wrapper`, non qualifié à ce stade.
- MacKernelSDK et linux-firmware sont maintenant épinglés : archive SDK,
  notices et dix SHA-256 firmware vérifiés. Rejouer le [préparateur](AMD-DEPENDENCIES.md)
  sur le Mac avant compilation ; le pinning ne vaut pas qualification du build.
- Construire le kext Navi48 x86_64 sur Mac, puis Mesa à la révision RADV
  attendue avec les patches 0001 à 0005. Vérifier ABI N48N, architectures,
  symboles, dépendances et signatures ; reconstruire depuis une copie propre.
- Résoudre l'absence publique de `notes/design/NATIVE-S1C-ABI.md`, déclaré
  normatif par le header N48N, avant qualification du transport.

Révisions épinglées : [sources.lock.json](../dependencies/sources.lock.json).
Découpage du fork et contrats proposés : [audit AMD](AMD-INTEGRATION.md).

## Avant les essais noyau et l'intégration Metal

- L'EFI de référence pour ce PC et le profil de secours sont générés et
  passent `ocvalidate` 1.0.8 après copie sur le support USB choisi. La récupération
  est 26.6.2 / 25G83 et le paquet complet est 26.7.1 / 25G241. Choisir la
  partition cible, puis essayer l'[installation et la récupération](OPENCORE-TAHOE.md)
  sur le PC avant toute installation du pilote expérimental. Le kit vise
  l'affichage de base ; il ne prouve pas encore que Tahoe démarre sur cette carte.
- Auditer ou extraire un module d'identification PCI sans initialisation GPU,
  vérifier les BAR et refuser les identités différentes de la cible relevée.
- Qualifier ensuite firmware, mémoire, commandes et complétions natives,
  avec résultats relus et erreurs bornées ; puis le calcul et le rendu RADV.
- Adapter la couche Metal aux contrats AMD : séparer les exports NVK,
  remplacer le partage IOSurface et désactiver les voies NVIDIA/NVENC.
- Qualifier le rendu hors écran avant WindowServer, puis stabilité, ressources,
  applications, récupération et paquet alpha avec notices et empreintes.

Les étapes complètes et critères de passage restent dans [ROADMAP.md](../ROADMAP.md).
Le [rapport initial](reports/2026-10-07-bootstrap.md) précise les preuves
obtenues et les essais non exécutés.
