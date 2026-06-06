# Vaisselle Factures

Petit logiciel Linux de facturation pour une entreprise individuelle de location de vaisselle.

Objectif : fonctionner sur un portable ancien sous Linux Mint, sans serveur, sans compte en ligne, avec une interface graphique simple.

## Fonctions incluses

- Fiche entreprise configurable : nom, SIRET, adresse, email, téléphone, mention légale.
- Gestion des clients.
- Création de factures avec lignes de location : désignation, quantité, prix unitaire HT, TVA.
- Numérotation automatique `FAC-AAAA-0001`.
- Statuts : brouillon, envoyée, payée.
- Export HTML imprimable depuis Firefox/Chromium ou LibreOffice.
- Base locale SQLite, utilisable hors ligne.

## Choix techniques

- Langage : C.
- Interface : GTK 3, disponible sur Linux Mint.
- Stockage : SQLite.
- Build : `make` + `pkg-config`.

Ce choix évite Electron et les grosses dépendances, ce qui convient mieux à un vieux portable.

## Installation

Voir [docs/INSTALLATION.md](docs/INSTALLATION.md).

Résumé :

```bash
sudo apt update
sudo apt install build-essential pkg-config libgtk-3-dev libsqlite3-dev
git clone https://github.com/cyril103/vaisselle-factures.git
cd vaisselle-factures
make
./vaisselle-factures
```

## Utilisation rapide

1. Ouvrir l’onglet **Entreprise** et renseigner les informations qui apparaîtront sur les factures.
2. Ouvrir **Clients**, saisir un client et cliquer **Ajouter client**.
3. Ouvrir **Facture**, sélectionner le client, ajouter les lignes de location, puis cliquer **Créer facture**.
4. Dans **Factures**, sélectionner une facture et cliquer **Exporter HTML**.
5. Ouvrir le fichier HTML généré et imprimer en PDF si besoin.

## Sauvegarde

Sauvegarder régulièrement :

```text
~/.local/share/vaisselle-factures/vaisselle_factures.db
~/FacturesVaisselle/
```

## Limites de cette première version

- Pas encore de devis transformables en factures.
- Pas encore de catalogue d’articles réutilisable.
- L’export est en HTML imprimable, volontairement plus léger qu’une dépendance PDF lourde.

Ces évolutions sont prévues facilement grâce à SQLite.
