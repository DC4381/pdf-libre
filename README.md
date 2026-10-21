# PDF Libre

Lecteur PDF gratuit, libre et rapide pour Windows, inspiré de SumatraPDF.
Aucun compte, aucune publicité, aucun abonnement : vos documents restent sur
votre ordinateur.

Version actuelle : **0.1** (lecture). La feuille de route complète
(annotations, organisation des pages, formulaires, OCR, signatures) figure
dans le cahier des charges du projet.

## Installer

Téléchargez la dernière version dans l'onglet **Releases** du dépôt :

| Fichier | Pour qui |
| --- | --- |
| `PDFLibre-x.y.z-installation.exe` | Tout le monde. Un seul fichier pour PC classiques (x64) et PC ARM (Surface, Snapdragon) : la bonne version est choisie automatiquement. Aucun droit administrateur requis. |
| `PDFLibre-x.y.z-portable-x64.zip` | Version sans installation pour PC classique (clé USB, poste verrouillé). |
| `PDFLibre-x.y.z-portable-arm64.zip` | Version sans installation pour Surface et PC ARM. |
| `SHA256SUMS.txt` | Empreintes pour vérifier que les fichiers n'ont pas été modifiés. |

Systèmes pris en charge : Windows 11 (x64 et ARM64 natif) ; Windows 10 22H2
x64 sans garantie.

**Avertissement Windows SmartScreen** : tant que l'installateur n'est pas signé
par un certificat de signature de code, Windows peut afficher « Windows a
protégé votre ordinateur ». Cliquez sur « Informations complémentaires » puis
« Exécuter quand même ». Vérifiez au préalable l'empreinte SHA-256.

**Désinstaller** : Paramètres > Applications > PDF Libre > Désinstaller. Vos
réglages sont conservés, sauf si vous demandez leur suppression.

**Version portable** : décompressez le ZIP où vous voulez et lancez
`PDFLibre.exe`. Le fichier `portable.ini` présent dans le dossier active le
mode portable : rien n'est écrit dans le registre ni dans votre profil.

## Fonctions de la version 0.1

- Ouverture rapide, affichage progressif, documents volumineux (rendu des seules pages visibles).
- Onglets, glisser-déposer, documents récents, reprise à la dernière page lue.
- Recherche plein texte en arrière-plan, occurrences surlignées.
- Miniatures, signets du document, aller à la page.
- Zoom de 10 % à 6 400 % net (rendu par zones aux forts zooms), ajuster à la largeur ou à la page.
- Tactile et stylet : défilement au doigt, pincer pour zoomer (Surface).
- Impression (plage de pages, rotation automatique des pages paysage).
- Rechargement automatique quand le fichier est modifié par un autre programme.
- Plein écran, documents protégés par mot de passe.

## Raccourcis clavier

Repris de SumatraPDF autant que possible.

| Action | Touches |
| --- | --- |
| Ouvrir | Ctrl+O |
| Rechercher / suivant / précédent | Ctrl+F / F3 / Maj+F3 |
| Page suivante / précédente | N / P, Page suivante / Page précédente |
| Défiler | J / K, flèches, Espace |
| Première / dernière page | Début / Fin |
| Aller à la page | Ctrl+G |
| Zoom avant / arrière | Ctrl++ / Ctrl+-, Ctrl+molette |
| Page entière / taille réelle / largeur | Ctrl+0 / Ctrl+1 / Ctrl+2 |
| Panneau latéral | F12 |
| Plein écran | F11 (Échap pour sortir) |
| Onglet suivant / précédent | Ctrl+Tab / Ctrl+Maj+Tab |
| Recharger | Ctrl+R |
| Imprimer | Ctrl+P |

## Confidentialité

PDF Libre ne se connecte à aucun serveur. Aucune télémétrie, aucune
statistique, aucun envoi de document.

## Compiler

Le code est en C++20 avec Qt 6 et MuPDF.

**Windows** : la compilation officielle est automatisée dans
`.github/workflows/build.yml` (x64 et ARM64 natifs). Pour compiler à la main :
Visual Studio 2022, CMake 3.21+, Qt 6.10 (MSVC), puis MuPDF compilé avec
`platform/win32/mupdf.sln` (cible `libmupdf`, configuration Release), copié
dans un dossier contenant `include/` et `lib/libmupdf.lib` :

```
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DCMAKE_PREFIX_PATH=C:/Qt/6.10.3/msvc2022_64 -DMUPDF_ROOT=C:/mupdf-dist
cmake --build build --config Release
ctest --test-dir build -C Release
```

**Linux** (développement et tests) :

```
sudo apt install ninja-build qt6-base-dev libmupdf-dev libharfbuzz-dev libfreetype-dev libjbig2dec0-dev libopenjp2-7-dev libjpeg-dev libgumbo-dev libmujs-dev
cmake -S . -B build -G Ninja && cmake --build build && ctest --test-dir build
```

## Architecture

| Dossier | Rôle |
| --- | --- |
| `src/core` | Noyau sans interface : enveloppe MuPDF, service de rendu et service de recherche, chacun dans son propre thread avec sa propre instance du document. |
| `src/ui` | Interface Qt : vue continue des pages, onglets, miniatures, signets, recherche, impression. |
| `src/app` | Réglages (dont le mode portable). |
| `tests` | Tests automatisés du noyau et de l'interface, exécutés à chaque modification. |
| `installer` | Script Inno Setup de l'installateur unique x64 + ARM64. |

## Licence

GNU Affero General Public License version 3 (voir `LICENSE`). Composants
tiers : voir `THIRD-PARTY-NOTICES.md`.
