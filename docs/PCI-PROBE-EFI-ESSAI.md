# EFI d'essai du module PCI — mode d'emploi

**Historique de l'observateur :** PROBE1401 a depuis été remplacé par l'essai
`Navi48Native` 0.1.2, avec sauvegarde de cette ancienne EFI. Pour le prochain
boot utiliser la [procédure native actuelle](NATIVE-KEXT-ESSAI.md), pas les
profils/arguments de l'observateur décrits ci-dessous. OPENCORE reste intact.

**Déploiement effectué le 9 octobre :** la clé USB **PROBE1401** de 15,6 Go
est formatée en MBR/FAT32, sans sauvegarde à la demande de l'utilisateur.
Le profil **01-disabled** a d'abord été copié à sa racine et vérifié après remontage.
Le disque OPENCORE habituel est intact. **Premier boot OFF observé à 09:53 UTC :
module réellement chargé, argument 0 reçu, aucun nœud attaché.**
**Premier boot ON observé à 11:05 UTC :** module attaché à la Radeon et
dictionnaire conforme au relevé PCI du même boot. **Retour OPENCORE vérifié à
11:31 UTC :** plus d'argument d'essai, de module chargé ou de nœud observateur.
PROBE1401 reste ON, l'EFI OFF et son journal sont conservés, les deux EFI
restent intactes. [Retour et suite native](reports/2026-10-09-native-isolation.md).
[Déploiement initial](reports/2026-10-09-probe1401-deployment.md) ·
[Résultat OFF](reports/2026-10-09-pci-probe-off-boot.md) ·
[Passage à ON](reports/2026-10-09-pci-probe-on-deployment.md) ·
[Résultat ON](reports/2026-10-09-pci-probe-on-boot.md).

## Ce que cet essai cherche à vérifier

Faire fonctionner notre petit **observateur** dans le vrai noyau de Tahoe :
chargement, activation explicite et identification de la Radeon. Il ne commande
pas le GPU. **Aucun gain de fluidité ou périphérique Metal n'est attendu.**
Un défaut de kext peut néanmoins bloquer le démarrage ; la signature et les
contrôles logiciels ne garantissent pas un démarrage réussi.

## Les trois dossiers

Le paquet préparé contient :

| Dossier | Ce qu'il contient |
| --- | --- |
| `00-reference/EFI` | Copie exacte de l'EFI actuelle au moment de la préparation, sans notre module |
| `01-disabled/EFI` | Même EFI + module proposé à macOS, mais refus de s'attacher avec `navi48-pci-probe=0` |
| `02-enabled/EFI` | Même EFI + même module, avec `navi48-pci-probe=1` pour autoriser l'observation |

Le module figure dans `Kernel/Add` avec **Enabled=true dans les deux essais**.
Le premier teste son coupe-circuit logiciel, pas seulement une case OpenCore
qui empêcherait toute injection. L'absence du nœud, seule, ne prouve toutefois
pas que le module a été chargé : une injection ratée donnerait le même résultat.

Seuls changent **une entrée ajoutée à Kernel/Add et un argument ajouté à
boot-args**. Les cinq kexts existants, patches CPU, réglages mémoire, SMBIOS,
réglages de sécurité et fichiers OpenCore sont conservés. Ni Navi48Bringup
complet ni un pilote Apple expérimental ne sont ajoutés. Les coupe-circuits
`navi48bringup=0 rdna4-off=1` restent présents.

Cible limitée à **x86_64, Darwin 25.6.0**, avec `MinKernel=MaxKernel=25.6.0`.
Le préparateur exige le build **25G241**. OpenCore filtre la version Darwin,
pas le numéro de build macOS : **revoir le paquet après toute mise à jour**,
même si `uname -r` n'a pas changé. Le module contrôle lui-même les IDs de la
carte, du sous-système, la révision et la classe.

## Où se trouve le paquet ?

Localement, sous `out/efi-pci/pci-probe-<date>/package/` ; la copie éventuelle
sur le disque est sous :

```text
OPENCORE/PROFILS-TAHOE/pci-probe-<date>/
```

Le chemin exact figure dans `preparation-report.json` et dans le rapport de
préparation. Les empreintes sont dans `SHA256SUMS.json`.

**Un dossier dans PROFILS-TAHOE n'est pas une nouvelle entrée de démarrage.**
Choisir le disque OPENCORE dans F12 continuera à utiliser son dossier `EFI`
habituel. Aucun menu supplémentaire n'est installé par le préparateur.

Le paquet contient l'identité SMBIOS privée et une ancienne copie de config
si elle était présente dans la source. **Ne pas publier le paquet ou ses
config.plist.** La documentation et les rapports publics n'incluent pas ces
identifiants. Ne pas utiliser les anciennes EFI historiques à sa place.

## Avant le premier redémarrage

**Étapes 1–5 réalisées une fois sur PROBE1401, avec le profil OFF.**
Pour un futur autre support, la préparation se fait séparément :

1. Préférer une **seconde clé USB FAT32**, déjà préparée et clairement identifiée,
   afin de conserver le disque OPENCORE habituel utilisable sans modification.
   La préparation de ce paquet ne formate et ne repartitionne aucun disque.
2. Vérifier que cette clé ne contient pas une EFI ou des fichiers à conserver.
   S'il existe déjà un dossier EFI, le sauvegarder et le déplacer en entier :
   **ne pas fusionner deux EFI**. Ne pas toucher à l'EFI du disque Windows.
3. Copier le dossier `EFI` de **01-disabled** à la racine du support d'essai,
   avec `BOOT/BOOTx64.efi` et `OC/config.plist` à l'intérieur. Les répertoires
   `00-reference`, `01-disabled`, `02-enabled` ne doivent pas s'intercaler
   entre la racine du support et son dossier `EFI`.
4. Vérifier les empreintes de la copie, puis `ocvalidate` avec la version
   **1.0.8** et la signature du kext. Cette copie sur le support amorçable
   n'est **pas** effectuée par `--stage-volume`.
5. Redémarrer seulement au moment choisi. Dans le menu Gigabyte **F12**,
   sélectionner la **clé d'essai en UEFI**, puis le **Tahoe déjà installé**
   dans OpenCore, pas la récupération/installation.

S'il n'y a pas de deuxième clé, il faudra autoriser et préparer séparément
un remplacement temporaire de l'EFI active avec un retour arrière accessible
hors de macOS. **Ce remplacement n'est ni exécuté ni automatisé ici.** La copie
de référence vérifiée ne signifie pas que la procédure de secours a été essayée.

Ne pas changer SIP, SecureBootModel, les réglages BIOS, l'ordre de boot par
défaut ou effectuer Reset NVRAM pour cet essai. `NVRAM/Delete/boot-args` est
déjà présent dans l'EFI source ; il permet l'application de l'argument choisi.
`WriteFlash=false` est conservé, sans promettre qu'un démarrage complet de
macOS ne modifiera jamais aucune autre variable NVRAM.

## Premier essai : OFF

**Résultat du 9 octobre :** injection réussie, `showloaded` confirme le module
0.1.0 avec le même UUID que le binaire, argument 0 et absence du nœud vérifiés.
Le chargement est établi pour ce démarrage OFF. Un boot ON a depuis été
exécuté, avec la même version du module et le dictionnaire attendu.
L'avertissement de notification de `kernelmanager_helper` est conservé dans
le rapport, sans le confondre avec un échec de chargement.

Après arrivée au bureau, relever :

```sh
sw_vers
uname -r
sysctl -n kern.bootargs
kmutil showloaded | grep -F com.amd-macos-driver.Navi48PciProbe
ioreg -r -c Navi48PciProbe -l -w 0
```

Attendu : `navi48-pci-probe=0`, aucun nœud d'observation attaché, écran et
périphériques habituels fonctionnels. Le module peut avoir été déchargé
automatiquement après refus de `probe()` : **l'absence dans showloaded ne
suffit pas à conclure que le coupe-circuit a été exécuté**.

Conserver aussi le journal `opencore-*.txt` du **support réellement utilisé**.
Une injection annoncée réussie par OpenCore prouve l'ajout au cache de ce
boot, pas l'exécution de `probe()`. Si la présence/prise en compte du module
n'est pas établie, classer le résultat **incomplet**, pas « test OFF réussi ».
Ce premier essai peut néanmoins établir que cette configuration démarre.

## Deuxième essai : ON

Après revue du premier résultat et accord utilisateur, l'EFI complète a été
remplacée **sur PROBE1401** par celle de `02-enabled`, puis vérifiée après
remontage. **Ce boot ON est maintenant observé :** argument 1, un nœud attaché
au bon provider et tous les champs du dictionnaire conformes. Aucun chargement/
déchargement à chaud n'a été effectué.

Rejouer les mêmes commandes. Attendu :

- argument `navi48-pci-probe=1` effectivement reçu par le noyau ;
- module présent et nœud `Navi48PciProbe` visible ;
- dictionnaire `Navi48PCI,RegistrySnapshot` avec `SchemaVersion=1` ;
- IDs `1002:7550 / 1849:5417`, révision `c0`, classe `030000` ;
- ressources conformes à celles publiées par IOPCIFamily pour ce même boot ;
- bureau, clavier, stockage et réseau sans régression observée.

Relevé précédent : BAR0 256 Mio, BAR2 2 Mio, BAR4 I/O 256 octets,
BAR5 512 Kio, ROM 128 Kio. Les bases d'adresses peuvent être réassignées par
le firmware entre démarrages : comparer au relevé du même démarrage, sans
conclure à une erreur sur la seule différence d'adresse. Il s'agit des
ressources déclarées, **pas d'un test de VRAM ou d'accès aux registres GPU**.

La ligne noyau attendue, si les journaux la conservent, est :
`Navi48PciProbe: registry-only snapshot, 5 resources; no GPU initialization`.
Le nombre dépend des ressources publiées (ROM optionnelle). Un log absent
n'annule pas à lui seul une capture IORegistry valide ; conserver les preuves.
Lors du premier ON, cette ligne n'est pas retrouvée dans l'extrait unifié,
mais le nœud et le dictionnaire sont présents. Les cinq `RegistryFlags` sont
négatifs dans `ioreg -a`, et affichés comme de grands entiers dans la sortie
texte : comparer leurs **32 bits**, pas les traiter comme des tailles mémoire.
`tools/pci_probe_snapshot.py` effectue cette comparaison bornée ; Base/Length
restent en 64 bits. Voir le rapport ON pour les valeurs et les avertissements.

## Retour et diagnostic

Revenir à la configuration habituelle en sélectionnant le disque OPENCORE
resté intact via F12. Confirmer la disparition de l'argument d'essai et du
nœud. **Pas de kextunload/kextload, ni d'installation dans /Library/Extensions.**

Si écran noir, blocage ou panic : ne pas passer à l'essai suivant. Photographier
les dernières lignes, noter quel support et quel profil ont démarré, conserver
le log OpenCore et, si disponible, le rapport de panic. Utiliser le support
habituel pour revenir au système. Un retour après le boot ON réussi est
observé ; la récupération après panne et la restauration d'une EFI endommagée
ne sont pas pour autant qualifiées.

Le résultat doit séparer : configuration contrôlée, injection OpenCore,
chargement noyau, activation du module et stabilité observée. **Aucune de ces
étapes ne valide l'accélération, les firmwares ou Navi48Bringup complet.**

## Reproduire la préparation, sans activation

Depuis la racine du dépôt, avec une nouvelle sortie :

```sh
python3 -B tools/prepare-pci-probe-efi.py \
  --efi /Volumes/OPENCORE/EFI \
  --build out/pci-probe/release-a \
  --output out/efi-pci/pci-probe-nouvel-essai \
  --offline --stage-volume /Volumes/OPENCORE
```

`--offline` exige l'archive officielle OpenCore 1.0.8 DEBUG déjà en cache.
Sans cette option, le préparateur peut télécharger cette seule archive,
vérifiée par le SHA-256 épinglé. Sans `--stage-volume`, seules les copies
locales sont créées. Les sorties existantes, symlinks, kexts inattendus et
modifications hors des deux champs autorisés sont refusés.

Le contrôle des imports vérifie la présence des noms dans le BootKC de Tahoe.
Il **ne remplace pas un vrai lien**, la vérification de la disposition des
vtables, l'admission du kext ou le démarrage. Aucune collection noyau système
n'est reconstruite par cet outil.
