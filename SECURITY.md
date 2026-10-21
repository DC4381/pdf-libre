# Sécurité

Un fichier PDF peut être piégé. Les failles de sécurité sont traitées en priorité.

## Signaler une faille

N'ouvrez pas de ticket public. Utilisez le signalement privé de GitHub
(onglet **Security** > **Report a vulnerability**) en décrivant le problème et,
si possible, en joignant un fichier de démonstration.

Engagements :

- accusé de réception sous 3 jours ouvrés ;
- correctif publié sous 14 jours pour une faille critique ;
- avis de sécurité public une fois le correctif disponible.

## Versions prises en charge

Seule la dernière version publiée reçoit les correctifs de sécurité.

## Mesures en place

- Aucune connexion réseau, aucun JavaScript exécuté, aucun programme lancé depuis un document.
- Les erreurs de MuPDF sont interceptées : un fichier malformé ou illisible produit un message d'erreur. L'isolation complète dans un processus séparé est prévue pour une version ultérieure.
- Versions des dépendances figées et compilées depuis leurs sources officielles.
