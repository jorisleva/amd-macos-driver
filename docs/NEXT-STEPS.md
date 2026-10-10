# Travail restant — Hackintosh Tahoe

## Dernier état : boot 0.2.1, refus BAR0 exact, aucun calcul

**10 octobre : le bon kext est réellement chargé**, UUID et cinq arguments
conformes. Aucun nœud/instance `Navi48Native`, aucun rapport Resources/Compute ;
deux fermetures du bail PCI par le client natif pendant le boot. Initialisation,
fences et résultats GPU non validés. Capture `sudo dmesg` lue dans ce même
boot : buffer de 128 Kio écrasé, aucune ligne native, refus exact inconnu.
Correctif **0.2.1** : diagnostic IOResources destiné à survivre au retrait,
**booté le 10 octobre à 12:31:43 UTC**, module/UUID/arguments conformes.
**BootDiagnostics réellement lu : Checkpoint 10, `InvalidMap`, BAR0 `0x10`,
DMA/compute non observés, `HardwareTouched=0`.** Prochaine action : isoler la
propriété exacte du mapping BAR0 rejetée par `checkMaps()`, corriger sans
masquer la divergence, puis remplacement revu avant un nouveau boot. Aucun
retry/reload dans le noyau courant.
[Boot 0.2.1](reports/2026-10-10-native-0.2.1-boot.md) ·
[Boot 0.2.0](reports/2026-10-10-native-compute-boot.md) ·
[correctif](reports/2026-10-10-native-boot-diagnostics.md) ·
[déploiement 0.2.1](reports/2026-10-10-native-diagnostics-deployment.md).

## Préparation 0.2.0, historique du 9 octobre

PROBE1401 injectera maintenant **0.2.0**, avec chemin firmware/GMC/GART/CP/MES/GFX
appelé par le service et deux dispatchs gfx1201, fences et 64 comparaisons.
321 + 162 + 29 + 39 contrôles RAM/doubles, 78 tests Python, dix firmwares/deux
shaders vérifiés et zéro avertissement. OPENCORE intact, ancienne EFI 0.1.2
sauvegardée. **Aucun boot/chargement/init/calcul Radeon natif encore observé.**
Essai explicitement risqué ; ressources hardware retenues jusqu'au reboot,
pas de sommeil/retry/unload, pas de Metal ou de client pour programmes libres.
[Procédure actuelle](NATIVE-COMPUTE-ESSAI.md) · [rapport](reports/2026-10-09-native-compute.md).

## Historique, 9 octobre 2026

Le système installé **Tahoe 26.7.1 / 25G241** est exécuté sur le Ryzen/Radeon,
avec affichage de base mais zéro périphérique Metal. L'EFI active est
sauvegardée sur OPENCORE et localement, 98 fichiers vérifiés. **Le secours
n'est pas encore essayé.** Navi48Bringup 0.0.620 compile depuis deux arbres
neufs : dix firmwares liés conformes, signatures ad hoc valides, 2 905
contrôles logiciels sous ASan/UBSan, 160 mutations détectées et 21 tests Python.
Navi48Bringup complet non chargé ; RADV Darwin non construit.

L'audit exclut le bundle amont comme premier module PCI passif : son mode
« read-only » écrit et son plist conserve des personnalités Apple. Détails,
artefacts, procédure et limites :
[rapport Hackintosh / Navi48](reports/2026-10-09-navi48-build-audit.md).

**Suite réalisée :** observateur indépendant `Navi48PciProbe` 0.1.0,
propriétés IORegistry uniquement, sans accès PCI/GPU direct. Deux bundles
identiques, 1 048 contrôles ASan/UBSan, 25 mutations détectées et désormais
31 tests Python. À cette phase d'isolation : non installé/non chargé. L'essai du secours est différé à la
demande de l'utilisateur. Un changement de `config.plist` indépendant du build
est détecté, non annulé ; la copie initiale et le fichier actuel ne sont plus
identiques. Voir le [rapport d'isolation PCI](reports/2026-10-09-pci-probe-isolation.md).

**EFI d'essai désormais préparées :** nouvelle référence exacte de l'EFI actuelle
(101 fichiers), OFF et ON (104 chacun), soit un paquet de 315 fichiers vérifiés
localement et sur OPENCORE. EFI active intacte, profils non activés. `ocvalidate`
réussi, 294 noms d'imports trouvés dans le BootKC (pas une liaison qualifiée),
44 tests Python. [Procédure](PCI-PROBE-EFI-ESSAI.md) · [Preuves](reports/2026-10-09-pci-probe-efi.md).

**Premier boot OFF observé sur PROBE1401 à 09:53 UTC :** retour au bureau,
argument 0, injection réussie, module 0.1.0 effectivement chargé (UUID conforme),
aucun nœud attaché. Les deux EFI restent inchangées. Une erreur de notification
`kernelmanager_helper` est consignée, sans empêcher le chargement observé.
**Premier boot ON observé à 11:05 UTC :** module attaché au provider Radeon,
six champs d'identité et cinq ressources conformes au relevé du même boot.
Les flags XML signés sont comparés sur 32 bits, sans tronquer les adresses.
52 tests Python passent. Le module n'est pas modifié ; les deux EFI restent
intactes. Pas d'accélération ni d'accès matériel direct qualifié. [Résultat ON](reports/2026-10-09-pci-probe-on-boot.md).
[Déploiement](reports/2026-10-09-probe1401-deployment.md) ·
[Preuves du boot OFF](reports/2026-10-09-pci-probe-off-boot.md).

**Retour OPENCORE vérifié à 11:31 UTC :** absence de l'argument d'essai, du
module chargé et du nœud observateur. Les deux EFI sont inchangées ; la clé
PROBE1401 reste ON. Première isolation native : bibliothèque firmware/PSP/SMU
non chargeable, deux builds identiques, dix firmwares liés vérifiés, zéro
avertissement, 1 582 contrôles du modèle/logger local, 22 mutations détectées,
61 tests Python. Ce modèle n'est pas raccordé à un contrôleur matériel ;
aucun accès GPU exécuté. [Rapport](reports/2026-10-09-native-isolation.md) ·
[Module et reproduction](../native/Navi48FirmwareCore/).

**Suite codée : accès et PSP raccordés.** La bibliothèque utilise désormais
les accès bornés et le binder de préconditions ; layout, staging et soumission
PSP sont modifiés. Le même code est exécuté sur RAM, avec contrôle des octets,
de la trame et d'une réponse simulée : 398 contrôles ASan/UBSan, 63 tests Python,
deux builds identiques sans avertissement. Aucune preuve matérielle n'est
inventée : l'adaptateur IOKit reste absent, donc pas d'activation GPU.
[Rapport](reports/2026-10-09-native-access.md).

**Suite codée : contrôleur de ressources IOKit séparé.** Il acquiert et conserve
BAR0/BAR2/BAR5, compare PCI/descripteur/mapping, contrôle le placement console
rapporté et nettoie les échecs partiels hors verrou. Deux builds identiques,
sans avertissement, 2 241 contrôles ASan/UBSan avec doubles IOKit, 66 tests Python.
Aucun appel au provider Radeon réel ; contexte privé et désactivé. Bail IOKit,
cache demandé et candidat hors console ne prouvent pas propriété GPU,
réservation VRAM ou cohérence HDP. Les sept familles de preuves manquantes
restent bloquantes ; aucune autorisation PSP. Les deux EFI sont inchangées.
[Contrat](../native/Navi48FirmwareCore/IOKIT-CONTROLLER.md) ·
[Rapport](reports/2026-10-09-native-platform.md).

**Étape 1 engagée : `Navi48Native.kext` 0.1.0 créé.** Vrai bundle noyau x86_64,
service IOKit et contrôleur raccordés, code firmware/PSP/SMU/IMU et dix blobs
vérifiés dans le binaire final. Compilation sans avertissement, signature ad hoc
valide ; contrôle ciblé du cycle de vie, pas de nouvelle campagne d'observateur.
Non installé/non chargé, aucun firmware envoyé. Les conditions mémoire/HDP/
propriété/arrêt manquantes bloquent l'initialisation GPU ; **l'étape 1 n'est pas
terminée et il n'y a pas d'accélération**. Objectif suivant : raccordement
matériel du kext permettant l'initialisation réelle.
[Livrable](reports/2026-10-09-native-kext.md) · [Module](../kexts/Navi48Native/).

**Suite 0.1.1 : mémoire DMA implémentée, pas de résultat GPU.** `DmaBuffer`
produit les pages IOVM via IODMACommand, gère les copies/synchronisations et
conserve les ressources potentiellement GPU en quarantaine. Compilé et lié,
mais pas encore appelé par le service ni relié à GMC/GART. Les étapes 1
(initialisation) et 2 (commande réelle/résultat relu) ne sont pas réalisées.
[État et raccordements restants](reports/2026-10-09-native-dma.md).

**Suite 0.1.2 : allocation DMA raccordée au service et essai natif déployé.**
Acquisition sous gate, préparation de 64 Kio via IODMACommand hors gate,
annulation/revalidation/finalisation, nettoyage DMA avant fermeture PCI.
234 contrôles service/DMA, 2 271 contrôleur et 76 tests Python passent.
`PROBE1401/EFI` contient le kext activé ; ses 112 fichiers et sa signature sont
vérifiés, l'ancienne EFI est sauvegardée et les 101 fichiers OPENCORE intacts.
**Démarrage restant : F12 → PROBE1401 UEFI → Tahoe installé**, puis observer
module 0.1.2 et ressources PCI/DMA réelles. Pas de chargement dans la session
courante, pas de firmware/commande/calcul Radeon ; initialisation et soumission
restent à implémenter. [Procédure](NATIVE-KEXT-ESSAI.md) ·
[Rapport](reports/2026-10-09-native-load-preparation.md).

## Historique des validations et du démarrage

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
Les journaux chargent la récupération jusqu'au passage vers macOS ; la photo
montre ensuite le noyau jusqu'à `CoreAnalyticsHub start completed`, sans
progression pendant plus de cinq minutes en DisplayPort. Le menu est corrigé
et le profil `-x` a affiché ensuite le symbole d'interdiction. Le SMBIOS
MacPro7,1 est vérifié. Le profil `-x` avec transfert USB affiche le même
symbole. Le nouveau journal du 8 octobre situe cet arrêt au chargement
mémoire du noyau, avant `EXITBS:START`. Le retour au mode normal atteint
de nouveau CoreAnalytics puis bloque, avec extinction de la LED du disque.
Le clavier ne répond plus à ce stade. À la demande de l'utilisateur, le profil
retire USBToolBox, UTBDefault, USBX et l'essai de transfert USB ;
la cartographie est différée. Cet essai bloque encore. Le nouvel essai retire
aussi WhateverGreen et ses arguments, sans autre modification graphique.
Aucun journal nouveau ne confirme un kernel panic lié au GPU. Le démarrage reste ouvert.
Preuve : [diagnostic des journaux USB](reports/2026-10-07-tahoe-boot.md).
Historique précédent : [symbole d'interdiction et SMBIOS](reports/2026-10-08-tahoe-prohibitory.md).

**Premier retour du 9 octobre, avant la session ci-dessus :** l'EFI RapidEFI 5.8.0 adaptée permet,
selon le retour utilisateur, l'accès au menu d'installation. Son réglage
chinois est identifié ; la correction française est préparée et validée.
L'installation et la qualification du système complet restent ouvertes.
Réglages, sauvegarde et commandes Git pour le Mac :
[rapport RapidEFI](reports/2026-10-09-rapidefi.md).

## Prochaines actions, dans l'ordre

1. **Compléter l'inventaire du PC.** B550M DS3H, BIOS FD, Ryzen 5600X,
   16 Gio et IDs `1002:7550` / `1849:5417` sont relevés. Il reste la révision
   matérielle de la carte mère, le modèle commercial de la carte,
   la partition d'installation et le modèle exact de l'écran LG.
   Le connecteur du premier essai est confirmé : DisplayPort sur la Radeon.
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
3. **Préserver les états OpenCore.** La sauvegarde
   `OPENCORE/PROFILS-TAHOE/avant-navi48-20261008T235644Z` est vérifiée ;
   son essai est différé, pas qualifié. Conserver aussi les changements du
   fichier actuel décrits dans le rapport, sans les écraser. Relever clavier,
   stockage, réseau et diagnostics sur plusieurs démarrages ; pas de réinstallation
   pour rejouer les instructions historiques ci-dessus.
4. **Étape 1 : faire initialiser la Radeon par `Navi48Native.kext`.** Le vrai
   bundle est créé et le chargement noyau 0.2.0 est observé, mais firmware réel
   et initialisation non validés. Le profil natif **0.2.1** est désormais sur PROBE1401 :
   orchestration firmware/GMC/GART/CP/MES/GFX et deux shaders raccordés au service.
   Le premier boot du 10 octobre a validé le chargement, mais le service se
   retire sans rapport GPU. Le buffer privilégié est maintenant lu : messages
   de boot écrasés. Le correctif diagnostic persistant 0.2.1 est déployé,
   chargé le 10 octobre ; signature/ocvalidate/hashes vérifiés et ancienne EFI
   sauvegardée. Le diagnostic réel est `InvalidMap` sur BAR0. Isoler le champ
   exact rejeté, corriger sans retry/reload. Puis observer
   étapes matérielles, fences et résultats Radeon selon [la procédure](NATIVE-COMPUTE-ESSAI.md).
   Référence OPENCORE intacte. Établir/qualifier ensuite console, réservations,
   géométrie/base MC, propriété GPU exclusive, HDP, DMA et puissance/restauration.
   L'essai explicite ne transforme pas les conditions inconnues en Claims vrais.
   Le service chargé ne signifie pas GPU initialisé : étapes 1/2 exigent
   initialisation réelle, calcul réellement exécuté et résultat relu. Ne pas élargir
   l'observateur, répéter des campagnes hôte sans cette cible ni charger
   Navi48Bringup complet.
5. **Construire Mesa/RADV Darwin**, puis le qualifier seulement après les
   essais natifs. Le banc Windows devra aussi être étendu aux dispatchs
   dépendants, files multiples et mémoire non cohérente.

Commandes détaillées : [banc Windows](VALIDATION-WINDOWS.md) et
[compilation AIR sur Mac / transfert](VALIDATION-MACOS.md). Le MacBookAir7,2
x86_64 sous Sequoia est qualifié pour ce premier shader ; Navi48 compile
maintenant sur le Hackintosh, mais Mesa et l'intégration Metal restent ouverts. Le nouveau test `apple_vector_add` est ajouté
aux scripts Mac/Windows et ses trois traductions de référence sont désormais
vérifiées sous Windows également. Les quatre régressions `apple_graphics`
portent la sélection à 15 tests, désormais tous rejoués avec succès sous Windows.

## Compilation et dépendances encore ouvertes

- Obtenir les trois fixtures de validation manquantes par une provenance
  autorisée, ou organiser leur couverture avec des cas écrits pour le projet.
  La suite unitaire complète ne compile pas ; les quatre balayages du corpus
  absent sont explicitement filtrés dans les suites sélectionnées.
- Construire et auditer `translator/wrapper`, non qualifié à ce stade.
- SDK et firmwares épinglés sont utilisés dans les deux builds Navi48 ;
  [reproduction](AMD-DEPENDENCIES.md). La liaison des 422 imports noyau avec
  Tahoe et le chargement restent non vérifiés malgré la signature ad hoc.
- Construire Mesa à la révision RADV attendue avec les patches 0001 à 0005.
  Vérifier ABI N48N, architectures, symboles, dépendances et signatures ;
  reconstruire depuis une copie propre.
- Résoudre l'absence publique de `notes/design/NATIVE-S1C-ABI.md`, déclaré
  normatif par le header N48N, avant qualification du transport.

Révisions épinglées : [sources.lock.json](../dependencies/sources.lock.json).
Découpage du fork et contrats proposés : [audit AMD](AMD-INTEGRATION.md).

## Avant les essais noyau et l'intégration Metal

- Une session du Tahoe installé et les copies de l'EFI sont vérifiées ;
  essayer maintenant le secours et le retour à cette référence. Aucun argument
  de boot ni réglage de sécurité n'a été changé pendant le build Navi48.
- Extraire le module d'identification PCI après les constats A1–A3 de l'audit,
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
