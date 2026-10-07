# Préparation dépendances AMD et OpenCore sur le PC Ryzen/Radeon

Rapport du 7 octobre 2026. Préparation demandée en parallèle du corpus
graphique Metal/AIR en cours sur le Mac. **Aucun OS démarré, aucune partition
EFI/USB modifiée, aucun pilote GPU du projet installé.**

## Résultats obtenus

| Vérification | Résultat |
| --- | --- |
| Inventaire stockage / réseau / USB | Crucial P3 Plus CT1000P3PSSD8 1 To NVMe `1344:5416`, Realtek `10EC:8168`, AMD USB `1022:43EE` et `1022:149C` |
| Topologie USB Windows | 14 et 8 ports racine ; chaque contrôleur est sous 15 ; cartographie physique non exécutée |
| Namespace ACPI | DSDT : pont LPC `\_SB.PCI0.SBRG` ; PnP CPU : `\_SB.PLTF.C000` à `C00B` |
| Identification VBIOS via VFCT | ASRock, `023.008.000.068.000001`, part number `113-APM107819-101`, PCI `1002:7550` |
| MacKernelSDK | Archive à `05094e5e88cec7caedbfb35e8449ed0db94bf95b` récupérée ; SHA-256 et notice APSL 2.0 vérifiés |
| Firmwares AMD | 10/10 SHA-256 conformes à une seule révision linux-firmware `5ff473283bf11ba0a8b6b04a075ae01676aee10d` |
| SSDT EC/USBX | iASL 20250807 : 0 erreur, 0 avertissement ; checksum AML valide |
| SSDT CPUR | iASL : 0 erreur, 12 avertissements 3168 `Processor()` ancien ; message « No parent method » du modèle de retour CPUR ; checksum AML valide |
| `ocvalidate` 1.0.8, référence | « No issues found » |
| `ocvalidate` 1.0.8, secours `-x` | « No issues found » |
| Vérifications du contenu EFI | Patches pour 6 cœurs, variante PAT applicable à Darwin 25 unique, kexts avec x86_64, PE AMD64, fichiers présents, aucune injection de pilote GPU expérimental |
| Tests de rejet | 9/9 profils/entrées incorrects refusés |

Le VBIOS est une image ATOMBIOS de **58 880 octets fournie par ACPI VFCT**,
SHA-256 `cb1fe14d10f3eb934731aa28993dd21fd18c3d8daa50d9c31d8612351fc41b01`.
Il ne constitue pas une sauvegarde complète de ROM PCI destinée au flash.
Chaîne de modèle firmware : `ASRock Navi48 XTX G292 16GB 304W` ; le modèle
commercial de la carte doit être confirmé sur l'étiquette.

Le DSDT BIOS FD mesure 27 084 octets, SHA-256
`4a382ee7c5311f768b99dd2968d8808010246658d3a21824dd90ed788c3fdb8c`.
La capture Windows renvoie seulement une table par signature ; les multiples
SSDT ne sont pas toutes distinguables. Le relevé n'est donc pas une preuve
d'absence de collision dans l'ensemble du namespace ACPI lors du boot.
Les dumps firmware complets restent sous `reports/local/`, exclus de Git.

## Artefacts locaux

Kit généré : `out/opencore/ryzen5600x-b550-rx9070xt-2026-10-07/`.
Archive : `out/opencore/ryzen5600x-b550-rx9070xt-2026-10-07.zip`,
3 778 764 octets, SHA-256
`33e35136393abaf1146e747112e78f6d6d3fe94011b98e9bb6ad402c2cc5d2d0`.

Le kit contient `EFI/` et `recovery/EFI/`, les deux configurations validées,
les logs, les notices, les empreintes et le profil matériel. Il contient des
identifiants SMBIOS privés et reste **local, hors Git**. Pas d'installateur
macOS, pas de Navi48Bringup/RDNA4FB ni des kexts NVIDIA du fork.
Les fichiers reproductibles du dépôt sont dans
[`boot/ryzen5600x-b550/`](../../boot/ryzen5600x-b550/).

Les entrées SDK/firmwares préparées sont dans `out/dependencies/amd-pinned/`,
avec `verification.json`. Leurs détails publics sont dans
[`sources.lock.json`](../../dependencies/sources.lock.json) et le
[guide AMD](../AMD-DEPENDENCIES.md). La révision linux-firmware la plus
récente examinée ne correspondait qu'à 3/10 empreintes ; elle n'a pas été
retenue pour les entrées de compilation.

## Tests de rejet

Le script `tools/test-boot-preparation.py` vérifie que ces modifications
font échouer la validation :

1. Nombre de threads 12 utilisé comme nombre de cœurs physiques.
2. `-wegnoegpu` ajouté alors que la Radeon est l'unique GPU d'affichage.
3. `XhciPortLimit=true` utilisé pour Tahoe.
4. Injection de `Navi48Bringup.kext` dans le profil de référence.
5. Deux variantes PAT activées simultanément pour Darwin 25.
6. SIP désactivé dans cette référence sans kext expérimental.
7. Firmware/cache dont le SHA-256 diffère de celui verrouillé.
8. Archive ZIP tentant de sortir de son répertoire d'extraction.
9. Destination de préparation en dehors du `out/` du projet.

Ces contrôles protègent les choix de préparation. Ils ne remplacent pas les
tests clavier, stockage, écran et récupération sur le PC démarré sous Tahoe.

## Reproduction et suite

```powershell
./tools/collect-windows-inventory.ps1 -Role target -OutputPath reports/local/windows-boot-replay.json
python tools/collect-acpi-vbios.py --output reports/local/acpi-replay
python tools/prepare-amd-dependencies.py --offline --output out/dependencies/amd-replay
python tools/build-opencore-kit.py --offline --output out/opencore/efi-replay
python tools/test-boot-preparation.py --kit out/opencore/efi-replay
```

Les modes hors ligne supposent les caches déjà remplis par les outils ;
omettre `--offline` pour les premières récupérations. Les répertoires de
sortie doivent être nouveaux. Les outils ACPI/EFI sont prévus pour Windows ;
le préparateur SDK/firmwares fonctionne aussi sur Mac avec `python3`.

La [procédure OpenCore](../OPENCORE-TAHOE.md) donne l'ordre de l'essai : choisir
disque/clé, fixer Tahoe version/build, préparer l'installateur Apple, vérifier
l'UEFI, copier l'EFI sur le support identifié, démarrer via F12 et qualifier
la référence puis le secours. L'affichage de base de la RX 9070 XT sous Tahoe
**reste à confirmer**. Aucune accélération macOS n'est annoncée.

L'absence de `notes/design/NATIVE-S1C-ABI.md` à la révision AMD publique
épinglée reste un blocage de qualification du transport, indépendamment
du pinning désormais réussi. La compilation du kext et de Mesa Darwin doit
encore être réalisée sur le Mac, puis reconstruite depuis une copie propre.
