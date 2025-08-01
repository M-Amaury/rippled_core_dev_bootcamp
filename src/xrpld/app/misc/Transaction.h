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

#ifndef RIPPLE_APP_MISC_TRANSACTION_H_INCLUDED
#define RIPPLE_APP_MISC_TRANSACTION_H_INCLUDED

#include <xrpl/basics/RangeSet.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/protocol/ErrorCodes.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/STBase.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxMeta.h>

#include <optional>
#include <variant>

namespace ripple {

//
// Transactions should be constructed in JSON with. Use STObject::parseJson to
// obtain a binary version.
//

class Application;
class Database;
class Rules;

enum TransStatus {
    NEW = 0,         // just received / generated
    INVALID = 1,     // no valid signature, insufficient funds
    INCLUDED = 2,    // added to the current ledger
    CONFLICTED = 3,  // losing to a conflicting transaction
    COMMITTED = 4,   // known to be in a ledger
    HELD = 5,        // not valid now, maybe later
    REMOVED = 6,     // taken out of a ledger
    OBSOLETE = 7,    // a compatible transaction has taken precedence
    INCOMPLETE = 8   // needs more signatures
};

enum class TxSearched { all, some, unknown };

// This class is for constructing and examining transactions.
// Transactions are static so manipulation functions are unnecessary.
class Transaction : public std::enable_shared_from_this<Transaction>,
                    public CountedObject<Transaction>
{
public:
    using pointer = std::shared_ptr<Transaction>;
    using ref = pointer const&;

    Transaction(
        std::shared_ptr<STTx const> const&,
        std::string&,
        Application&) noexcept;

    // The two boost::optional parameters are because SOCI requires
    // boost::optional (not std::optional) parameters.
    static Transaction::pointer
    transactionFromSQL(
        boost::optional<std::uint64_t> const& ledgerSeq,
        boost::optional<std::string> const& status,
        Blob const& rawTxn,
        Application& app);

    // The boost::optional parameter is because SOCI requires
    // boost::optional (not std::optional) parameters.
    static TransStatus
    sqlTransactionStatus(boost::optional<std::string> const& status);

    std::shared_ptr<STTx const> const&
    getSTransaction()
    {
        return mTransaction;
    }

    uint256 const&
    getID() const
    {
        return mTransactionID;
    }

    LedgerIndex
    getLedger() const
    {
        return mLedgerIndex;
    }

    bool
    isValidated() const
    {
        return mLedgerIndex != 0;
    }

    TransStatus
    getStatus() const
    {
        return mStatus;
    }

    TER
    getResult()
    {
        return mResult;
    }

    void
    setResult(TER terResult)
    {
        mResult = terResult;
    }

    void
    setStatus(
        TransStatus status,
        std::uint32_t ledgerSeq,
        std::optional<uint32_t> transactionSeq = std::nullopt,
        std::optional<uint32_t> networkID = std::nullopt);

    void
    setStatus(TransStatus status)
    {
        mStatus = status;
    }

    void
    setLedger(LedgerIndex ledger)
    {
        mLedgerIndex = ledger;
    }

    /**
     * Set this flag once added to a batch.
     */
    void
    setApplying()
    {
        // Note that all access to mApplying are made by NetworkOPsImp, and must
        // be done under that class's lock.
        mApplying = true;
    }

    /**
     * Detect if transaction is being batched.
     *
     * @return Whether transaction is being applied within a batch.
     */
    bool
    getApplying()
    {
        // Note that all access to mApplying are made by NetworkOPsImp, and must
        // be done under that class's lock.
        return mApplying;
    }

    /**
     * Indicate that transaction application has been attempted.
     */
    void
    clearApplying()
    {
        // Note that all access to mApplying are made by NetworkOPsImp, and must
        // be done under that class's lock.
        mApplying = false;
    }

    struct SubmitResult
    {
        /**
         * @brief clear Clear all states
         */
        void
        clear()
        {
            applied = false;
            broadcast = false;
            queued = false;
            kept = false;
        }

        /**
         * @brief any Get true of any state is true
         * @return True if any state if true
         */
        bool
        any() const
        {
            return applied || broadcast || queued || kept;
        }

        bool applied = false;
        bool broadcast = false;
        bool queued = false;
        bool kept = false;
    };

    /**
     * @brief getSubmitResult Return submit result
     * @return SubmitResult struct
     */
    SubmitResult
    getSubmitResult() const
    {
        return submitResult_;
    }

    /**
     * @brief clearSubmitResult Clear all flags in SubmitResult
     */
    void
    clearSubmitResult()
    {
        submitResult_.clear();
    }

    /**
     * @brief setApplied Set this flag once was applied to open ledger
     */
    void
    setApplied()
    {
        submitResult_.applied = true;
    }

    /**
     * @brief setQueued Set this flag once was put into heldtxns queue
     */
    void
    setQueued()
    {
        submitResult_.queued = true;
    }

    /**
     * @brief setBroadcast Set this flag once was broadcasted via network
     */
    void
    setBroadcast()
    {
        submitResult_.broadcast = true;
    }

    /**
     * @brief setKept Set this flag once was put to localtxns queue
     */
    void
    setKept()
    {
        submitResult_.kept = true;
    }

    struct CurrentLedgerState
    {
        CurrentLedgerState() = delete;

        CurrentLedgerState(
            LedgerIndex li,
            XRPAmount fee,
            std::uint32_t accSeqNext,
            std::uint32_t accSeqAvail)
            : validatedLedger{li}
            , minFeeRequired{fee}
            , accountSeqNext{accSeqNext}
            , accountSeqAvail{accSeqAvail}
        {
        }

        LedgerIndex validatedLedger;
        XRPAmount minFeeRequired;
        std::uint32_t accountSeqNext;
        std::uint32_t accountSeqAvail;
    };

    /**
     * @brief getCurrentLedgerState Get current ledger state of transaction
     * @return Current ledger state
     */
    std::optional<CurrentLedgerState>
    getCurrentLedgerState() const
    {
        return currentLedgerState_;
    }

    /**
     * @brief setCurrentLedgerState Set current ledger state of transaction
     * @param validatedLedger Number of last validated ledger
     * @param fee minimum Fee required for the transaction
     * @param accountSeq First valid account sequence in current ledger
     * @param availableSeq First available sequence for the transaction
     */
    void
    setCurrentLedgerState(
        LedgerIndex validatedLedger,
        XRPAmount fee,
        std::uint32_t accountSeq,
        std::uint32_t availableSeq)
    {
        currentLedgerState_.emplace(
            validatedLedger, fee, accountSeq, availableSeq);
    }

    Json::Value
    getJson(JsonOptions options, bool binary = false) const;

    // Information used to locate a transaction.
    // Contains a nodestore hash and ledger sequence pair if the transaction was
    // found. Otherwise, contains the range of ledgers present in the database
    // at the time of search.
    struct Locator
    {
        std::variant<std::pair<uint256, uint32_t>, ClosedInterval<uint32_t>>
            locator;

        // @return true if transaction was found, false otherwise
        //
        // Call this function first to determine the type of the contained info.
        // Calling the wrong getter function will throw an exception.
        // See documentation for the getter functions for more details
        bool
        isFound()
        {
            return std::holds_alternative<std::pair<uint256, uint32_t>>(
                locator);
        }

        // @return key used to find transaction in nodestore
        //
        // Throws if isFound() returns false
        uint256 const&
        getNodestoreHash()
        {
            return std::get<std::pair<uint256, uint32_t>>(locator).first;
        }

        // @return sequence of ledger containing the transaction
        //
        // Throws is isFound() returns false
        uint32_t
        getLedgerSequence()
        {
            return std::get<std::pair<uint256, uint32_t>>(locator).second;
        }

        // @return range of ledgers searched
        //
        // Throws if isFound() returns true
        ClosedInterval<uint32_t> const&
        getLedgerRangeSearched()
        {
            return std::get<ClosedInterval<uint32_t>>(locator);
        }
    };

    static Locator
    locate(uint256 const& id, Application& app);

    static std::variant<
        std::pair<std::shared_ptr<Transaction>, std::shared_ptr<TxMeta>>,
        TxSearched>
    load(uint256 const& id, Application& app, error_code_i& ec);

    static std::variant<
        std::pair<std::shared_ptr<Transaction>, std::shared_ptr<TxMeta>>,
        TxSearched>
    load(
        uint256 const& id,
        Application& app,
        ClosedInterval<uint32_t> const& range,
        error_code_i& ec);

private:
    static std::variant<
        std::pair<std::shared_ptr<Transaction>, std::shared_ptr<TxMeta>>,
        TxSearched>
    load(
        uint256 const& id,
        Application& app,
        std::optional<ClosedInterval<uint32_t>> const& range,
        error_code_i& ec);

    uint256 mTransactionID;

    LedgerIndex mLedgerIndex = 0;
    std::optional<uint32_t> mTxnSeq;
    std::optional<uint32_t> mNetworkID;
    TransStatus mStatus = INVALID;
    TER mResult = temUNCERTAIN;
    /* Note that all access to mApplying are made by NetworkOPsImp,
        and must be done under that class's lock. This avoids the overhead of
        taking a separate lock, and the consequences of a race condition are
        nearly-zero.

        1. If there is a race condition, and getApplying returns false when it
            should be true, the transaction will be processed again. Not that
            big a deal if it's a rare one-off. Most of the time, it'll get
            tefALREADY or tefPAST_SEQ.
        2. On the flip side, if it returns true, when it should be false, then
            the transaction must have been attempted recently, so no big deal if
            it doesn't immediately get tried right away.
        3. If there's a race between setApplying and clearApplying, and the
            flag ends up set, then a batch is about to try to process the
            transaction and will call clearApplying later. If it ends up
            cleared, then it might get attempted again later as is the case with
            item 1.
    */
    bool mApplying = false;

    /** different ways for transaction to be accepted */
    SubmitResult submitResult_;

    std::optional<CurrentLedgerState> currentLedgerState_;

    std::shared_ptr<STTx const> mTransaction;
    Application& mApp;
    beast::Journal j_;
};

}  // namespace ripple

#endif
