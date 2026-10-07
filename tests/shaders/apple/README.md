# Corpus Apple AIR — vector_add

Shader écrit pour ce projet, compilé le 7 octobre 2026 avec Apple Metal
32023.864, SDK macOS 26.2, cible `air64-apple-macosx26.0`, `-std=metal4.0 -O2`.
Source canonique : [`../vector_add.metal`](../vector_add.metal).

- `vector_add.air` : bitcode Apple original, **pas** la fixture synthétique.
- `vector_add.metallib` : bibliothèque liée par Apple, cible Tahoe ; non exécutée.
- `vector_add.air.ll` : désassemblage LLVM 20.1.8 ; seul le commentaire ModuleID
  ajouté par llvm-dis est retiré. L'AIR n'est pas réécrit.
- `vector_add.spv`, `vector_add.spvasm`, `vector_add.reflection.json` : traduction
  du bitcode et contrat du banc Windows (schéma 56, main, local 64×1×1,
  Workgroups, buffers 0..3, Int64 requis).
- `provenance.json`, `SHA256SUMS` : machine, outils, options et empreintes.

La compilation lit la copie exacte de la source par stdin pour éviter un chemin
utilisateur dans l'AIR. `bash tools/compile-metal-reference.sh DOSSIER_VIDE`
régénère le corpus. Vérifier depuis ce dossier : `shasum -a 256 -c SHA256SUMS`.

Le test Rust `apple_vector_add` traduit le désassemblage et compare exactement
SPIR-V et réflexion à cette référence. Il ne vérifie aucun résultat GPU.
Le SPIR-V a ensuite été exécuté sur la RX 9070 XT sous Windows : **36 cas
réussis**, résultats identiques à la référence CPU et au contrôle GLSL, sans
erreur Vulkan/synchronisation. La metallib n'est pas exécutée via Metal.
Preuves : [rapport Apple AIR/Radeon](../../../docs/reports/2026-10-07-apple-air-radeon.md).

[Rapport](../../../docs/reports/2026-10-07-apple-air.md) ·
[Procédure Mac et transfert Windows](../../../docs/VALIDATION-MACOS.md)
