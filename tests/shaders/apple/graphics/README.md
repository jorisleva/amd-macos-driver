# Corpus graphique Apple AIR

Quatre shaders écrits pour le projet, compilés avec Apple Metal 32023.864,
SDK macOS 26.2, `air64-apple-macosx26.0`, Metal 4.0 et `-O2` :

| Fichiers | Rôle | Descriptors Vulkan |
| --- | --- | --- |
| `fullscreen.vert.*` | Triangle plein écran, sans vertex buffer | Aucun |
| `texture.frag.*` | Texture RGBA, nearest, centres des texels, mip 0 | Set 0 : sampled image 32, sampler 160 |
| `triangle.vert.*` | Deux géométries du contrôle GLSL, sélection par shape | Set 0 : storage buffer 0 |
| `solid.frag.*` | Couleur RGBA, blending fixe dans le pipeline | Même storage buffer 0 |

Sources canoniques : [`../../graphics/`](../../graphics/). Chaque famille
contient source, AIR original, désassemblage LLVM (sans son commentaire
ModuleID), metallib, SPIR-V validé, désassemblage SPIR-V et réflexion 56.
Entrées SPIR-V : `main`. `DrawParams` : 32 octets, offsets 0/16/20/24/28.

`NVMTL_NO_BINDLESS_ALL=1` sélectionne explicitement les descriptors directs.
Le profil bindless par défaut du fork est refusé : sa réflexion texture ne
correspond pas aux descriptors émis. `texture.frag.spv` nécessite `shaderInt8`
à cause de l'octet de résidence de l'intrinsèque AIR ; le banc le demande
explicitement et refuse un GPU qui ne le fournit pas.

Vérification : `python tools/compile-metal-graphics.py --check tests/shaders/apple/graphics`.
Reconstruction sur Mac : `python3 tools/compile-metal-graphics.py DOSSIER_VIDE`,
avec les mêmes overrides LLVM/SPIR-V que le calcul. Le manifeste utilise des
noms relatifs ; `.gitattributes` préserve les octets sous Windows.

**Aucun rendu GPU de ce corpus n'est encore validé.**
[Rapport Mac](../../../../docs/reports/2026-10-07-metal-graphics.md) ·
[Commande Windows](../../../../docs/VALIDATION-GRAPHICS.md)
