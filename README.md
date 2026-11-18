# WANTED — Heat Distortion

Plugin de distorsion **Underwater Palace**. L'interface : 6 étoiles sur fond noir, rien d'autre.
Plus tu montes le niveau de recherche, plus la disto chauffe.

- Son : topologie **Pro Co Rat** (comme UAD Raw) — étage op-amp à gain dépendant de la fréquence,
  limite de bande passante LM308, rails + diodes silicium, filtre de sortie.
- 1★ → 4★ : la course du pot DISTORTION. 5★ / 6★ : au-delà de la pédale.
- Auto gain (volume perçu pondéré K) toujours actif, limiteur + écrêtage doux de sécurité.
- Suréchantillonnage 16× (44.1/48 kHz), 8× (88.2/96 kHz).
- Bandeau 4:1 redimensionnable (300×75 → 1200×300), 480×120 à l'ouverture.
- Formats : VST3 (Windows / macOS), AU (macOS). Universal binary sur Mac.

## Télécharger
Onglet **Releases** → zip Windows ou macOS (voir `INSTALL.txt`).

## Compiler
```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```
Les builds Windows + macOS sont faits automatiquement par GitHub Actions à chaque push.
