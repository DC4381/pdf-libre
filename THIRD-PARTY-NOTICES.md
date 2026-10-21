# Composants tiers

CARMO PDF est distribué sous licence GNU AGPL version 3. Il intègre les
composants suivants.

| Composant | Rôle | Licence | Source |
| --- | --- | --- | --- |
| MuPDF (Artifex Software) | Lecture et rendu des PDF | GNU AGPL 3.0 | https://mupdf.com |
| Bibliothèques tierces de MuPDF (FreeType, HarfBuzz, zlib, libjpeg, OpenJPEG, jbig2dec, lcms2, MuJS, Gumbo, Tesseract, Leptonica, Brotli…) | Polices, images, compression | Licences libres respectives, voir le dossier `thirdparty` des sources de MuPDF | https://mupdf.com |
| Qt 6 (The Qt Company) | Interface graphique | GNU LGPL 3.0, liaison dynamique : les DLL Qt livrées peuvent être remplacées | https://www.qt.io |
| Bibliothèques d'exécution Microsoft Visual C++ | Exécution du programme | Licence de redistribution Microsoft | https://visualstudio.microsoft.com |

Le texte de la licence de MuPDF est livré dans `LICENSE-MuPDF.txt`. Les
sources exactes de chaque composant utilisé pour une version publiée sont
indiquées dans `.github/workflows/build.yml` (numéros de version figés).
