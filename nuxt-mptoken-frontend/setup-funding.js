import { Client, Wallet, xrpToDrops } from 'xrpl';

async function setupFunding() {
  console.log('🏦 Setting up funding account for testing...');
  
  const client = new Client('ws://localhost:6006');
  await client.connect();
  
  // Generate a new funding wallet
  const fundingWallet = Wallet.generate();
  console.log('Generated funding wallet:', fundingWallet.address);
  console.log('Funding wallet seed:', fundingWallet.seed);
  
  // For now, we'll use the known test seed
  const masterWallet = Wallet.fromSeed('snoPBrXtMeMyMHUVTgbuqAfg1SUTb', {algorithm: 'secp256k1'});
  console.log('Test wallet address:', masterWallet.address);
  
  // Check if test wallet exists
  try {
    const testAccountInfo = await client.request({
      command: 'account_info',
      account: masterWallet.address
    });
    console.log('Test wallet already exists with balance:', testAccountInfo.result.account_data.Balance);
  } catch (error) {
    console.log('Test wallet does not exist - we need to fund it from master');
    
    // For a standalone rippled, we would need the master key
    // Let's try some common master seeds
          const transaction = {
            TransactionType: 'Payment',
            Account: masterWallet.address,
            Destination: testWallet.address,
            Amount: xrpToDrops('10000') // 10,000 XRP
          };
          
          const prepared = await client.autofill(transaction);
          const signed = masterWallet.sign(prepared);
          const result = await client.submitAndWait(signed.tx_blob);
          
          if (result.result.meta.TransactionResult === 'tesSUCCESS') {
            console.log('✅ Successfully funded test wallet!');
            console.log('Transaction hash:', result.result.hash);
          }
  }
  
  await client.disconnect();
}

setupFunding().catch(console.error);