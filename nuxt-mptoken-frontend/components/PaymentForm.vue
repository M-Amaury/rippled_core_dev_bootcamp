<template>
  <div class="space-y-6">
    <!-- Payment Form -->
    <div class="bg-white dark:bg-gray-700 rounded-lg p-6 border border-gray-200 dark:border-gray-600">
      <form @submit.prevent="sendPayment" class="space-y-4">
        <div class="grid grid-cols-1 md:grid-cols-2 gap-4">
          <div>
            <label class="block text-sm font-medium text-gray-700 dark:text-gray-300 mb-2">
              Destination Address
            </label>
            <input
              v-model="form.destination"
              type="text"
              placeholder="rAddress... (recipient)"
              class="w-full px-4 py-2 border border-gray-300 dark:border-gray-600 rounded-lg focus:ring-2 focus:ring-blue-500 dark:bg-gray-700 dark:text-white"
              required
            />
          </div>

          <div>
            <label class="block text-sm font-medium text-gray-700 dark:text-gray-300 mb-2">
              Amount
            </label>
            <input
              v-model="form.amount"
              type="text"
              placeholder="100.50"
              class="w-full px-4 py-2 border border-gray-300 dark:border-gray-600 rounded-lg focus:ring-2 focus:ring-blue-500 dark:bg-gray-700 dark:text-white"
              required
            />
          </div>
        </div>

        <div>
          <label class="block text-sm font-medium text-gray-700 dark:text-gray-300 mb-2">
            MPToken Issuance ID
          </label>
          <input
            v-model="form.issuanceId"
            type="text"
            placeholder="00000000... (MPToken Issuance ID)"
            class="w-full px-4 py-2 border border-gray-300 dark:border-gray-600 rounded-lg focus:ring-2 focus:ring-blue-500 dark:bg-gray-700 dark:text-white"
            required
          />
        </div>

        <div>
          <label class="block text-sm font-medium text-gray-700 dark:text-gray-300 mb-2">
            Memo (optional)
          </label>
          <input
            v-model="form.memo"
            type="text"
            placeholder="Payment description or note"
            class="w-full px-4 py-2 border border-gray-300 dark:border-gray-600 rounded-lg focus:ring-2 focus:ring-blue-500 dark:bg-gray-700 dark:text-white"
          />
        </div>

        <!-- Advanced Options -->
        <div class="border-t border-gray-200 dark:border-gray-600 pt-4">
          <button
            type="button"
            @click="showAdvanced = !showAdvanced"
            class="flex items-center text-sm text-blue-600 hover:text-blue-800 dark:text-blue-400 dark:hover:text-blue-300"
          >
            <Icon :name="showAdvanced ? 'heroicons:chevron-up' : 'heroicons:chevron-down'" class="mr-1" />
            Advanced Options
          </button>
          
          <div v-if="showAdvanced" class="mt-4 space-y-4 bg-gray-50 dark:bg-gray-800 p-4 rounded-lg">
            <div class="grid grid-cols-1 md:grid-cols-2 gap-4">
              <div>
                <label class="block text-sm font-medium text-gray-700 dark:text-gray-300 mb-2">
                  Destination Tag (optional)
                </label>
                <input
                  v-model.number="form.destinationTag"
                  type="number"
                  placeholder="123456"
                  class="w-full px-4 py-2 border border-gray-300 dark:border-gray-600 rounded-lg focus:ring-2 focus:ring-blue-500 dark:bg-gray-700 dark:text-white"
                />
              </div>
              
              <div>
                <label class="block text-sm font-medium text-gray-700 dark:text-gray-300 mb-2">
                  Send Max (optional)
                </label>
                <input
                  v-model="form.sendMax"
                  type="text"
                  placeholder="Maximum amount willing to send"
                  class="w-full px-4 py-2 border border-gray-300 dark:border-gray-600 rounded-lg focus:ring-2 focus:ring-blue-500 dark:bg-gray-700 dark:text-white"
                />
              </div>
            </div>
          </div>
        </div>

        <div class="flex justify-end space-x-4">
          <button
            type="button"
            @click="resetForm"
            class="px-6 py-2 bg-gray-500 hover:bg-gray-600 text-white rounded-lg font-medium transition-colors"
          >
            Reset
          </button>
          <button
            type="submit"
            :disabled="!canSubmit || loading"
            class="px-6 py-2 bg-blue-600 hover:bg-blue-700 disabled:opacity-50 text-white rounded-lg font-medium transition-colors flex items-center"
          >
            <Icon v-if="loading" name="heroicons:arrow-path" class="animate-spin mr-2" />
            <Icon v-else name="heroicons:paper-airplane" class="mr-2" />
            {{ loading ? 'Sending...' : 'Send Payment' }}
          </button>
        </div>
      </form>
    </div>

    <!-- Quick Actions -->
    <div class="bg-blue-50 dark:bg-blue-900/20 rounded-lg p-4 border border-blue-200 dark:border-blue-800">
      <h4 class="font-medium text-blue-800 dark:text-blue-300 mb-3">Quick Actions</h4>
      <div class="grid grid-cols-1 md:grid-cols-3 gap-3">
        <button
          @click="fillTestData"
          class="px-4 py-2 bg-blue-600 hover:bg-blue-700 text-white rounded-lg font-medium transition-colors"
        >
          Fill Test Data
        </button>
        <button
          @click="sendToSelf"
          :disabled="!canSubmit || loading"
          class="px-4 py-2 bg-purple-600 hover:bg-purple-700 disabled:opacity-50 text-white rounded-lg font-medium transition-colors"
        >
          Send to Self
        </button>
        <button
          @click="checkBalance"
          :disabled="!form.issuanceId"
          class="px-4 py-2 bg-green-600 hover:bg-green-700 disabled:opacity-50 text-white rounded-lg font-medium transition-colors"
        >
          Check Balance
        </button>
      </div>
    </div>

    <!-- Balance Info -->
    <div v-if="balanceInfo" class="bg-white dark:bg-gray-700 rounded-lg p-6 border border-gray-200 dark:border-gray-600">
      <h4 class="font-semibold text-lg mb-4 flex items-center">
        <Icon name="heroicons:banknotes" class="mr-2 text-green-500" />
        MPToken Balance
      </h4>
      
      <div class="grid grid-cols-1 md:grid-cols-3 gap-4">
        <div class="p-4 bg-gray-50 dark:bg-gray-600 rounded-lg">
          <p class="text-sm text-gray-600 dark:text-gray-400">Account</p>
          <p class="font-mono text-sm">{{ balanceInfo.account }}</p>
        </div>
        <div class="p-4 bg-gray-50 dark:bg-gray-600 rounded-lg">
          <p class="text-sm text-gray-600 dark:text-gray-400">Issuance ID</p>
          <p class="font-mono text-sm">{{ balanceInfo.issuanceId }}</p>
        </div>
        <div class="p-4 bg-green-50 dark:bg-green-900/20 rounded-lg">
          <p class="text-sm text-gray-600 dark:text-gray-400">Balance</p>
          <p class="font-bold text-lg text-green-600 dark:text-green-400">{{ formatBalance(balanceInfo.balance) }}</p>
        </div>
      </div>
    </div>

    <!-- Payment History -->
    <div class="bg-white dark:bg-gray-700 rounded-lg p-6 border border-gray-200 dark:border-gray-600">
      <h4 class="font-semibold text-lg mb-4 flex items-center">
        <Icon name="heroicons:clock" class="mr-2 text-purple-500" />
        Recent Payments
      </h4>
      
      <div v-if="recentPayments.length === 0" class="text-center py-8 text-gray-500 dark:text-gray-400">
        No payments yet
      </div>
      
      <div v-else class="space-y-3">
        <div
          v-for="payment in recentPayments"
          :key="payment.id"
          class="p-4 border border-gray-200 dark:border-gray-600 rounded-lg hover:bg-gray-50 dark:hover:bg-gray-600 transition-colors"
        >
          <div class="flex items-center justify-between">
            <div class="flex items-center space-x-3">
              <div
                :class="[
                  'w-3 h-3 rounded-full',
                  payment.status === 'tesSUCCESS' ? 'bg-green-500' : 'bg-red-500'
                ]"
              ></div>
              <div>
                <p class="font-medium">{{ payment.destination }}</p>
                <p class="text-sm text-gray-500 dark:text-gray-400">Amount: {{ payment.amount }}</p>
              </div>
            </div>
            <div class="text-right">
              <p class="text-sm font-mono">{{ payment.hash.substring(0, 16) }}...</p>
              <p class="text-xs text-gray-500 dark:text-gray-400">{{ formatTime(payment.timestamp) }}</p>
            </div>
          </div>
        </div>
      </div>
    </div>

    <!-- Result Message -->
    <div v-if="message" :class="messageClass" class="p-4 rounded-lg">
      <div class="flex items-center">
        <Icon :name="messageIcon" class="mr-2" />
        {{ message }}
      </div>
    </div>
  </div>
</template>

<script setup>
import { ref, computed } from 'vue'
import { useXRPLStore } from '~/stores/xrpl'

const xrplStore = useXRPLStore()

const form = ref({
  destination: '',
  amount: '',
  issuanceId: '',
  memo: '',
  destinationTag: null,
  sendMax: ''
})

const showAdvanced = ref(false)
const loading = ref(false)
const message = ref('')
const messageType = ref('info')
const balanceInfo = ref(null)

const canSubmit = computed(() => {
  return xrplStore.isConnected && 
         xrplStore.isAccountLoaded && 
         form.value.destination.trim() && 
         form.value.amount.trim() && 
         form.value.issuanceId.trim()
})

const recentPayments = computed(() => {
  return xrplStore.transactions
    .filter(tx => tx.type === 'Payment' || tx.type === 'MPTOKEN_PAYMENT')
    .slice(0, 5)
    .map(tx => ({
      id: tx.id,
      destination: tx.details.transaction?.Destination || 'Unknown',
      amount: tx.details.transaction?.Amount?.value || tx.details.transaction?.Amount || 'Unknown',
      hash: tx.id,
      status: tx.status,
      timestamp: tx.timestamp
    }))
})

const messageClass = computed(() => {
  const base = 'border'
  switch (messageType.value) {
    case 'success': return `${base} bg-green-50 border-green-200 text-green-800 dark:bg-green-900/20 dark:border-green-800 dark:text-green-300`
    case 'error': return `${base} bg-red-50 border-red-200 text-red-800 dark:bg-red-900/20 dark:border-red-800 dark:text-red-300`
    default: return `${base} bg-blue-50 border-blue-200 text-blue-800 dark:bg-blue-900/20 dark:border-blue-800 dark:text-blue-300`
  }
})

const messageIcon = computed(() => {
  switch (messageType.value) {
    case 'success': return 'heroicons:check-circle'
    case 'error': return 'heroicons:x-circle'
    default: return 'heroicons:information-circle'
  }
})

function showMessage(text, type = 'info') {
  message.value = text
  messageType.value = type
  setTimeout(() => {
    message.value = ''
  }, 8000)
}

async function sendPayment() {
  if (!canSubmit.value) return
  
  loading.value = true
  try {
    const transaction = {
      TransactionType: 'Payment',
      Account: xrplStore.currentAccount.address,
      Destination: form.value.destination,
      Amount: {
        currency: 'MPT',
        value: form.value.amount,
        issuer: form.value.issuanceId
      },
      ...(form.value.destinationTag && { DestinationTag: form.value.destinationTag }),
      ...(form.value.sendMax && { 
        SendMax: {
          currency: 'MPT',
          value: form.value.sendMax,
          issuer: form.value.issuanceId
        }
      }),
      ...(form.value.memo && { 
        Memos: [{ 
          Memo: { 
            MemoData: Buffer.from(form.value.memo, 'utf8').toString('hex').toUpperCase()
          } 
        }] 
      })
    }
    
    const result = await submitTransaction(transaction)
    
    if (result.result.meta.TransactionResult === 'tesSUCCESS') {
      showMessage(`Payment sent successfully! Hash: ${result.result.hash}`, 'success')
      resetForm()
    } else {
      throw new Error(`Transaction failed: ${result.result.meta.TransactionResult}`)
    }
    
  } catch (error) {
    showMessage(`Payment failed: ${error.message}`, 'error')
  } finally {
    loading.value = false
  }
}

async function sendToSelf() {
  form.value.destination = xrplStore.currentAccount.address
  await sendPayment()
}

function fillTestData() {
  form.value.destination = 'rDestinationExampleAddress123456789'
  form.value.amount = '100'
  form.value.memo = 'Test payment from MPToken interface'
}

async function checkBalance() {
  if (!form.value.issuanceId) return
  
  loading.value = true
  try {
    const response = await xrplStore.client.request({
      command: 'account_objects',
      account: xrplStore.currentAccount.address,
      type: 'mpt_issuance'
    })
    
    const tokenObject = response.result.account_objects.find(obj => 
      obj.MPTokenIssuanceID === form.value.issuanceId
    )
    
    if (tokenObject) {
      balanceInfo.value = {
        account: xrplStore.currentAccount.address,
        issuanceId: form.value.issuanceId,
        balance: tokenObject.MPTAmount || '0'
      }
      showMessage('Balance retrieved successfully', 'success')
    } else {
      balanceInfo.value = {
        account: xrplStore.currentAccount.address,
        issuanceId: form.value.issuanceId,
        balance: '0'
      }
      showMessage('No balance found for this token', 'info')
    }
    
  } catch (error) {
    showMessage(`Failed to check balance: ${error.message}`, 'error')
  } finally {
    loading.value = false
  }
}

async function submitTransaction(transaction) {
  const prepared = await xrplStore.client.autofill(transaction)
  const signed = xrplStore.currentAccount.wallet.sign(prepared)
  const result = await xrplStore.client.submitAndWait(signed.tx_blob)
  
  // Add to transaction log
  xrplStore.addTransaction({
    id: result.result.hash,
    type: 'Payment',
    status: result.result.meta.TransactionResult,
    timestamp: new Date(),
    details: { transaction: prepared, result }
  })
  
  await xrplStore.refreshAccountInfo()
  return result
}

function resetForm() {
  form.value = {
    destination: '',
    amount: '',
    issuanceId: '',
    memo: '',
    destinationTag: null,
    sendMax: ''
  }
  balanceInfo.value = null
  showAdvanced.value = false
}

function formatBalance(balance) {
  if (!balance) return '0'
  return new Intl.NumberFormat().format(balance)
}

function formatTime(timestamp) {
  return new Date(timestamp).toLocaleTimeString()
}
</script>