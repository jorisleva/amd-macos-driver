# Travail restant après le premier banc Windows

État du 7 octobre 2026. Le traducteur Rust et le banc Vulkan compilent sur
le PC Ryzen/Radeon ; 10 tests Rust ciblés et 4 tests CTest passent. Le contrôle
GLSL et la fixture synthétique traduite passent chacun 36 cas sur RX 9070 XT,
y compris les transferts staging/VRAM, sans écart ni erreur Vulkan/synchronisation.
Preuves : [rapport Radeon](reports/2026-10-07-radeon-windows.md).
L'AIR Apple et les essais macOS restent ouverts.

## Prochaines actions, dans l'ordre

1. **Compléter l'inventaire du PC.** B550M DS3H, BIOS FD, Ryzen 5600X,
   16 Gio et IDs `1002:7550` / `1849:5417` sont relevés. Il reste la révision
   matérielle de la carte mère, le fabricant exact/VBIOS de la carte, le
   stockage et les périphériques de démarrage, ainsi que l'écran/connecteur
   à retenir pour Tahoe.
2. **Conserver le banc Radeon comme contrôle de régression.** Exécuter
   `tools/run-windows-probe.ps1 -DeviceId 0x7550` après un changement pertinent.
   Les 36 cas couvrent mémoire hôte et staging/VRAM. Le contrôle GLSL et
   l'IR synthétique sont validés sous le pilote AMD Windows ; ils ne ferment
   pas la qualification AIR Apple ou RADV Darwin.
3. **Qualifier le Mac de compilation.** Relever modèle, macOS/build, Xcode,
   SDK Metal et outils LLVM/SPIR-V. Exécuter `tools/build-translator.sh`, puis
   `tools/compile-metal-reference.sh`. Vérifier le ciblage AIR, les versions
   d'outils et la lecture du bitcode par `llvm-dis`. Ces scripts n'ont pas
   encore été exécutés sur un Mac.
4. **Exécuter le shader issu de Metal sur la Radeon.** Transférer AIR, metallib,
   SPIR-V, réflexion et empreintes du Mac. Lancer le même banc avec `-Shader`
   et `-Reflection`, puis comparer tous les cas. Le cas synthétique livré
   n'est pas un AIR produit par Apple et ne remplace pas cette validation.
5. **Compléter le corpus graphique.** Ajouter texture, triangle et mélange
   de couleurs hors écran, références d'images et tolérances. Le banc actuel
   couvre des buffers hôte/cohérents et device-local avec staging, du calcul
   entier et une relecture après écriture. Les dispatchs dépendants, files
   multiples et mémoire non cohérente restent également à étendre.

Commandes détaillées : [guide Windows et Mac](VALIDATION-WINDOWS.md).

## Compilation et dépendances encore ouvertes

- Obtenir les trois fixtures de validation manquantes par une provenance
  autorisée, ou organiser leur couverture avec des cas écrits pour le projet.
  La suite unitaire complète ne compile pas ; les quatre balayages du corpus
  absent sont explicitement filtrés dans les suites sélectionnées.
- Construire et auditer `translator/wrapper`, non qualifié à ce stade.
- Figer un checkout MacKernelSDK et sa licence ; figer la provenance des
  firmwares AMD, vérifier les dix SHA-256 attendus et conserver les notices.
  Les révisions SDK et linux-firmware restent `null` dans le manifeste.
- Construire le kext Navi48 x86_64 sur Mac, puis Mesa à la révision RADV
  attendue avec les patches 0001 à 0005. Vérifier ABI N48N, architectures,
  symboles, dépendances et signatures ; reconstruire depuis une copie propre.
- Résoudre l'absence publique de `notes/design/NATIVE-S1C-ABI.md`, déclaré
  normatif par le header N48N, avant qualification du transport.

Révisions partielles : [sources.lock.json](../dependencies/sources.lock.json).
Découpage du fork et contrats proposés : [audit AMD](AMD-INTEGRATION.md).

## Avant les essais noyau et l'intégration Metal

- Préparer un disque Tahoe de test, fixer version/build et configuration
  OpenCore adaptée au PC. Valider un démarrage de référence et essayer le
  moyen de récupération avant toute installation du pilote expérimental.
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
