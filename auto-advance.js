#!/usr/bin/env node

/**
 * Script simple pour avancer la blockchain de 1 block toutes les 2 secondes
 * Simple script to advance blockchain by 1 block every 2 seconds
 */

const { Client } = require('xrpl');

const SERVER_URL = 'ws://localhost:6006';
const INTERVAL = 2000; // 2 secondes

let client = null;
let blockCount = 0;

async function connect() {
  try {
    client = new Client(SERVER_URL);
    await client.connect();
    console.log('✅ Connecté à rippled');
    return true;
  } catch (error) {
    console.error('❌ Erreur de connexion:', error.message);
    return false;
  }
}

async function advanceBlock() {
  try {
    // Obtenir le ledger actuel
    const currentLedger = await client.request({
      command: 'ledger',
      ledger_index: 'validated'
    });
    
    const currentIndex = currentLedger.result.ledger.ledger_index;
    
    // Fermer manuellement le ledger
    await client.request({
      command: 'ledger_accept'
    });
    
    blockCount++;
    console.log(`📦 Block ${blockCount} - Ledger ${currentIndex} → ${currentIndex + 1}`);
    
  } catch (error) {
    console.error('❌ Erreur lors de l\'avancement:', error.message);
  }
}

async function main() {
  console.log('🚀 Démarrage auto-advance blockchain');
  console.log(`🔗 Serveur: ${SERVER_URL}`);
  console.log(`⏱️  Intervalle: ${INTERVAL/1000}s`);
  console.log('');
  
  if (!(await connect())) {
    process.exit(1);
  }
  
  // Avancer le ledger toutes les 2 secondes
  const interval = setInterval(advanceBlock, INTERVAL);
  
  // Gestion propre de l'arrêt
  process.on('SIGINT', async () => {
    console.log('\n🛑 Arrêt en cours...');
    clearInterval(interval);
    if (client) {
      await client.disconnect();
    }
    console.log(`📊 Total blocks avancés: ${blockCount}`);
    process.exit(0);
  });
}

main();