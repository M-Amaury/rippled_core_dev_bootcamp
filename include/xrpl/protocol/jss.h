//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.

    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

#ifndef RIPPLE_PROTOCOL_JSONFIELDS_H_INCLUDED
#define RIPPLE_PROTOCOL_JSONFIELDS_H_INCLUDED

#include <xrpl/json/json_value.h>

namespace ripple {
namespace jss {

// JSON static strings

#define JSS(x) constexpr ::Json::StaticString x(#x)

/* These "StaticString" field names are used instead of string literals to
   optimize the performance of accessing properties of Json::Value objects.

   Most strings have a trailing comment. Here is the legend:

   in: Read by the given RPC handler from its `Json::Value` parameter.
   out: Assigned by the given RPC handler in the `Json::Value` it returns.
   field: A field of at least one type of transaction.
   RPC: Common properties of RPC requests and responses.
   error: Common properties of RPC error responses.
*/

// clang-format off
JSS(AL_size);              // out: GetCounts
JSS(AL_hit_rate);          // out: GetCounts
JSS(AcceptedCredentials);  // out: AccountObjects
JSS(Account);              // in: TransactionSign; field.
JSS(AMMID);                // field
JSS(Amount);               // in: TransactionSign; field.
JSS(Amount2);              // in/out: AMM IOU/XRP pool, deposit, withdraw amount
JSS(Asset);                // in: AMM Asset1
JSS(Asset2);               // in: AMM Asset2
JSS(AssetClass);           // in: Oracle
JSS(AssetPrice);           // in: Oracle
JSS(AuthAccount);          // in: AMM Auction Slot
JSS(AuthAccounts);         // in: AMM Auction Slot
JSS(BaseAsset);            // in: Oracle
JSS(BidMax);               // in: AMM Bid
JSS(BidMin);               // in: AMM Bid
JSS(ClearFlag);            // field.
JSS(DeliverMax);           // out: alias to Amount
JSS(DeliverMin);           // in: TransactionSign
JSS(Destination);          // in: TransactionSign; field.
JSS(EPrice);               // in: AMM Deposit option
JSS(Fee);                  // in/out: TransactionSign; field.
JSS(Flags);                // in/out: TransactionSign; field.
JSS(Holder);               // field.
JSS(Invalid);              //
JSS(Issuer);               // in: Credential transactions
JSS(LastLedgerSequence);   // in: TransactionSign; field
JSS(LastUpdateTime);       // field.
JSS(LimitAmount);          // field.
JSS(NetworkID);            // field.
JSS(LPTokenOut);           // in: AMM Liquidity Provider deposit tokens
JSS(LPTokenIn);            // in: AMM Liquidity Provider withdraw tokens
JSS(LPToken);              // out: AMM Liquidity Provider tokens info
JSS(OfferSequence);        // field.
JSS(OracleDocumentID);     // field
JSS(Owner);                // field
JSS(Paths);                // in/out: TransactionSign
JSS(PriceDataSeries);                    // field.
JSS(PriceData);                          // field.
JSS(Provider);                           // field.
JSS(QuoteAsset);                         // in: Oracle.
JSS(RawTransaction);                     // in: Batch
JSS(RawTransactions);                    // in: Batch
JSS(SLE_hit_rate);                       // out: GetCounts.
JSS(Scale);                              // field.
JSS(SettleDelay);                        // in: TransactionSign
JSS(SendMax);                            // in: TransactionSign
JSS(Sequence);                           // in/out: TransactionSign; field.
JSS(SetFlag);                            // field.
JSS(Signer);                             // field.
JSS(Signers);                            // field.
JSS(SigningPubKey);                      // field.
JSS(Subject);                            // in: Credential transactions
JSS(TakerGets);                          // field.
JSS(TakerPays);                          // field.
JSS(TradingFee);                         // in/out: AMM trading fee
JSS(TransactionType);                    // in: TransactionSign.
JSS(TransferRate);                       // in: TransferRate.
JSS(TxnSignature);                       // field.
JSS(URI);                                // field.
JSS(VoteSlots);                          // out: AMM Vote
JSS(aborted);                            // out: InboundLedger
JSS(accepted);               // out: LedgerToJson, OwnerInfo, SubmitTransaction
JSS(accountState);           // out: LedgerToJson
JSS(accountTreeHash);        // out: ledger/Ledger.cpp
JSS(account_data);           // out: AccountInfo
JSS(account_flags);          // out: AccountInfo
JSS(account_hash);           // out: LedgerToJson
JSS(account_id);             // out: WalletPropose
JSS(account_nfts);           // out: AccountNFTs
JSS(account_objects);        // out: AccountObjects
JSS(account_root);           // in: LedgerEntry
JSS(account_sequence_next);  // out: SubmitTransaction
JSS(account_sequence_available);  // out: SubmitTransaction
JSS(account_history_tx_stream);   // in: Subscribe, Unsubscribe
JSS(account_history_tx_index);    // out: Account txn history subscribe

JSS(account_history_tx_first);   // out: Account txn history subscribe
JSS(account_history_boundary);   // out: Account txn history subscribe
JSS(accounts);                // in: LedgerEntry, Subscribe,
                              //     handlers/Ledger, Unsubscribe
JSS(accounts_proposed);       // in: Subscribe, Unsubscribe
JSS(action);
JSS(acquiring);               // out: LedgerRequest
JSS(address);                 // out: PeerImp
JSS(affected);                // out: AcceptedLedgerTx
JSS(age);                     // out: NetworkOPs, Peers
JSS(alternatives);            // out: PathRequest, RipplePathFind
JSS(amendment_blocked);       // out: NetworkOPs
JSS(amm_account);             // in: amm_info
JSS(amount);                  // out: AccountChannels, amm_info
JSS(amount2);                 // out: amm_info
JSS(api_version);             // in: many, out: Version
JSS(api_version_low);         // out: Version
JSS(applied);                 // out: SubmitTransaction
JSS(asks);                    // out: Subscribe
JSS(asset);                   // in: amm_info
JSS(asset2);                  // in: amm_info
JSS(assets);                  // out: GatewayBalances
JSS(asset_frozen);            // out: amm_info
JSS(asset2_frozen);           // out: amm_info
JSS(attestations);
JSS(attestation_reward_account);
JSS(auction_slot);            // out: amm_info
JSS(authorized);              // out: AccountLines
JSS(authorize);               // out: delegate
JSS(authorized_credentials);   // in: ledger_entry DepositPreauth
JSS(auth_accounts);           // out: amm_info
JSS(auth_change);             // out: AccountInfo
JSS(auth_change_queued);      // out: AccountInfo
JSS(available);               // out: ValidatorList
JSS(avg_bps_recv);            // out: Peers
JSS(avg_bps_sent);            // out: Peers
JSS(balance);                 // out: AccountLines
JSS(balances);                // out: GatewayBalances
JSS(base);                    // out: LogLevel
JSS(base_asset);              // in: get_aggregate_price
JSS(base_fee);                // out: NetworkOPs
JSS(base_fee_xrp);            // out: NetworkOPs
JSS(base_reserve);
JSS(bids);                    // out: Subscribe
JSS(binary);                  // in: AccountTX, LedgerEntry,
                              //     AccountTxOld, Tx LedgerData
JSS(blob);                    // out: ValidatorList
JSS(blobs_v2);                // out: ValidatorList
                              // in: UNL
JSS(books);                   // in: Subscribe, Unsubscribe
JSS(both);                    // in: Subscribe, Unsubscribe
JSS(both_sides);              // in: Subscribe, Unsubscribe
JSS(branch);                  // out: server_info
JSS(broadcast);               // out: SubmitTransaction
JSS(bridge_account);          // in: LedgerEntry
JSS(build_path);              // in: TransactionSign
JSS(build_version);           // out: NetworkOPs
JSS(cancel_after);            // out: AccountChannels
JSS(can_delete);              // out: CanDelete
JSS(mpt_amount);              // out: mpt_holders
JSS(mpt_issuance_id);         // in: Payment, mpt_holders
JSS(mptoken_index);           // out: mpt_holders
JSS(changes);                 // out: BookChanges
JSS(channel_id);              // out: AccountChannels
JSS(channels);                // out: AccountChannels
JSS(check_nodes);             // in: LedgerCleaner
JSS(clear);                   // in/out: FetchInfo
JSS(close);                   // out: BookChanges
JSS(close_flags);             // out: LedgerToJson
JSS(close_time);              // in: Application, out: NetworkOPs,
                              //      RCLCxPeerPos, LedgerToJson
JSS(close_time_iso);          // out: Tx, NetworkOPs, TransactionEntry
                              //      AccountTx, LedgerToJson
JSS(close_time_estimated);    // in: Application, out: LedgerToJson
JSS(close_time_human);        // out: LedgerToJson
JSS(close_time_offset);       // out: NetworkOPs
JSS(close_time_resolution);   // in: Application; out: LedgerToJson
JSS(closed);                  // out: NetworkOPs, LedgerToJson,
                              //      handlers/Ledger
JSS(closed_ledger);           // out: NetworkOPs
JSS(cluster);                 // out: PeerImp
JSS(code);                    // out: errors
JSS(command);                 // in: RPCHandler
JSS(complete);                // out: NetworkOPs, InboundLedger
JSS(complete_ledgers);        // out: NetworkOPs, PeerImp
JSS(consensus);               // out: NetworkOPs, LedgerConsensus
JSS(converge_time);           // out: NetworkOPs
JSS(converge_time_s);         // out: NetworkOPs
JSS(cookie);                  // out: NetworkOPs
JSS(count);                   // in: AccountTx*, ValidatorList
JSS(counters);                // in/out: retrieve counters
JSS(credentials);             // in: deposit_authorized
JSS(credential_type);         // in: LedgerEntry DepositPreauth
JSS(ctid);                    // in/out: Tx RPC
JSS(currency_a);              // out: BookChanges
JSS(currency_b);              // out: BookChanges
JSS(currency);                // in: paths/PathRequest, STAmount
                              // out: STPathSet, STAmount,
                              //      AccountLines
JSS(current);                 // out: OwnerInfo
JSS(current_activities);
JSS(current_ledger_size);     // out: TxQ
JSS(current_queue_size);      // out: TxQ
JSS(data);                    // out: LedgerData
JSS(date);                    // out: tx/Transaction, NetworkOPs
JSS(dbKBLedger);              // out: getCounts
JSS(dbKBTotal);               // out: getCounts
JSS(dbKBTransaction);         // out: getCounts
JSS(debug_signing);           // in: TransactionSign
JSS(deletion_blockers_only);  // in: AccountObjects
JSS(delivered_amount);        // out: insertDeliveredAmount
JSS(deposit_authorized);      // out: deposit_authorized
JSS(deprecated);              // out
JSS(descending);              // in: AccountTx*
JSS(description);             // in/out: Reservations
JSS(destination);             // in: nft_buy_offers, nft_sell_offers
JSS(destination_account);     // in: PathRequest, RipplePathFind, account_lines
                              // out: AccountChannels
JSS(destination_amount);      // in: PathRequest, RipplePathFind
JSS(destination_currencies);  // in: PathRequest, RipplePathFind
JSS(destination_tag);         // in: PathRequest
                              // out: AccountChannels
JSS(details);                 // out: Manifest, server_info
JSS(dir_entry);               // out: DirectoryEntryIterator
JSS(dir_index);               // out: DirectoryEntryIterator
JSS(dir_root);                // out: DirectoryEntryIterator
JSS(discounted_fee);          // out: amm_info
JSS(domain);                  // out: ValidatorInfo, Manifest
JSS(drops);                   // out: TxQ
JSS(duration_us);             // out: NetworkOPs
JSS(effective);               // out: ValidatorList
                              // in: UNL
JSS(enabled);                 // out: AmendmentTable
JSS(engine_result);           // out: NetworkOPs, TransactionSign, Submit
JSS(engine_result_code);      // out: NetworkOPs, TransactionSign, Submit
JSS(engine_result_message);   // out: NetworkOPs, TransactionSign, Submit
JSS(entire_set);              // out: get_aggregate_price
JSS(ephemeral_key);           // out: ValidatorInfo
                              // in/out: Manifest
JSS(error);                   // out: error
JSS(errored);
JSS(error_code);              // out: error
JSS(error_exception);         // out: Submit
JSS(error_message);           // out: error
JSS(expand);                  // in: handler/Ledger
JSS(expected_date);           // out: any (warnings)
JSS(expected_date_UTC);       // out: any (warnings)
JSS(expected_ledger_size);    // out: TxQ
JSS(expiration);              // out: AccountOffers, AccountChannels,
                              //      ValidatorList, amm_info
JSS(fail_hard);               // in: Sign, Submit
JSS(failed);                  // out: InboundLedger
JSS(feature);                 // in: Feature
JSS(features);                // out: Feature
JSS(fee_base);                // out: NetworkOPs
JSS(fee_div_max);             // in: TransactionSign
JSS(fee_level);               // out: AccountInfo
JSS(fee_mult_max);            // in: TransactionSign
JSS(fee_ref);                 // out: NetworkOPs, DEPRECATED
JSS(fetch_pack);              // out: NetworkOPs
JSS(FIELDS);                  // out: RPC server_definitions
                              // matches definitions.json format
JSS(first);                   // out: rpc/Version
JSS(finished);
JSS(fix_txns);                // in: LedgerCleaner
JSS(flags);                   // out: AccountOffers,
                              //      NetworkOPs
JSS(forward);                 // in: AccountTx
JSS(freeze);                  // out: AccountLines
JSS(freeze_peer);             // out: AccountLines
JSS(deep_freeze);                  // out: AccountLines
JSS(deep_freeze_peer);             // out: AccountLines
JSS(frozen_balances);         // out: GatewayBalances
JSS(full);                    // in: LedgerClearer, handlers/Ledger
JSS(full_reply);              // out: PathFind
JSS(fullbelow_size);          // out: GetCounts
JSS(git);                     // out: server_info
JSS(good);                    // out: RPCVersion
JSS(hash);                    // out: NetworkOPs, InboundLedger,
                              //      LedgerToJson, STTx; field
JSS(have_header);             // out: InboundLedger
JSS(have_state);              // out: InboundLedger
JSS(have_transactions);       // out: InboundLedger
JSS(high);                    // out: BookChanges
JSS(highest_sequence);        // out: AccountInfo
JSS(highest_ticket);          // out: AccountInfo
JSS(historical_perminute);    // historical_perminute.
JSS(holders);                 // out: MPTHolders
JSS(hostid);                  // out: NetworkOPs
JSS(hotwallet);               // in: GatewayBalances
JSS(id);                      // websocket.
JSS(ident);                   // in: AccountCurrencies, AccountInfo,
                              //     OwnerInfo
JSS(ignore_default);          // in: AccountLines
JSS(include_reserves);
JSS(include_trustlines);
JSS(inLedger);                // out: tx/Transaction
JSS(inbound);                 // out: PeerImp
JSS(index);                   // in: LedgerEntry
                              // out: STLedgerEntry,
                              //      LedgerEntry, TxHistory, LedgerData
JSS(info);                    // out: ServerInfo, ConsensusInfo, FetchInfo
JSS(initial_sync_duration_us);
JSS(internal_command);        // in: Internal
JSS(invalid_API_version);     // out: Many, when a request has an invalid
                              //      version
JSS(io_latency_ms);           // out: NetworkOPs
JSS(ip);                      // in: Connect, out: OverlayImpl
JSS(is_burned);               // out: nft_info (clio)
JSS(isSerialized);            // out: RPC server_definitions
                              // matches definitions.json format
JSS(isSigningField);          // out: RPC server_definitions
                              // matches definitions.json format
JSS(isVLEncoded);             // out: RPC server_definitions
                              // matches definitions.json format
JSS(issuer);                  // in: RipplePathFind, Subscribe,
                              //     Unsubscribe, BookOffers
                              // out: STPathSet, STAmount
JSS(job);
JSS(job_queue);
JSS(jobs);
JSS(jsonrpc);                 // json version
JSS(jq_trans_overflow);       // JobQueue transaction limit overflow.
JSS(kept);                    // out: SubmitTransaction
JSS(key);                     // out
JSS(key_type);                // in/out: WalletPropose, TransactionSign
JSS(latency);                 // out: PeerImp
JSS(last);                    // out: RPCVersion
JSS(last_close);              // out: NetworkOPs
JSS(last_refresh_time);       // out: ValidatorSite
JSS(last_refresh_status);     // out: ValidatorSite
JSS(last_refresh_message);    // out: ValidatorSite
JSS(ledger);                  // in: NetworkOPs, LedgerCleaner,
                              //     RPCHelpers
                              // out: NetworkOPs, PeerImp
JSS(ledger_current_index);    // out: NetworkOPs, RPCHelpers,
                              //      LedgerCurrent, LedgerAccept,
                              //      AccountLines
JSS(ledger_data);             // out: LedgerHeader
JSS(ledger_hash);             // in: RPCHelpers, LedgerRequest,
                              //     RipplePathFind, TransactionEntry,
                              //     handlers/Ledger
                              // out: NetworkOPs, RPCHelpers,
                              //      LedgerClosed, LedgerData,
                              //      AccountLines
JSS(ledger_hit_rate);         // out: GetCounts
JSS(ledger_index);            // in/out: many
JSS(ledger_index_max);        // in, out: AccountTx*
JSS(ledger_index_min);        // in, out: AccountTx*
JSS(ledger_max);              // in, out: AccountTx*
JSS(ledger_min);              // in, out: AccountTx*
JSS(ledger_time);             // out: NetworkOPs
JSS(LEDGER_ENTRY_TYPES);      // out: RPC server_definitions
                              // matches definitions.json format
JSS(levels);                  // LogLevels
JSS(limit);                   // in/out: AccountTx*, AccountOffers,
                              //         AccountLines, AccountObjects
                              // in: LedgerData, BookOffers
JSS(limit_peer);              // out: AccountLines
JSS(lines);                   // out: AccountLines
JSS(list);                    // out: ValidatorList
JSS(load);                    // out: NetworkOPs, PeerImp
JSS(load_base);               // out: NetworkOPs
JSS(load_factor);             // out: NetworkOPs
JSS(load_factor_cluster);     // out: NetworkOPs
JSS(load_factor_fee_escalation); // out: NetworkOPs
JSS(load_factor_fee_queue);      // out: NetworkOPs
JSS(load_factor_fee_reference);  // out: NetworkOPs
JSS(load_factor_local);       // out: NetworkOPs
JSS(load_factor_net);         // out: NetworkOPs
JSS(load_factor_server);      // out: NetworkOPs
JSS(load_fee);                // out: LoadFeeTrackImp, NetworkOPs
JSS(local);                   // out: resource/Logic.h
JSS(local_txs);               // out: GetCounts
JSS(local_static_keys);       // out: ValidatorList
JSS(locked);                  // out: GatewayBalances
JSS(low);                     // out: BookChanges
JSS(lowest_sequence);         // out: AccountInfo
JSS(lowest_ticket);           // out: AccountInfo
JSS(lp_token);                // out: amm_info
JSS(majority);                // out: RPC feature
JSS(manifest);                // out: ValidatorInfo, Manifest
JSS(marker);                  // in/out: AccountTx, AccountOffers,
                              //         AccountLines, AccountObjects,
                              //         LedgerData
                              // in: BookOffers
JSS(master_key);              // out: WalletPropose, NetworkOPs,
                              //      ValidatorInfo
                              // in/out: Manifest
JSS(master_seed);             // out: WalletPropose
JSS(master_seed_hex);         // out: WalletPropose
JSS(master_signature);        // out: pubManifest
JSS(max_ledger);              // in/out: LedgerCleaner
JSS(max_queue_size);          // out: TxQ
JSS(max_spend_drops);         // out: AccountInfo
JSS(max_spend_drops_total);   // out: AccountInfo
JSS(mean);                    // out: get_aggregate_price
JSS(median);                  // out: get_aggregate_price
JSS(median_fee);              // out: TxQ
JSS(median_level);            // out: TxQ
JSS(message);                 // error.
JSS(meta);                    // out: NetworkOPs, AccountTx*, Tx
JSS(meta_blob);               // out: NetworkOPs, AccountTx*, Tx
JSS(metaData);
JSS(metadata);                // out: TransactionEntry
JSS(method);                  // RPC
JSS(methods);
JSS(metrics);                 // out: Peers
JSS(min_count);               // in: GetCounts
JSS(min_ledger);              // in: LedgerCleaner
JSS(minimum_fee);             // out: TxQ
JSS(minimum_level);           // out: TxQ
JSS(missingCommand);          // error
JSS(name);                    // out: AmendmentTableImpl, PeerImp
JSS(needed_state_hashes);     // out: InboundLedger
JSS(needed_transaction_hashes);  // out: InboundLedger
JSS(network_id);              // out: NetworkOPs
JSS(network_ledger);          // out: NetworkOPs
JSS(next_refresh_time);       // out: ValidatorSite
JSS(nft_id);                  // in: nft_sell_offers, nft_buy_offers
JSS(nft_offer_index);         // out nft_buy_offers, nft_sell_offers
JSS(nft_serial);              // out: account_nfts
JSS(nft_taxon);               // out: nft_info (clio)
JSS(nftoken_id);              // out: insertNFTokenID
JSS(nftoken_ids);             // out: insertNFTokenID
JSS(no_ripple);               // out: AccountLines
JSS(no_ripple_peer);          // out: AccountLines
JSS(node);                    // out: LedgerEntry
JSS(node_binary);             // out: LedgerEntry
JSS(node_read_bytes);         // out: GetCounts
JSS(node_read_errors);        // out: GetCounts
JSS(node_read_retries);       // out: GetCounts
JSS(node_reads_hit);          // out: GetCounts
JSS(node_reads_total);        // out: GetCounts
JSS(node_reads_duration_us);  // out: GetCounts
JSS(node_size);               // out: server_info
JSS(nodes);                   // out: VaultInfo
JSS(nodestore);               // out: GetCounts
JSS(node_writes);             // out: GetCounts
JSS(node_written_bytes);      // out: GetCounts
JSS(node_writes_duration_us); // out: GetCounts
JSS(node_write_retries);      // out: GetCounts
JSS(node_writes_delayed);     // out::GetCounts
JSS(nth);                     // out: RPC server_definitions
JSS(obligations);             // out: GatewayBalances
JSS(offers);                  // out: NetworkOPs, AccountOffers, Subscribe
JSS(offer_id);                // out: insertNFTokenOfferID
JSS(offline);                 // in: TransactionSign
JSS(offset);                  // in/out: AccountTxOld
JSS(open);                    // out: handlers/Ledger
JSS(open_ledger_cost);        // out: SubmitTransaction
JSS(open_ledger_fee);         // out: TxQ
JSS(open_ledger_level);       // out: TxQ
JSS(oracles);                 // in: get_aggregate_price
JSS(oracle_document_id);      // in: get_aggregate_price
JSS(owner);                   // in: LedgerEntry, out: NetworkOPs
JSS(owner_funds);             // in/out: Ledger, NetworkOPs, AcceptedLedgerTx
JSS(owner_reserve);
JSS(page_index);
JSS(params);                  // RPC
JSS(parent_close_time);       // out: LedgerToJson
JSS(parent_hash);             // out: LedgerToJson
JSS(partition);               // in: LogLevel
JSS(passphrase);              // in: WalletPropose
JSS(password);                // in: Subscribe
JSS(paths);                   // in: RipplePathFind
JSS(paths_canonical);         // out: RipplePathFind
JSS(paths_computed);          // out: PathRequest, RipplePathFind
JSS(peer);                    // in: AccountLines
JSS(peer_authorized);         // out: AccountLines
JSS(peer_id);                 // out: RCLCxPeerPos
JSS(peers);                   // out: InboundLedger, handlers/Peers, Overlay
JSS(peer_disconnects);        // Severed peer connection counter.
JSS(peer_disconnects_resources); // Severed peer connections because of
                              // excess resource consumption.
JSS(port);                    // in: Connect, out: NetworkOPs
JSS(ports);                   // out: NetworkOPs
JSS(previous);                // out: Reservations
JSS(previous_ledger);         // out: LedgerPropose
JSS(price);                   // out: amm_info, AuctionSlot
JSS(proof);                   // in: BookOffers
JSS(propose_seq);             // out: LedgerPropose
JSS(proposers);               // out: NetworkOPs, LedgerConsensus
JSS(protocol);                // out: NetworkOPs, PeerImp
JSS(proxied);                 // out: RPC ping
JSS(pubkey_node);             // out: NetworkOPs
JSS(pubkey_publisher);        // out: ValidatorList
JSS(pubkey_validator);        // out: NetworkOPs, ValidatorList
JSS(public_key);              // out: OverlayImpl, PeerImp, WalletPropose,
                              //      ValidatorInfo
                              // in/out: Manifest
JSS(public_key_hex);          // out: WalletPropose
JSS(published_ledger);        // out: NetworkOPs
JSS(publisher_lists);         // out: ValidatorList
JSS(quality);                 // out: NetworkOPs
JSS(quality_in);              // out: AccountLines
JSS(quality_out);             // out: AccountLines
JSS(queue);                   // in: AccountInfo
JSS(queue_data);              // out: AccountInfo
JSS(queued);                  // out: SubmitTransaction
JSS(queued_duration_us);
JSS(quote_asset);             // in: get_aggregate_price
JSS(random);                  // out: Random
JSS(raw_meta);                // out: AcceptedLedgerTx
JSS(receive_currencies);      // out: AccountCurrencies
JSS(reference_level);         // out: TxQ
JSS(refresh_interval);        // in: UNL
JSS(refresh_interval_min);    // out: ValidatorSites
JSS(regular_seed);            // in/out: LedgerEntry
JSS(remaining);               // out: ValidatorList
JSS(remote);                  // out: Logic.h
JSS(request);                 // RPC
JSS(requested);               // out: Manifest
JSS(reservations);            // out: Reservations
JSS(reserves);                // out: AccountBalance
JSS(reserve_base);            // out: NetworkOPs
JSS(reserve_base_xrp);        // out: NetworkOPs
JSS(reserve_inc);             // out: NetworkOPs
JSS(reserve_inc_xrp);         // out: NetworkOPs
JSS(response);                // websocket
JSS(result);                  // RPC
JSS(ripple_lines);            // out: NetworkOPs
JSS(ripple_state);            // in: LedgerEntr
JSS(ripplerpc);               // ripple RPC version
JSS(role);                    // out: Ping.cpp
JSS(rpc);
JSS(rt_accounts);             // in: Subscribe, Unsubscribe
JSS(running_duration_us);
JSS(search_depth);            // in: RipplePathFind
JSS(searched_all);            // out: Tx
JSS(secret);                  // in: TransactionSign,
                              //     ValidationCreate, ValidationSeed,
                              //     channel_authorize
JSS(seed);                    //
JSS(seed_hex);                // in: WalletPropose, TransactionSign
JSS(send_currencies);         // out: AccountCurrencies
JSS(send_max);                // in: PathRequest, RipplePathFind
JSS(seq);                     // in: LedgerEntry;
                              // out: NetworkOPs, RPCSub, AccountOffers,
                              //      ValidatorList, ValidatorInfo, Manifest
JSS(sequence);                // in: UNL
JSS(sequence_count);          // out: AccountInfo
JSS(server_domain);           // out: NetworkOPs
JSS(server_state);            // out: NetworkOPs
JSS(server_state_duration_us);// out: NetworkOPs
JSS(server_status);           // out: NetworkOPs
JSS(server_version);          // out: NetworkOPs
JSS(settle_delay);            // out: AccountChannels
JSS(severity);                // in: LogLevel
JSS(shares);                  // out: VaultInfo
JSS(signature);               // out: NetworkOPs, ChannelAuthorize
JSS(signature_verified);      // out: ChannelVerify
JSS(signing_key);             // out: NetworkOPs
JSS(signing_keys);            // out: ValidatorList
JSS(signing_time);            // out: NetworkOPs
JSS(signer_lists);            // in/out: AccountInfo
JSS(size);                    // out: get_aggregate_price
JSS(snapshot);                // in: Subscribe
JSS(source_account);          // in: PathRequest, RipplePathFind
JSS(source_amount);           // in: PathRequest, RipplePathFind
JSS(source_currencies);       // in: PathRequest, RipplePathFind
JSS(source_tag);              // out: AccountChannels
JSS(stand_alone);             // out: NetworkOPs
JSS(standard_deviation);      // out: get_aggregate_price
JSS(start);                   // in: TxHistory
JSS(started);
JSS(state_accounting);        // out: NetworkOPs
JSS(state_now);               // in: Subscribe
JSS(status);                  // error
JSS(stop);                    // in: LedgerCleaner
JSS(stop_history_tx_only);    // in: Unsubscribe, stop history tx stream
JSS(streams);                 // in: Subscribe, Unsubscribe
JSS(strict);                  // in: AccountCurrencies, AccountInfo
JSS(sub_index);               // in: LedgerEntry
JSS(subcommand);              // in: PathFind
JSS(subject);                 // in: LedgerEntry Credential
JSS(success);                 // rpc
JSS(supported);               // out: AmendmentTableImpl
JSS(sync_mode);               // in: Submit
JSS(system_time_offset);      // out: NetworkOPs
JSS(tag);                     // out: Peers
JSS(taker);                   // in: Subscribe, BookOffers
JSS(taker_gets);              // in: Subscribe, Unsubscribe, BookOffers
JSS(taker_gets_funded);       // out: NetworkOPs
JSS(taker_pays);              // in: Subscribe, Unsubscribe, BookOffers
JSS(taker_pays_funded);       // out: NetworkOPs
JSS(threshold);               // in: Blacklist
JSS(ticket_count);            // out: AccountInfo
JSS(ticket_seq);              // in: LedgerEntry
JSS(time);
JSS(timeouts);                // out: InboundLedger
JSS(time_threshold);          // in/out: Oracle aggregate
JSS(time_interval);           // out: AMM Auction Slot
JSS(track);                   // out: PeerImp
JSS(traffic);                 // out: Overlay
JSS(trim);                    // in: get_aggregate_price
JSS(trimmed_set);             // out: get_aggregate_price
JSS(total);                   // out: counters
JSS(total_bytes_recv);        // out: Peers
JSS(total_bytes_sent);        // out: Peers
JSS(total_coins);             // out: LedgerToJson
JSS(total_reserve);
JSS(trading_fee);             // out: amm_info
JSS(transTreeHash);           // out: ledger/Ledger.cpp
JSS(transaction);             // in: Tx
                              // out: NetworkOPs, AcceptedLedgerTx,
JSS(transaction_hash);        // out: RCLCxPeerPos, LedgerToJson
JSS(transactions);            // out: LedgerToJson,
                              // in: AccountTx*, Unsubscribe
JSS(TRANSACTION_RESULTS);     // out: RPC server_definitions
                              // matches definitions.json format
JSS(TRANSACTION_TYPES);       // out: RPC server_definitions
                              // matches definitions.json format
JSS(TYPES);                   // out: RPC server_definitions
                              // matches definitions.json format
JSS(transfer_rate);           // out: nft_info (clio)
JSS(transitions);             // out: NetworkOPs
JSS(treenode_cache_size);     // out: GetCounts
JSS(treenode_track_size);     // out: GetCounts
JSS(trusted);                 // out: UnlList
JSS(trusted_validator_keys);  // out: ValidatorList
JSS(tx);                      // out: STTx, AccountTx*
JSS(tx_blob);                 // in/out: Submit,
                              // in: TransactionSign, AccountTx*
JSS(tx_hash);                 // in: TransactionEntry
JSS(tx_json);                 // in/out: TransactionSign
                              // out: TransactionEntry
JSS(tx_signing_hash);         // out: TransactionSign
JSS(tx_unsigned);             // out: TransactionSign
JSS(txn_count);               // out: NetworkOPs
JSS(txr_tx_cnt);              // out: protocol message tx's count
JSS(txr_tx_sz);               // out: protocol message tx's size
JSS(txr_have_txs_cnt);        // out: protocol message have tx count
JSS(txr_have_txs_sz);         // out: protocol message have tx size
JSS(txr_get_ledger_cnt);      // out: protocol message get ledger count
JSS(txr_get_ledger_sz);       // out: protocol message get ledger size
JSS(txr_ledger_data_cnt);     // out: protocol message ledger data count
JSS(txr_ledger_data_sz);      // out: protocol message ledger data size
JSS(txr_transactions_cnt);    // out: protocol message get object count
JSS(txr_transactions_sz);     // out: protocol message get object size
JSS(txr_selected_cnt);        // out: selected peers count
JSS(txr_suppressed_cnt);      // out: suppressed peers count
JSS(txr_not_enabled_cnt);     // out: peers with tx reduce-relay disabled count
JSS(txr_missing_tx_freq);     // out: missing tx frequency average
JSS(txs);                     // out: TxHistory
JSS(type);                    // in: AccountObjects
                              // out: NetworkOPs, RPC server_definitions
                              //      OverlayImpl, Logic
JSS(type_hex);                // out: STPathSet
JSS(unl);                     // out: UnlList
JSS(unlimited);               // out: Connection.h
JSS(uptime);                  // out: GetCounts
JSS(uri);                     // out: ValidatorSites
JSS(url);                     // in/out: Subscribe, Unsubscribe
JSS(url_password);            // in: Subscribe
JSS(url_username);            // in: Subscribe
JSS(urlgravatar);             //
JSS(username);                // in: Subscribe
JSS(validated);               // out: NetworkOPs, RPCHelpers, AccountTx*
                              //      Tx
JSS(validator_list_expires);  // out: NetworkOps, ValidatorList
JSS(validator_list);          // out: NetworkOps, ValidatorList
JSS(validators);
JSS(validated_hash);            // out: NetworkOPs
JSS(validated_ledger);          // out: NetworkOPs
JSS(validated_ledger_index);    // out: SubmitTransaction
JSS(validated_ledgers);         // out: NetworkOPs
JSS(validation_key);            // out: ValidationCreate, ValidationSeed
JSS(validation_private_key);    // out: ValidationCreate
JSS(validation_public_key);     // out: ValidationCreate, ValidationSeed
JSS(validation_quorum);         // out: NetworkOPs
JSS(validation_seed);           // out: ValidationCreate, ValidationSeed
JSS(validations);               // out: AmendmentTableImpl
JSS(validator_list_threshold);  // out: ValidatorList
JSS(validator_sites);           // out: ValidatorSites
JSS(value);                     // out: STAmount
JSS(vault_id);                  // in: VaultInfo
JSS(version);                   // out: RPCVersion
JSS(vetoed);                    // out: AmendmentTableImpl
JSS(volume_a);                  // out: BookChanges
JSS(volume_b);                  // out: BookChanges
JSS(vote);                      // in: Feature
JSS(vote_slots);                // out: amm_info
JSS(vote_weight);               // out: amm_info
JSS(warning);                   // rpc:
JSS(warnings);                  // out: server_info, server_state
JSS(workers);
JSS(write_load);              // out: GetCounts
// clang-format on
JSS(xrp_balance);

#pragma push_macro("TRANSACTION")
#undef TRANSACTION

#define TRANSACTION(tag, value, name, delegatable, fields) JSS(name);

#include <xrpl/protocol/detail/transactions.macro>

#undef TRANSACTION
#pragma pop_macro("TRANSACTION")

#pragma push_macro("LEDGER_ENTRY")
#undef LEDGER_ENTRY
#pragma push_macro("LEDGER_ENTRY_DUPLICATE")
#undef LEDGER_ENTRY_DUPLICATE

#define LEDGER_ENTRY(tag, value, name, rpcName, fields) \
    JSS(name);                                          \
    JSS(rpcName);

#define LEDGER_ENTRY_DUPLICATE(tag, value, name, rpcName, fields) JSS(rpcName);

#include <xrpl/protocol/detail/ledger_entries.macro>

#undef LEDGER_ENTRY
#pragma pop_macro("LEDGER_ENTRY")
#undef LEDGER_ENTRY_DUPLICATE
#pragma pop_macro("LEDGER_ENTRY_DUPLICATE")

#undef JSS

}  // namespace jss
}  // namespace ripple

#endif
