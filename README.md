# Stratum Synthetic Share Test Client

Outil C++ minimal pour **tester un pool Stratum personnel** avec un débit de shares synthétique configurable.

> ⚠️ Cet outil n'implémente pas de preuve de travail réelle. Les shares envoyés sont artificiels et ne doivent être utilisés que sur un environnement de test contrôlé (pool personnel / staging).

## Build

```bash
cmake -S . -B build
cmake --build build -j
```

## Usage

```bash
./build/stratum_test_client <stratum_url> <wallet_or_user> <worker> [password=x] [ehs=1.0] [difficulty=1.0] [nonce_start=0]
```

Exemple:

```bash
./build/stratum_test_client stratum+tcp://127.0.0.1:3333 test.wallet rig01 x 1.0 8.0 0
```

## Comportement

- Connexion TCP au serveur Stratum (`stratum+tcp://host:port`).
- Envoi `mining.subscribe` puis `mining.authorize`.
- Réception des notifications (`mining.notify`, `mining.set_extranonce`) pour récupérer les champs utiles.
- Soumission périodique de `mining.submit` avec:
  - `job_id` courant,
  - `extranonce2` incrémental,
  - `ntime` du job,
  - `nonce` incrémental.
- Cadence de soumission calculée à partir de:
  - hashrate annoncé (EH/s),
  - difficulté synthétique.

Formule:

```text
interval_seconds = (difficulty * 2^32) / (ehs * 10^18)
```

Un plancher de 1 ms est appliqué pour éviter une boucle excessive.
