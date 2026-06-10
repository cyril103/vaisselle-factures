# Installation sur Linux Mint

## Méthode simple depuis les sources

Ouvrir un terminal, puis installer les dépendances :

```bash
sudo apt update
sudo apt install build-essential pkg-config libgtk-3-dev libsqlite3-dev
```

Compiler :

```bash
git clone https://github.com/cyril103/vaisselle-factures.git
cd vaisselle-factures
make
```

Lancer :

```bash
./vaisselle-factures
```

Installer dans le système, optionnel :

```bash
sudo make install
```

## Données

La base SQLite est créée automatiquement dans :

```text
~/.local/share/vaisselle-factures/vaisselle_factures.db
```

Les factures exportées en PDF sont créées dans :

```text
~/FacturesVaisselle/
```

Ces fichiers restent sur l’ordinateur : l’application fonctionne hors ligne.
