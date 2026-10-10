# Navi48Native 0.2.0 — initialisation et calcul raccordés, EFI déployée

> **Suite observée le 10 octobre :** le module 0.2.0 a bien été chargé, mais
> le service se retire sans rapport de calcul. Aucun résultat GPU validé.
> Voir [le rapport du premier boot](2026-10-10-native-compute-boot.md).
> Les états non chargés ci-dessous décrivent la préparation du 9 octobre.

## Verdict

**Le prochain boot PROBE1401 tentera réellement l'initialisation et deux calculs
Radeon**, au lieu de s'arrêter à la préparation PCI/DMA de 0.1.2. Le vrai service
noyau appelle maintenant : découverte IP → bootloader PSP → GMC/GART →
PSP/ring/TMR/firmwares → SMU/IMU/RLC → CP/MES/GFX → fences → deux dispatchs
gfx1201 et comparaison des 64 résultats.

**Ce code n'a pas encore été chargé/exécuté sur la Radeon.** La vérification de
la session courante à **2026-10-09T17:44:13.149543+00:00** ne trouve aucun argument,
module ou nœud natif. Aucun firmware, transfert DMA, initialisation ou calcul
Radeon natif n'est observé. Les étapes matérielles 1/2 restent non terminées.
Compilation, signature et injection ne les remplacent pas.

[Procédure de boot et vérification](../NATIVE-COMPUTE-ESSAI.md) ·
[code service](../../kexts/Navi48Native/).

## Ce qui est réellement implémenté

- `Navi48Native::start()` appelle `ExperimentalCompute::run()` **hors gate**
  après allocation/revalidation du socle. Double opt-in `compute=1` + `risk=1`,
  sans lequel il ne fait toujours aucune opération firmware/commande GPU.
- Contexte/ABI `n48compute` distinct, mappings RW/UC privés, accès bornés,
  annulation atomique, identité/command/BAR recontrôlés dans le callback live.
  Aucun changement des anciens `Claims`, blockers ou contexte RO.
- Protection de toute l'allocation console, rejet d'une base/forme ambiguë,
  d'un accélérateur existant et des mappings divergents. RCC/IP discovery
  confirme gfx1201 ; MC framebuffer jamais réécrit d'après une supposition.
- NBIF/HDP/doorbells, test BAR0/MM avec mots uniques dans le scratch puis
  restauration des mots ; concordance des bases GMC/PSP contrôlée.
- Orchestration firmware et moteurs ci-dessus. PSP applique la correction
  fail-closed native déjà auditée : mismatch MC, layout, ring, fence, status et
  staging invalide ne deviennent pas un warning suivi d'une commande.
- Deux shaders publics de la révision épinglée (assembleur et LLVM/HSA),
  chacun faisant 32 additions et écrivant une rampe. Test IB, fence et
  comparaison des 32 lanes exigés pour **chacun**. Quatre mots/résultats,
  durées et états de fences publiés dans `Navi48Native,Compute` schéma 1.
- Retour au state SMU Low après les deux calculs. Une erreur de clocks est
  séparée du fait qu'un calcul a déjà effectivement passé ses comparaisons.

**Pas de `UserClient` pour soumettre des programmes libres, pas d'accélérateur
IOGraphics, RADV Darwin, Metal ou WindowServer.** Le premier calcul prévu est
interne au service, pas une pile graphique fonctionnelle.

## Mémoire GPU, pas de physique CPU supposée

`ComputeSysMem` remplace le SysMem amont pour les rings/queues/writeback. Ses
buffers passent par `IOBufferMemoryDescriptor` + `IODMACommand::kMapped`.
`gmc_bind_existing` écrit chaque PTE depuis `sysmem_iovm_page()` ; les pages
IOVM discontiguës ne sont pas aplaties en `first + i*4096`. Aliases inter-buffers,
limites/alignement/coverage sont validés. Aucun `getPhysicalSegment` CPU utilisé
pour fournir une adresse DMA. L'allocateur high VRAM amont est désactivé.

Les consommateurs polled CP/MES/fence reçoivent seulement une vue CPU originale
avec sortie DMA 64 bits (pas de bounce dû à la largeur d'adresse), pages validées
sous 48 bits et `getIOMemoryDescriptor()` identique au descripteur original.
Aucune synchronisation bounce ne remplace la fence et aucune réponse GPU n'est
simulée dans le vrai kext. Le fonctionnement du mapper pour la Radeon n'est
pas prouvé avant les fence/result readbacks matériels.

## Conservation/risque

Les validations ne constituent **pas une réservation de VRAM, une propriété
exclusive de tous les blocs ni une récupération testée**. L'utilisateur a
autorisé les essais matériels ; le code exige un opt-in risqué séparé, pas des
preuves fabriquées. Scratch expérimental BAR0 : 64 Mio + 64 Mio ; ce n'est pas
la RAM DMA du socle (64 Kio).

Avant hardware : échecs nettoyés hors gate, DMA avant fermeture PCI, maps RW et
assertions PM retirés. Dès la première opération hardware : owner/module,
contrôleur/bail PCI, mappings et tous les buffers publiés restent retenus
**jusqu'au reboot**, y compris timeout/annulation/échec de publication.
Pas de quiescement inventé, hot-unload, retry ou réarmement. Une assertion PM
évite idle/display sleep, sans empêcher demand sleep ; ne pas forcer la veille.

Un écran noir/panic demeure possible. Le retour prévu est arrêt complet si
nécessaire, puis F12 → OPENCORE. Ce n'est pas une récupération après panne GPU
qualifiée ; les sauvegardes ne sont pas des entrées BIOS indépendantes.

## Builds et tests, strictement logiciels

Deux builds isolés :

- déployé : `out/native-kext/compute-trial-0.2.0/` ;
- reproduction : `out/native-kext/compute-repro-0.2.0/`.

**26 objets identiques**, sources identiques, exécutable signé et tous les
fichiers de bundle identiques. Zéro warning, signature ad hoc stricte, dix
firmwares et deux shaders vérifiés octet à octet dans le Mach-O. Les ZIP ont des
métadonnées temporelles différentes ; seule l'empreinte du ZIP déployé ci-dessous
est donnée, sans prétendre à leur identité.

| Banc | Contrôles | Périmètre |
|---|---:|---|
| Service/lifecycle | 321 | vrai service/controller/DMA, backend hardware remplacé par double de durée de vie, jamais de shader simulé réussi |
| Adaptateur DMA | 162 | IOKit doubles, mapper/bounce/cleanup |
| Pool compute IOVM | 29 | pages discontiguës, trous non alias, alias réels, publication conservée, limites/descripteur direct |
| Accès compute | 39 | bornes/lease/fault terminal/HDP/indirect/PSP sur RAM |
| Outils/profils Python | 78 tests | opt-in, profils, identité USB, swap/rollback, régressions |

Les quatre bancs C++ sont ASan/UBSan. Les modèles vérifient arrêt depuis un autre
thread pendant la phase hors gate, conservation après publication hardware
modélisée, échec de publication de diagnostics, refus de retry. **Ces nombres
ne sont ni des opérations ni des résultats Radeon.**

### Produit

- Version : **0.2.0**, x86_64.
- UUID : **AB1F0B3A-865C-33FC-BC6B-5CA0056BBF45**.
- Exécutable SHA-256 :
  `34ce08473ac2acedd0a9094420cbd4ebf159bf48b21a0134095715cfd95809f9`.
- ZIP déployé SHA-256 :
  `a70ee90593f0c56900bc816ec58a69b15c21ed2d58859654fdc31fdcec9a25eb`.
- Rapport build SHA-256 :
  `16cedb66700efeda3b84d7ebb6c2a68a46f3ebad8f85f6889a28ea6124240454`.
- **323 noms d'imports** présents dans le BootKC, aucun manquant. Propriétaires
  `com.apple.kernel` / `com.apple.iokit.IOPCIFamily`. **Noms seulement : ni
  relocation, vtable, ABI ou chargement validés.**

Révisions inchangées : Navi48 `696959753070e9a16677be485580dc53b06a7ab5`,
MacKernelSDK `05094e5e88cec7caedbfb35e8449ed0db94bf95b`, linux-firmware
`5ff473283bf11ba0a8b6b04a075ae01676aee10d`. Aucun service/hook graphique amont
compilé. Notices de licence conservées dans le produit.

## Déploiement réellement effectué

`tools/prepare-native-kext-efi.py` a préparé puis échangé l'EFI complète, avec
`--experimental-compute --deploy-probe1401 --replace-native-0.1.2`. Il a refusé
les profils inattendus et vérifié la version/hash de l'ancien essai.

- **112 fichiers** de `/Volumes/PROBE1401/EFI` conformes au manifeste final ;
  `ocvalidate` et signature réussis après copie.
- **101 fichiers OPENCORE inchangés** ; SMBIOS, patches Ryzen, sécurité et cinq
  kexts de référence conservés. Une entrée native et cinq arguments ajoutés.
- **112 fichiers de l'EFI 0.1.2** sauvegardés sur USB dans
  `EFI.BACKUP-native-0.2.0-compute-trial` et localement dans
  `out/efi-native/native-0.2.0-compute-trial/previous-trial/EFI`.
- Ancien backup observateur **104 fichiers** préservé dans
  `EFI.BACKUP-native-0.1.2-trial`, toujours identique à sa copie locale.
- Manifeste racine/notice USB actualisés, versions précédentes archivées et
  vérifiées. Aucune EFI privée/copie de SMBIOS committée.
- Aucun reboot, installation système, hot-load, modification SIP/sécurité ou
  NVRAM système. **La session reste celle de référence.**

Preuves privées/locales : `out/efi-native/native-0.2.0-compute-trial/`.

- Rapport préparation SHA-256 :
  `78cdada2e3f5c3bfa6477de6c0c39040fb9217c5a938e52011b2ff59a1d3dab0`.
- Manifeste EFI SHA-256 :
  `eb2e9d0946cc98f0e16f879d828df89659b156ef4429261ea7132b86351158b6`.
- Vérification post-déploiement SHA-256 :
  `90bb347de2e62a870605c3849ca6cdb0ad8a31ad190308559d9608ea41f07c69`.

## Étape suivante

**Boot physique : F12 → PROBE1401 UEFI → Tahoe installé**, selon la procédure
liée. Observer version/UUID, `FailedStage`/`Result`, les deux fences et les 64
résultats comparés. Ne pas déclarer les étapes 1/2 terminées sur un simple
chargement. Pas de relance de l'observateur, pas de nouvelle bibliothèque à la
place du chemin service/mémoire/commandes. Corriger ensuite le premier refus
matériel observé ; conserver les ressources/logs et revenir à la référence.
