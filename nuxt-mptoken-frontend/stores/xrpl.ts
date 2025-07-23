import { defineStore } from 'pinia'
import { Client, Wallet, xrpToDrops, dropsToXrp } from 'xrpl'

export interface XRPLState {
  client: Client | null
  isConnected: boolean
  connectionStatus: 'disconnected' | 'connecting' | 'connected' | 'error'
  serverUrl: string
  currentAccount: {
    wallet: Wallet | null
    address: string
    balance: string
    sequence: number
  }
  transactions: Array<{
    id: string
    type: string
    status: string
    timestamp: Date
    details: any
  }>
  issuances: Array<{
    id: string
    issuer: string
    outstandingAmount: string
    maximumAmount: string
    assetScale: number
    transferFee: number
    flags: string[]
  }>
}

export const useXRPLStore = defineStore('xrpl', {
  state: (): XRPLState => ({
    client: null,
    isConnected: false,
    connectionStatus: 'disconnected',
    serverUrl: 'ws://localhost:6006',
    currentAccount: {
      wallet: null,
      address: '',
      balance: '0',
      sequence: 0
    },
    transactions: [],
    issuances: []
  }),

  actions: {
    async connectToServer(url?: string) {
      try {
        this.connectionStatus = 'connecting'
        
        if (url) {
          this.serverUrl = url
        }
        
        this.client = new Client(this.serverUrl)
        await this.client.connect()
        
        this.isConnected = true
        this.connectionStatus = 'connected'
        
        this.addTransaction({
          id: Date.now().toString(),
          type: 'CONNECTION',
          status: 'SUCCESS',
          timestamp: new Date(),
          details: { server: this.serverUrl }
        })
        
        return true
      } catch (error: any) {
        this.connectionStatus = 'error'
        this.addTransaction({
          id: Date.now().toString(),
          type: 'CONNECTION',
          status: 'ERROR',
          timestamp: new Date(),
          details: { error: error.message }
        })
        throw error
      }
    },

    async disconnect() {
      if (this.client) {
        await this.client.disconnect()
        this.client = null
      }
      this.isConnected = false
      this.connectionStatus = 'disconnected'
    },

    async generateAccount() {
      try {
        const wallet = Wallet.generate()
        this.currentAccount.wallet = wallet
        this.currentAccount.address = wallet.address
        
        // Set initial values
        this.currentAccount.balance = '0'
        this.currentAccount.sequence = 0
        
        // Try to fund the account if connectedx
        if (this.isConnected && this.client) {
          try {
            // Fund the account directly from genesis wallet            
            // Wait a moment for the transaction to be processed
            await new Promise(resolve => setTimeout(resolve, 2000))
            
            // Refresh account info
            await this.refreshAccountInfo()
          } catch (fundError: any) {
            console.warn('Could not auto-fund account:', fundError.message)
          }
        }
        
        this.addTransaction({
          id: Date.now().toString(),
          type: 'ACCOUNT_GENERATED',
          status: 'SUCCESS',
          timestamp: new Date(),
          details: { address: wallet.address }
        })
        
        return wallet
      } catch (error: any) {
        this.addTransaction({
          id: Date.now().toString(),
          type: 'ACCOUNT_GENERATED',
          status: 'ERROR',
          timestamp: new Date(),
          details: { error: error.message }
        })
        throw error
      }
    },

    async loadAccount(seed: string) {
      try {
        const wallet = Wallet.fromSeed(seed)
        this.currentAccount.wallet = wallet
        this.currentAccount.address = wallet.address
        
        if (this.isConnected && this.client) {
          const accountInfo = await this.client.request({
            command: 'account_info',
            account: wallet.address
          })
          
          this.currentAccount.balance = dropsToXrp(accountInfo.result.account_data.Balance).toString()
          this.currentAccount.sequence = accountInfo.result.account_data.Sequence
        }
        
        this.addTransaction({
          id: Date.now().toString(),
          type: 'ACCOUNT_LOADED',
          status: 'SUCCESS',
          timestamp: new Date(),
          details: { address: wallet.address }
        })
        
        return wallet
      } catch (error: any) {
        this.addTransaction({
          id: Date.now().toString(),
          type: 'ACCOUNT_LOADED',
          status: 'ERROR',
          timestamp: new Date(),
          details: { error: error.message }
        })
        throw error
      }
    },

    async refreshAccountInfo() {
      if (!this.isConnected || !this.client || !this.currentAccount.wallet) {
        return
      }
      
      try {
        const accountInfo = await this.client.request({
          command: 'account_info',
          account: this.currentAccount.address
        })
        
        this.currentAccount.balance = dropsToXrp(accountInfo.result.account_data.Balance).toString()
        this.currentAccount.sequence = accountInfo.result.account_data.Sequence
      } catch (error) {
        // Account might not exist yet after funding - set default values
        this.currentAccount.balance = '0'
        this.currentAccount.sequence = 0
        console.error('Failed to refresh account info:', error)
      }
    },

    async fundAccount(address: string, amount = '1000') {
      if (!this.isConnected || !this.client) {
        throw new Error('Not connected to XRPL')
      }

      try {
        // Use the master account (genesis wallet) for funding in standalone mode
        const masterWallet = Wallet.fromSeed('snoPBrXtMeMyMHUVTgbuqAfg1SUTb', {algorithm: 'secp256k1'});
        
        // Check if master wallet exists and has balance
        try {
          const masterAccountInfo = await this.client.request({
            command: 'account_info',
            account: masterWallet.address
          })
          
          const balance = BigInt(masterAccountInfo.result.account_data.Balance)
          const amountInDrops = BigInt(xrpToDrops(amount))
          
          if (balance < amountInDrops) {
            throw new Error(`Master account has insufficient balance: ${dropsToXrp(balance.toString())} XRP`)
          }
          
          // Create funding transaction
          const transaction = {
            TransactionType: 'Payment',
            Account: masterWallet.address,
            Destination: address,
            Amount: xrpToDrops(amount)
          }
          
          const prepared = await this.client.autofill(transaction as any)
          const signed = masterWallet.sign(prepared)
          const result = await this.client.submitAndWait(signed.tx_blob)
          
          if (result.result.meta && result.result.meta.TransactionResult === 'tesSUCCESS') {
            this.addTransaction({
              id: result.result.hash,
              type: 'ACCOUNT_FUNDED',
              status: 'SUCCESS',
              timestamp: new Date(),
              details: { 
                destination: address, 
                amount: amount + ' XRP',
                fundingAccount: masterWallet.address
              }
            })
            
            // Refresh account info if it's the current account
            if (address === this.currentAccount.address) {
              setTimeout(async () => {
                await this.refreshAccountInfo()
              }, 2000)
            }
            
            return result
          } else {
            throw new Error(`Funding transaction failed: ${result.result.meta?.TransactionResult || 'Unknown error'}`)
          }
          
        } catch (error: any) {
          // If master account doesn't exist or has insufficient funds, try alternative approach
          throw new Error(`Unable to fund from master account: ${error.message}`)
        }
      } catch (error: any) {
        this.addTransaction({
          id: Date.now().toString(),
          type: 'ACCOUNT_FUNDED',
          status: 'ERROR',
          timestamp: new Date(),
          details: { error: error.message, destination: address }
        })
        throw error
      }
    },

    async checkAccountExists(address: string): Promise<boolean> {
      if (!this.isConnected || !this.client) {
        return false
      }

      try {
        await this.client.request({
          command: 'account_info',
          account: address
        })
        return true
      } catch (error) {
        return false
      }
    },

    async getAccountBalance(address: string): Promise<string> {
      if (!this.isConnected || !this.client) {
        return '0'
      }

      try {
        const accountInfo = await this.client.request({
          command: 'account_info',
          account: address
        })
        return dropsToXrp(accountInfo.result.account_data.Balance).toString()
      } catch (error) {
        return '0'
      }
    },

    async createMPTokenIssuance(params: {
      assetScale: number
      transferFee: number
      maximumAmount?: string
      metadata?: string
      flags: string[]
    }) {
      if (!this.isConnected || !this.client || !this.currentAccount.wallet) {
        throw new Error('Not connected or no account loaded')
      }
      
      try {
        const transaction = {
          TransactionType: 'MPTokenIssuanceCreate',
          Account: this.currentAccount.address,
          AssetScale: params.assetScale,
          TransferFee: params.transferFee,
          ...(params.maximumAmount && { MaximumAmount: params.maximumAmount }),
          ...(params.metadata && { Metadata: params.metadata }),
          Flags: this.calculateFlags(params.flags)
        }
        
        const prepared = await this.client.autofill(transaction as any)
        const signed = this.currentAccount.wallet.sign(prepared)
        const result = await this.client.submitAndWait(signed.tx_blob)
        
        if (result.result.meta) {
          this.addTransaction({
            id: result.result.hash,
            type: 'MPTOKEN_ISSUANCE_CREATE',
            status: result.result.meta.TransactionResult as string,
            timestamp: new Date(),
            details: { transaction: prepared, result }
          })
        }
        
        await this.refreshAccountInfo()
        return result
      } catch (error: any) {
        this.addTransaction({
          id: Date.now().toString(),
          type: 'MPTOKEN_ISSUANCE_CREATE',
          status: 'ERROR',
          timestamp: new Date(),
          details: { error: error.message }
        })
        throw error
      }
    },

    async sendMPTokenPayment(params: {
      destination: string
      amount: string
      issuanceId: string
      memo?: string
    }) {
      if (!this.isConnected || !this.client || !this.currentAccount.wallet) {
        throw new Error('Not connected or no account loaded')
      }
      
      try {
        const transaction = {
          TransactionType: 'Payment',
          Account: this.currentAccount.address,
          Destination: params.destination,
          Amount: {
            currency: 'MPT',
            value: params.amount,
            issuer: params.issuanceId
          },
          ...(params.memo && { Memos: [{ Memo: { MemoData: Buffer.from(params.memo).toString('hex') } }] })
        }
        
        const prepared = await this.client.autofill(transaction as any)
        const signed = this.currentAccount.wallet.sign(prepared)
        const result = await this.client.submitAndWait(signed.tx_blob)
        
        if (result.result.meta) {
          this.addTransaction({
            id: result.result.hash,
            type: 'MPTOKEN_PAYMENT',
            status: result.result.meta.TransactionResult as string,
            timestamp: new Date(),
            details: { transaction: prepared, result }
          })
        }
        
        await this.refreshAccountInfo()
        return result
      } catch (error: any) {
        this.addTransaction({
          id: Date.now().toString(),
          type: 'MPTOKEN_PAYMENT',
          status: 'ERROR',
          timestamp: new Date(),
          details: { error: error.message }
        })
        throw error
      }
    },

    calculateFlags(flagNames: string[]): number {
      const flagMap: Record<string, number> = {
        'Can Lock': 0x00000001,
        'Require Auth': 0x00000002,
        'Can Escrow': 0x00000004,
        'Can Trade': 0x00000008,
        'Can Transfer': 0x00000010,
        'Can Clawback': 0x00000020
      }
      
      return flagNames.reduce((flags, name) => {
        return flags | (flagMap[name] || 0)
      }, 0)
    },

    addTransaction(transaction: {
      id: string
      type: string
      status: string
      timestamp: Date
      details: any
    }) {
      this.transactions.unshift(transaction)
      
      // Keep only last 100 transactions
      if (this.transactions.length > 100) {
        this.transactions = this.transactions.slice(0, 100)
      }
    },

    clearTransactions() {
      this.transactions = []
    },

    exportTransactions() {
      const data = JSON.stringify(this.transactions, null, 2)
      const blob = new Blob([data], { type: 'application/json' })
      const url = URL.createObjectURL(blob)
      const a = document.createElement('a')
      a.href = url
      a.download = `xrpl-transactions-${new Date().toISOString()}.json`
      a.click()
      URL.revokeObjectURL(url)
    }
  },

  getters: {
    isAccountLoaded: (state) => !!state.currentAccount.wallet,
    formattedBalance: (state) => `${state.currentAccount.balance} XRP`,
    recentTransactions: (state) => state.transactions.slice(0, 10)
  }
})