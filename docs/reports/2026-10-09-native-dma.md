# Étapes matérielles 1 et 2 — mémoire DMA avancée, GPU toujours non initialisé

**La demande était de réaliser l'initialisation GPU puis une première commande
avec résultat relu. Ces deux résultats ne sont pas obtenus par cette passe.**
Le code actuellement livré ne peut pas encore initialiser la Radeon. Une
compilation, un nom de symbole dans le noyau ou un test sur doubles IOKit ne
remplacent pas ce résultat matériel.

## Code effectivement ajouté

Dans `kexts/Navi48Native/DmaBuffer.cpp` et `.hpp` :

- allocation de RAM noyau via `IOBufferMemoryDescriptor`, sans demande de
  contiguïté physique et sans contournement du mapper ;
- mapper propre au périphérique lorsque disponible, sinon mapper par défaut
  prévu par IODMACommand, **sans prétendre que l'adressage est identité** ;
- `IODMACommand::kMapped`, préparation et liste de pages IOVM 64 bits, bornée
  à 48 bits et 1 Mio par buffer ; aucune adresse DMA issue de `getPhysicalSegment` ;
- contrôle de toute la liste : taille, couverture, alignement et absence d'alias ;
  une liste discontiguë ne devient pas arbitrairement un bloc contigu ;
- écriture du descripteur original + synchronisation Out, synchronisation In +
  lecture du descripteur original, y compris avec bounce buffer ;
- opérations bloquantes interdites dans le gate propriétaire, réentrée refusée ;
- ressources référencées potentiellement par le GPU conservées en quarantaine
  terminale, avec mapper, commande, mémoire, provider, workloop et owner retenus.
  Aucun producteur de quiescement ne permet encore de recycler ces ressources.

**Ce code est réellement lié, mais pas encore invoqué par le service.** Il ne
remplace donc pas encore le `amdgpu_sysmem.cpp` amont dans un pilote exécuté.
Il ne réserve pas de VRAM, ne configure pas GMC/GART et ne prouve pas que le GPU
peut atteindre les pages générées. Le contrôleur reste privé/désactivé et ses
sept familles de blocages ne sont pas effacées.

## Livrable et vérifications limitées

- `out/native-kext/dma-integration/Navi48Native.kext`, version **0.1.1**, ZIP voisin.
- Compilation x86_64 sans avertissement ; signature ad hoc vérifiée.
- Dix firmwares toujours conformes dans le binaire ; objets firmware et contrôleur
  réutilisés à l'identique depuis `out/native-platform/final-a`.
- 162 contrôles ciblés ASan/UBSan de l'adaptateur sur doubles IOKit, zéro échec.
  Ces vérifications simulent notamment des IOVM différentes des adresses CPU,
  des pages discontiguës, des bounce buffers, des erreurs et la quarantaine.
  **Aucun DMA matériel n'a été exécuté par ces tests.**
- 109 contrôles du service/contrôleur inchangé, zéro échec. Pas de campagne
  observateur, de mutations ou de reconstruction globale amont.
- Les 312 noms d'imports sont présents dans le BootKC Tahoe 25G241 : absence de
  nom manquant, **pas une liaison/compatibilité ABI ou un chargement validé**.

SHA-256 de l'exécutable :
`5ee0258dd60e035c77125b82964589e01ef5a34e82339371580e820f2048922c`.

## Travail encore requis pour obtenir les résultats demandés

1. Établir les réservations/origine/taille/base MC de la VRAM et la protection
   de la console, puis propriété GPU, HDP et cycle de vie/quiescement/récupération.
2. Raccorder une autorisation issue de ces producteurs au contexte matériel,
   puis orchestrer PSP/SMU et les blocs nécessaires. L'archive firmware liée
   n'est pas une orchestration d'initialisation exécutée.
3. Relier les pages DMA à GMC/GART, puis RLC/CP/MES et une soumission réelle.
   Comparer le résultat écrit par un shader au résultat attendu, avec une fence,
   un délai borné et un traitement sûr de la panne. Ce chemin n'existe pas encore
   dans le service natif.
4. Préparer le boot d'essai et le retour arrière avant la première exécution.
   Un simple chargement de 0.1.1 ne peut pas terminer les étapes 1 et 2.

Aucun déploiement, chargement, firmware envoyé, redémarrage ou changement de
sécurité. OPENCORE **101 fichiers** et PROBE1401 **104 fichiers** identiques
par SHA-256 à l'état précédent. Le bundle historique `stage1/` reste intact.

[Preuves structurées](2026-10-09-native-dma.json) ·
[Sources et compilation](../../kexts/Navi48Native/README.md).
