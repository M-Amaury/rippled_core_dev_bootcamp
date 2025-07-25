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

#ifndef RIPPLE_APP_TX_TRANSACTIONMETA_H_INCLUDED
#define RIPPLE_APP_TX_TRANSACTIONMETA_H_INCLUDED

#include <xrpl/beast/utility/Journal.h>
#include <xrpl/protocol/STArray.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/TER.h>

#include <boost/container/flat_set.hpp>

#include <optional>

namespace ripple {

class TxMeta
{
private:
    struct CtorHelper
    {
        explicit CtorHelper() = default;
    };
    template <class T>
    TxMeta(
        uint256 const& txID,
        std::uint32_t ledger,
        T const& data,
        CtorHelper);

public:
    TxMeta(
        uint256 const& transactionID,
        std::uint32_t ledger,
        std::optional<uint256> parentBatchId = std::nullopt);
    TxMeta(uint256 const& txID, std::uint32_t ledger, Blob const&);
    TxMeta(uint256 const& txID, std::uint32_t ledger, std::string const&);
    TxMeta(uint256 const& txID, std::uint32_t ledger, STObject const&);

    uint256 const&
    getTxID() const
    {
        return mTransactionID;
    }
    std::uint32_t
    getLgrSeq() const
    {
        return mLedger;
    }
    int
    getResult() const
    {
        return mResult;
    }
    TER
    getResultTER() const
    {
        return TER::fromInt(mResult);
    }
    std::uint32_t
    getIndex() const
    {
        return mIndex;
    }

    void
    setAffectedNode(uint256 const&, SField const& type, std::uint16_t nodeType);
    STObject&
    getAffectedNode(SLE::ref node, SField const& type);  // create if needed
    STObject&
    getAffectedNode(uint256 const&);

    /** Return a list of accounts affected by this transaction */
    boost::container::flat_set<AccountID>
    getAffectedAccounts() const;

    Json::Value
    getJson(JsonOptions p) const
    {
        return getAsObject().getJson(p);
    }
    void
    addRaw(Serializer&, TER, std::uint32_t index);

    STObject
    getAsObject() const;
    STArray&
    getNodes()
    {
        return (mNodes);
    }
    STArray const&
    getNodes() const
    {
        return (mNodes);
    }

    void
    setDeliveredAmount(STAmount const& delivered)
    {
        mDelivered = delivered;
    }

    STAmount
    getDeliveredAmount() const
    {
        XRPL_ASSERT(
            hasDeliveredAmount(),
            "ripple::TxMeta::getDeliveredAmount : non-null delivered amount");
        return *mDelivered;
    }

    bool
    hasDeliveredAmount() const
    {
        return static_cast<bool>(mDelivered);
    }

    void
    setParentBatchId(uint256 const& parentBatchId)
    {
        mParentBatchId = parentBatchId;
    }

    uint256
    getParentBatchId() const
    {
        XRPL_ASSERT(
            hasParentBatchId(),
            "ripple::TxMeta::getParentBatchId : non-null batch id");
        return *mParentBatchId;
    }

    bool
    hasParentBatchId() const
    {
        return static_cast<bool>(mParentBatchId);
    }

private:
    uint256 mTransactionID;
    std::uint32_t mLedger;
    std::uint32_t mIndex;
    int mResult;

    std::optional<STAmount> mDelivered;
    std::optional<uint256> mParentBatchId;

    STArray mNodes;
};

}  // namespace ripple

#endif
