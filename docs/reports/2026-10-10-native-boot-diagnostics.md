# Buffer de boot perdu ; correctif diagnostic 0.2.1 hors ligne

## Ce que la capture utilisateur établit

Dans le boot du **10 octobre 2026 à 11:20:06 UTC**, l'utilisateur a capturé
`sudo dmesg`. Le fichier fait **131 071 octets / 135 lignes** : buffer de
128 Kio, début au milieu d'un message, horodatages de **742,730521 à
777,569067 secondes après le boot**. Il ne contient **aucune ligne native**.
Les messages du démarrage ont été écrasés. Les erreurs XProtect et
IOVersatileHDCPClient restantes n'établissent pas la cause du retrait natif.

**Code exact du refus : toujours inconnu.** Le module **0.2.0** est chargé,
mais pas de service/rapport/calcul Radeon disponible. DMA complète, firmware,
initialisation, fences et shaders restent non validés. Aucun `FailedStage` ou
`HardwareTouched` n'est inventé ; étapes matérielles 1/2 non terminées.

Copie privée : `out/native-compute/boot-2026-10-10-112006/dmesg-followup/`.
SHA-256 du buffer :
`3ab94bc30321cc89db4e7ad2a7eeb94d4c637cb90bc1c6f1623e68f8388eb199`.
Aucun XML/EFI ou détail de session privé ajouté à Git.

## Correctif ciblé, pas contournement du garde

Les sources passent à **0.2.1**. Le service publie un dictionnaire générique
`Navi48Native,BootDiagnostics` dans **IOResources**, après finalisation et
nettoyage pré-hardware, **hors command gate**. Il contient uniquement des
valeurs OSNumber/OSDictionary, aucune référence au service, au provider PCI,
aux mappings ou à la DMA. Il est destiné à survivre au refus de `start()` et à
la destruction du service, jusqu'au redémarrage ; pas une sauvegarde sur disque.

Publication best effort : l'épuisement mémoire peut l'empêcher. Une tentative
de réarmement du même service ne remplace pas le résultat d'origine. Plusieurs
instances peuvent remplacer la propriété : elle décrit la dernière publication,
pas un historique de toutes les tentatives. Aucune autorisation GPU, Claim,
plage scratch, opération DMA, firmware ou commande PM4 n'est changée. Les
**23 autres objets sur 26** sont identiques à ceux du bundle 0.2.0 déployé ;
seuls service, kmod et orchestration annotée changent.

Lecture pour **un futur boot 0.2.1, après revue/déploiement distincts** :

```sh
kmutil showloaded | grep -i Navi48Native
ioreg -r -c IOResources -l -w 0 | grep 'Navi48Native,BootDiagnostics'
ioreg -r -c Navi48Native -l -w 0
```

**Ce correctif ne récupère pas rétroactivement les refus du boot 0.2.0.** Sa
persistance réelle dans le noyau n'est pas encore observée : seul le contrat SDK
et le modèle IOKit ont été vérifiés. Ne pas redémarrer simplement la clé actuelle
pour le tester : **PROBE1401 est toujours en 0.2.0**.

## Décodage du diagnostic, schéma 1

`DriverVersion=0x000201`. `StartReturn` est un IOReturn encodé en uint32,
**pas un résultat GPU** : il peut être zéro quand le service est conservé pour
un échec hardware. Examiner alors `ComputeResult` et `FailedStage`.

**Lire d'abord `PlatformObserved`, `DMAObserved`, `ComputeObserved`.** Les
valeurs par défaut d'un bloc non observé ne signifient ni réussite ni zéro
lanes exécutées. `MappingsHeldBeforeCleanup` est un relevé avant nettoyage,
pas la preuve qu'un bail est encore détenu quand on lit le rapport.

| Checkpoint | Dernière frontière logicielle |
|---|---|
| 1–4 | init parent, workloop, gate, ajout event source |
| 5–6 | arguments de probe / provider PCI |
| 7 | préconditions start / double opt-in |
| 8 | réarmement refusé ; conserve le diagnostic précédent |
| 9 | start parent |
| 10 | acquisition contrôleur RO |
| 11 | allocation DMA / revalidation / publication Resources |
| 12 | allocation orchestrateur compute |
| 13 | finalisation compute, même sur échec |
| 14 | socle RO/DMA préparé sans compute |

`PlatformDecision` conserve le code renvoyé au service par acquire/revalidate,
comme dans l'ancien IOLog ; `PlatformResult` décrit le snapshot du contrôleur.
À checkpoint 11, `PlatformDecision=23` peut signifier revalidation non tentée
après échec DMA/annulation. Les codes `PlatformResult`/`PlatformDecision` sont
ceux de `IOKitController.hpp` : 0 mappings non qualifiés, 7 provider occupé,
10 carte incorrecte, 12 BAR, 13–14 descripteur, 16–17 scratch/console,
18 mapping échoué, 19 mapping invalide, 21–22 changement config/console,
23 non mappé. `FailedBar`, `DMAResult`, `DMAPhase`, `CancelledOrInactive` et
les faits de console permettent de distinguer les frontières sans deviner.

Pour `ComputeObserved=1` et `FailedStage=1`, `PreflightCheck` identifie :

| PreflightCheck | Contrôle avant la première opération hardware |
|---|---|
| 1 | owner/provider/workloop valides |
| 2 | taille/alignement scratch |
| 3 | base console physique et géométrie |
| 4 | allocation entière de console dans BAR0 |
| 5 | absence d'overlap scratch/console |
| 6–8 | dictionnaire/itérateur IOAccelerator, accélérateur existant |
| 9–10 | allocation Resources / verrou indirect |
| 11/12 | descripteur / mapping RW BAR0 |
| 13/14 | descripteur / mapping RW BAR2 |
| 15/16 | descripteur / mapping RW BAR5 |
| 17 | mémoire indirecte / bail live |
| 18–19 | root PM / assertion anti-idle-sleep |
| 20 | revalidation live / session terminale unique |
| 0 | préflight passé ; pas qualification mémoire/propriété |

Les stages matériels 2–18 et critères de réussite shader restent ceux de
[NATIVE-COMPUTE-ESSAI.md](../NATIVE-COMPUTE-ESSAI.md). Le rapport persistant
est un diagnostic synthétique ; les preuves positives exigent toujours les
**deux fences, deux tests IB et 64 comparaisons** du rapport Compute complet.

## Validation hors ligne

- Bundle : `out/native-kext/boot-diagnostics-0.2.1-final/Navi48Native.kext`.
- UUID : **62893962-8984-3EE0-B636-40E33C9F073D**.
- Exécutable SHA-256 :
  `441140b8f86095f0bd06e7c77d61f90ae20518e0fa40171e35dd270bd49e01da`.
- ZIP SHA-256 :
  `398e48cfe9dd0308bf06648cdc6bf901384d5de52d3c80445b9732605572f030`.
- Signature ad hoc stricte, zéro warning, dix firmwares/deux shaders vérifiés.
- **419** contrôles service/lifecycle, **162** DMA, **29** pool IOVM,
  **39** accès RAM, sous ASan/UBSan ; zéro échec. Le modèle vérifie refus
  acquisition/DMA/préflight, persistance après destruction, publication hors
  gate, absence de références PCI conservées et refus de réarmement.
- **80 tests Python** : zéro échec, dont identité 0.2.1 et refus d'un produit
  dépourvu du symbole de diagnostic.
- **324 imports** ont des noms présents dans BootKC, aucun manquant : audit
  de noms seulement, pas validation relocation/vtable/ABI/chargement de 0.2.1.

**Non déployé, non chargé.** Aucun hot-load, commande GPU, changement sécurité,
NVRAM ou reboot. **112 fichiers PROBE1401, 101 OPENCORE et 112 fichiers de
backup natif inchangés**, revérifiés après compilation. Le préparateur actuel
refuse de remplacer sans revue la clé déjà passée en 0.2.0 ; l'option historique
`--replace-native-0.1.2` ne s'applique plus. Prochaine opération : préparer un
remplacement 0.2.0 → 0.2.1 strictement vérifié/sauvegardé, puis un nouveau boot
diagnostic explicitement planifié, pas une recharge du pilote courant.
