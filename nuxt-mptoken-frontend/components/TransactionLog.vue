<template>
  <div class="space-y-4">
    <div v-if="xrplStore.transactions.length === 0" class="text-center py-8 text-gray-500 dark:text-gray-400">
      No transactions yet
    </div>
    
    <div
      v-for="transaction in xrplStore.recentTransactions"
      :key="transaction.id"
      class="p-4 border border-gray-200 dark:border-gray-700 rounded-lg hover:bg-gray-50 dark:hover:bg-gray-700 transition-colors"
    >
      <div class="flex items-center justify-between mb-2">
        <div class="flex items-center space-x-3">
          <div
            :class="[
              'w-3 h-3 rounded-full',
              getStatusColor(transaction.status)
            ]"
          ></div>
          <span class="font-medium text-gray-900 dark:text-white">
            {{ transaction.type }}
          </span>
          <span
            :class="[
              'px-2 py-1 rounded-full text-xs font-medium',
              getStatusStyles(transaction.status)
            ]"
          >
            {{ transaction.status }}
          </span>
        </div>
        <div class="text-sm text-gray-500 dark:text-gray-400">
          {{ formatTimestamp(transaction.timestamp) }}
        </div>
      </div>
      
      <div class="text-sm text-gray-600 dark:text-gray-300">
        <div v-if="transaction.details.error" class="text-red-600 dark:text-red-400">
          Error: {{ transaction.details.error }}
        </div>
        <div v-else-if="transaction.details.result?.result?.hash" class="font-mono">
          Hash: {{ transaction.details.result.result.hash }}
        </div>
        <div v-else-if="transaction.details.address" class="font-mono">
          Address: {{ transaction.details.address }}
        </div>
        <div v-else-if="transaction.details.server">
          Server: {{ transaction.details.server }}
        </div>
      </div>
      
      <div 
        v-if="transaction.details.result || transaction.details.transaction"
        class="mt-2"
      >
        <button
          @click="toggleDetails(transaction.id)"
          class="text-blue-600 hover:text-blue-800 dark:text-blue-400 dark:hover:text-blue-300 text-sm"
        >
          {{ expandedTransactions[transaction.id] ? 'Hide' : 'Show' }} Details
        </button>
        
        <div v-if="expandedTransactions[transaction.id]" class="mt-2 p-3 bg-gray-100 dark:bg-gray-800 rounded-lg">
          <pre class="text-xs text-gray-600 dark:text-gray-300 whitespace-pre-wrap">{{ JSON.stringify(transaction.details, null, 2) }}</pre>
        </div>
      </div>
    </div>

    <div v-if="xrplStore.transactions.length > 10" class="text-center">
      <button
        @click="showAll = !showAll"
        class="text-blue-600 hover:text-blue-800 dark:text-blue-400 dark:hover:text-blue-300 text-sm"
      >
        {{ showAll ? 'Show Less' : `Show All (${xrplStore.transactions.length})` }}
      </button>
    </div>

    <div v-if="showAll" class="space-y-2">
      <div
        v-for="transaction in xrplStore.transactions.slice(10)"
        :key="transaction.id"
        class="p-3 border border-gray-200 dark:border-gray-700 rounded-lg text-sm"
      >
        <div class="flex items-center justify-between">
          <div class="flex items-center space-x-2">
            <div
              :class="[
                'w-2 h-2 rounded-full',
                getStatusColor(transaction.status)
              ]"
            ></div>
            <span class="font-medium">{{ transaction.type }}</span>
            <span
              :class="[
                'px-2 py-1 rounded-full text-xs',
                getStatusStyles(transaction.status)
              ]"
            >
              {{ transaction.status }}
            </span>
          </div>
          <div class="text-gray-500 dark:text-gray-400">
            {{ formatTimestamp(transaction.timestamp) }}
          </div>
        </div>
      </div>
    </div>
  </div>
</template>

<script setup>
import { ref } from 'vue'
import { useXRPLStore } from '~/stores/xrpl'

const xrplStore = useXRPLStore()

const expandedTransactions = ref({})
const showAll = ref(false)

function toggleDetails(transactionId) {
  expandedTransactions.value[transactionId] = !expandedTransactions.value[transactionId]
}

function getStatusColor(status) {
  switch (status) {
    case 'SUCCESS':
    case 'tesSUCCESS':
      return 'bg-green-500'
    case 'ERROR':
    case 'FAILED':
      return 'bg-red-500'
    case 'PENDING':
      return 'bg-yellow-500'
    default:
      return 'bg-gray-500'
  }
}

function getStatusStyles(status) {
  switch (status) {
    case 'SUCCESS':
    case 'tesSUCCESS':
      return 'bg-green-100 text-green-800 dark:bg-green-900 dark:text-green-300'
    case 'ERROR':
    case 'FAILED':
      return 'bg-red-100 text-red-800 dark:bg-red-900 dark:text-red-300'
    case 'PENDING':
      return 'bg-yellow-100 text-yellow-800 dark:bg-yellow-900 dark:text-yellow-300'
    default:
      return 'bg-gray-100 text-gray-800 dark:bg-gray-900 dark:text-gray-300'
  }
}

function formatTimestamp(timestamp) {
  return new Date(timestamp).toLocaleString()
}
</script>