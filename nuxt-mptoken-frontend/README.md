# 🪙 MPToken Frontend - Interface de Test Avancée

Interface moderne et complète pour tester les fonctionnalités MPToken sur XRP Ledger, construite avec Nuxt 3, Vue 3, et Tailwind CSS.

## ✨ Fonctionnalités Principales

### 🔗 Connexion XRPL
- Connexion WebSocket temps réel à rippled
- Statut de connexion visuel avec indicateurs colorés
- Support serveurs locaux et testnet
- Gestion automatique des erreurs de connexion

### 👤 Gestion des Comptes
- **Génération automatique** de wallets sécurisés
- **Financement automatique** des nouveaux comptes (1000 XRP)
- Chargement de comptes existants via seed
- Vérification en temps réel du solde et statut
- Alertes visuelles pour les balances faibles

### 🏗️ Création d'Émissions MPToken
- Configuration complète : Asset Scale, Transfer Fee, Maximum Amount
- Gestion des flags : Can Lock, Require Auth, Can Transfer, etc.
- Metadata personnalisée en hexadécimal
- Interface intuitive avec validation

### ⚙️ Gestion des Émissions
- Chargement et affichage détaillé des émissions existantes
- Autorisation/désautorisation des détenteurs
- Verrouillage/déverrouillage des tokens
- Destruction sécurisée des émissions
- Actions protégées avec confirmations

### 💸 Paiements MPToken
- Envoi de paiements avec destination tags et mémos
- Vérification automatique des balances
- Historique des paiements récents
- Support des options avancées (SendMax)
- Tests de paiement vers soi-même

### 🔍 Explorateur
- Exploration des tokens par compte
- Recherche d'émissions par ID
- Affichage des métadonnées et flags
- Statistiques réseau en temps réel
- Copie facile des identifiants

### 🤖 Automatisation & Tests
- **4 scénarios de test automatisés** :
  - Cycle de vie complet des tokens
  - Flux d'autorisation
  - Scénarios de paiement multiples
  - Tests de stress haute performance
- Gestion automatique des comptes de test
- Configuration des délais et paramètres
- Rapports détaillés avec captures d'écran

## 🚀 Installation et Démarrage

### Prérequis
- Node.js 18+
- rippled en cours d'exécution sur `ws://localhost:6006`

### Installation
```bash
cd nuxt-mptoken-frontend
npm install
```

### Démarrage
```bash
# Serveur de développement
npm run dev
# Interface disponible sur http://localhost:3000

# Tests E2E
npm run test              # Avec interface graphique
npm run test:headless     # Mode headless
npm run test:debug        # Avec DevTools
```

## 💰 Système de Financement Intelligent

L'interface inclut un système de financement automatique qui :

1. **Détecte automatiquement** les comptes non financés
2. **Finance instantanément** avec 1000 XRP via des seeds prédéfinis
3. **Affiche des alertes visuelles** pour les soldes < 10 XRP
4. **Permet le financement manuel** d'autres comptes
5. **Suit l'historique** de tous les financements

### Seeds de Financement (Mode Standalone)
- `snoPBrXtMeMyMHUVTgbuqAfg1SUTb` (Master seed)
- `sEd7rBGm5kxzauRTAV2hbsNz7N46twN` (Seed alternatif)

## 🧪 Workflow de Test Complet

### 1. Préparation de l'Environnement
```bash
# Terminal 1: Démarrer rippled
./rippled --conf rippled.cfg

# Terminal 2: Avancement automatique de la blockchain
npm run auto-advance

# Terminal 3: Interface frontend
cd nuxt-mptoken-frontend && npm run dev
```

### 2. Test Manuel Guidé
1. **Connexion** → Se connecter à `ws://localhost:6006`
2. **Compte** → Générer un compte (auto-financé avec 1000 XRP)
3. **Émission** → Créer une émission MPToken avec flags personnalisés
4. **Autorisation** → S'auto-autoriser pour recevoir les tokens
5. **Paiement** → Envoyer des tokens vers d'autres comptes
6. **Exploration** → Explorer les données via l'onglet Explorer

### 3. Tests Automatisés
```bash
npm run test
# Exécute tous les tests E2E avec Puppeteer
# Génère des captures d'écran automatiques
# Produit un rapport JSON détaillé
```

## 🔧 Architecture Technique

### Stack Technologique
- **Nuxt 3** - Framework Vue.js full-stack
- **Vue 3** - Interface réactive avec Composition API
- **Tailwind CSS** - Styling utilitaire responsive
- **Pinia** - Gestion d'état centralisée
- **XRPL.js** - SDK officiel XRP Ledger
- **Puppeteer** - Tests E2E automatisés

### Store Centralisé (Pinia)
- État XRPL global partagé
- Gestion automatique des transactions
- Cache intelligent des données
- Rafraîchissement périodique

### Composants Modulaires
```
components/
├── CreateIssuanceForm.vue    # Création d'émissions
├── ManageIssuanceForm.vue    # Gestion d'émissions
├── AuthorizeForm.vue         # Autorisations
├── PaymentForm.vue           # Paiements
├── ExplorerPanel.vue         # Exploration
├── AutomationPanel.vue       # Tests automatisés
├── TransactionLog.vue        # Journal des transactions
└── FundingPanel.vue          # Financement des comptes
```

## 🎯 Fonctionnalités Avancées

### Interface Intelligente
- **Mode sombre automatique** selon les préférences système
- **Design responsive** mobile-first
- **Indicateurs visuels** pour tous les statuts
- **Messages contextuels** d'erreur et succès

### Gestion d'Erreurs Robuste
- Retry automatique des connexions
- Validation complète des formulaires
- Messages d'erreur explicites
- Logs détaillés pour debugging

### Performance Optimisée
- Chargement paresseux des composants
- Cache intelligent des requêtes
- Mise à jour réactive en temps réel
- Optimisation pour les gros volumes

## 📊 Cas d'Usage Supportés

### Développement MPToken
- Test complet de tous les types de transactions
- Validation des flags et paramètres
- Simulation de scénarios complexes
- Debugging interactif

### Tests de Régression
- Automatisation complète des workflows
- Vérification des fonctionnalités existantes
- Tests de performance et stress
- Rapports de conformité

### Démonstrations
- Interface prête pour les démonstrations
- Données de test préchargées
- Scénarios de démonstration guidés
- Visualisation claire des concepts MPToken

## 🚀 Prêt pour la Production !

Cette interface fournit tout ce dont vous avez besoin pour développer, tester et déployer des applications MPToken sur XRP Ledger. Elle est optimisée pour :

- ✅ **Développement rapide** avec hot-reload
- ✅ **Tests exhaustifs** automatisés et manuels  
- ✅ **Financement automatique** des comptes
- ✅ **Interface intuitive** pour tous les niveaux
- ✅ **Documentation complète** intégrée
- ✅ **Architecture extensible** et modulaire

**Démarrez maintenant et explorez toute la puissance des MPTokens ! 🪙**