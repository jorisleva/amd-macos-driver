# Chemin natif d'initialisation et de calcul — travail en cours

## Checkpoint de développement

Le service `Navi48Native` **0.2.0** appelle désormais, avec double opt-in explicite,
un chemin matériel distinct de son ancien contrôleur RO : découverte IP,
bootloader PSP, GMC/GART, ring/TMR/firmwares PSP, SMU, IMU, RLC, CP, MES, GFX,
fences et deux dispatchs gfx1201 (assembleur et LLVM/HSA). Les shaders effectuent
32 additions chacun et écrivent une rampe ; les 64 lanes doivent être relues et
comparées, en plus des fences. Aucun `UserClient`, Metal ou WindowServer ajouté.

Ce chemin est **un essai matériel autorisé**, pas une qualification préalable :
les anciens `Claims`, blockers et contexte RO ne sont pas modifiés. L'absence
d'accélérateur, le bail PCI, la protection de la console et le test d'origine
BAR0/MM ne prouvent pas une réservation générale de VRAM ni une restauration.
Deux arguments supplémentaires sont obligatoires : `navi48-native-compute=1`
et `navi48-native-risk=1`. Aucun redémarrage/chargement à chaud n'a été fait.

Les moteurs amont proviennent de la révision Navi48 épinglée
`696959753070e9a16677be485580dc53b06a7ab5`, dans un namespace distinct.
Aucun service/hook graphique amont n'est compilé. Les deux shaders sont des
binaires publics de cette révision, pas des blobs privés Apple.

## Mémoire et durée de vie

- `SysMem` utilise `DmaBuffer`/`IODMACommand`, pas `getPhysicalSegment`.
- `gmc_bind_existing` émet chaque PTE depuis la page IOVM réelle ; pas d'hypothèse
  `bus = physique CPU` ni de contiguïté.
- Les rings/fences utilisent une vue CPU directe, sortie DMA 64 bits (pas de
  bounce de largeur d'adresse), pages validées sous 48 bits et descripteur DMA
  identique au descripteur original. Pas de copie vers un faux tampon CPU.
- Aucun allocateur « high VRAM » hors du scratch explicite.
- Dès la première opération hardware, le service/module, le bail PCI, les
  mappings et tous les buffers publiés restent retenus **jusqu'au reboot**.
  Annulation/timeout n'autorisent ni désallocation, ni unload, ni retry.

## Premier build, non final

`out/native-kext/compute-e/` compile et lie le vrai service et les moteurs :
**zéro avertissement**, signature ad hoc stricte, dix firmwares et deux shaders
vérifiés dans le Mach-O. Le smoke service/DMA historique reste sur doubles IOKit.
La validation ciblée des nouveaux chemins, l'audit runtime et le remplacement
du profil EFI de calcul sont encore en cours à ce checkpoint.

**Chargement noyau : non observé. Initialisation GPU : non observée.
Commandes/calculs Radeon : non observés. EFI actuelle : encore essai 0.1.2.**
Ne pas utiliser ce checkpoint comme une autorisation de démarrer un profil de
calcul déjà prêt : la procédure finale doit nommer le bundle et ses hashes.
