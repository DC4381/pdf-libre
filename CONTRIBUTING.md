# Contribuer

Merci de votre intérêt pour PDF Libre.

## Principes

1. **La lecture ne paie jamais le prix des fonctions avancées.** Toute
   modification qui ralentit l'ouverture, le défilement ou le zoom de plus de
   5 % est refusée ou optimisée avant fusion.
2. **Local et privé.** Aucune fonction ne transmet de document ni ne collecte
   de statistiques.
3. **x64 et ARM64 à égalité.** Toute dépendance nouvelle doit exister en
   version ARM64 native et avoir une licence compatible AGPL-3.0.
4. **Accessible.** Chaque commande est utilisable au clavier et porte un nom
   lisible par les lecteurs d'écran.

## Proposer une modification

- Ouvrez d'abord un ticket pour toute fonction importante.
- Une modification = une demande de fusion, avec des tests.
- Les tests (`ctest`) doivent passer ; l'intégration continue compile x64 et ARM64.
- Signez vos commits (`git commit -s`) : vous certifiez avoir le droit de
  contribuer ce code sous licence AGPL-3.0 (Developer Certificate of Origin).
- Chaque modification est relue par un second mainteneur.

## Traductions

L'interface est rédigée en français. Les textes passent par `tr()` et seront
extraits vers des fichiers de traduction Qt (`.ts`) dans une prochaine version.
