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

#ifndef RIPPLE_LEDGER_READVIEW_H_INCLUDED
#define RIPPLE_LEDGER_READVIEW_H_INCLUDED

#include <xrpld/ledger/detail/ReadViewFwdRange.h>

#include <xrpl/basics/chrono.h>
#include <xrpl/beast/hash/uhash.h>
#include <xrpl/protocol/Fees.h>
#include <xrpl/protocol/IOUAmount.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/LedgerHeader.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/Rules.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STTx.h>

#include <cstdint>
#include <optional>
#include <unordered_set>

namespace ripple {

//------------------------------------------------------------------------------

/** A view into a ledger.

    This interface provides read access to state
    and transaction items. There is no checkpointing
    or calculation of metadata.
*/
class ReadView
{
public:
    using tx_type =
        std::pair<std::shared_ptr<STTx const>, std::shared_ptr<STObject const>>;

    using key_type = uint256;

    using mapped_type = std::shared_ptr<SLE const>;

    struct sles_type : detail::ReadViewFwdRange<std::shared_ptr<SLE const>>
    {
        explicit sles_type(ReadView const& view);
        iterator
        begin() const;
        iterator
        end() const;
        iterator
        upper_bound(key_type const& key) const;
    };

    struct txs_type : detail::ReadViewFwdRange<tx_type>
    {
        explicit txs_type(ReadView const& view);
        bool
        empty() const;
        iterator
        begin() const;
        iterator
        end() const;
    };

    virtual ~ReadView() = default;

    ReadView&
    operator=(ReadView&& other) = delete;
    ReadView&
    operator=(ReadView const& other) = delete;

    ReadView() : sles(*this), txs(*this)
    {
    }

    ReadView(ReadView const& other) : sles(*this), txs(*this)
    {
    }

    ReadView(ReadView&& other) : sles(*this), txs(*this)
    {
    }

    /** Returns information about the ledger. */
    virtual LedgerInfo const&
    info() const = 0;

    /** Returns true if this reflects an open ledger. */
    virtual bool
    open() const = 0;

    /** Returns the close time of the previous ledger. */
    NetClock::time_point
    parentCloseTime() const
    {
        return info().parentCloseTime;
    }

    /** Returns the sequence number of the base ledger. */
    LedgerIndex
    seq() const
    {
        return info().seq;
    }

    /** Returns the fees for the base ledger. */
    virtual Fees const&
    fees() const = 0;

    /** Returns the tx processing rules. */
    virtual Rules const&
    rules() const = 0;

    /** Determine if a state item exists.

        @note This can be more efficient than calling read.

        @return `true` if a SLE is associated with the
                specified key.
    */
    virtual bool
    exists(Keylet const& k) const = 0;

    /** Return the key of the next state item.

        This returns the key of the first state item
        whose key is greater than the specified key. If
        no such key is present, std::nullopt is returned.

        If `last` is engaged, returns std::nullopt when
        the key returned would be outside the open
        interval (key, last).
    */
    virtual std::optional<key_type>
    succ(
        key_type const& key,
        std::optional<key_type> const& last = std::nullopt) const = 0;

    /** Return the state item associated with a key.

        Effects:
            If the key exists, gives the caller ownership
            of the non-modifiable corresponding SLE.

        @note While the returned SLE is `const` from the
              perspective of the caller, it can be changed
              by other callers through raw operations.

        @return `nullptr` if the key is not present or
                if the type does not match.
    */
    virtual std::shared_ptr<SLE const>
    read(Keylet const& k) const = 0;

    // Accounts in a payment are not allowed to use assets acquired during that
    // payment. The PaymentSandbox tracks the debits, credits, and owner count
    // changes that accounts make during a payment. `balanceHook` adjusts
    // balances so newly acquired assets are not counted toward the balance.
    // This is required to support PaymentSandbox.
    virtual STAmount
    balanceHook(
        AccountID const& account,
        AccountID const& issuer,
        STAmount const& amount) const
    {
        return amount;
    }

    // Accounts in a payment are not allowed to use assets acquired during that
    // payment. The PaymentSandbox tracks the debits, credits, and owner count
    // changes that accounts make during a payment. `ownerCountHook` adjusts the
    // ownerCount so it returns the max value of the ownerCount so far.
    // This is required to support PaymentSandbox.
    virtual std::uint32_t
    ownerCountHook(AccountID const& account, std::uint32_t count) const
    {
        return count;
    }

    // used by the implementation
    virtual std::unique_ptr<sles_type::iter_base>
    slesBegin() const = 0;

    // used by the implementation
    virtual std::unique_ptr<sles_type::iter_base>
    slesEnd() const = 0;

    // used by the implementation
    virtual std::unique_ptr<sles_type::iter_base>
    slesUpperBound(key_type const& key) const = 0;

    // used by the implementation
    virtual std::unique_ptr<txs_type::iter_base>
    txsBegin() const = 0;

    // used by the implementation
    virtual std::unique_ptr<txs_type::iter_base>
    txsEnd() const = 0;

    /** Returns `true` if a tx exists in the tx map.

        A tx exists in the map if it is part of the
        base ledger, or if it is a newly inserted tx.
    */
    virtual bool
    txExists(key_type const& key) const = 0;

    /** Read a transaction from the tx map.

        If the view represents an open ledger,
        the metadata object will be empty.

        @return A pair of nullptr if the
                key is not found in the tx map.
    */
    virtual tx_type
    txRead(key_type const& key) const = 0;

    //
    // Memberspaces
    //

    /** Iterable range of ledger state items.

        @note Visiting each state entry in the ledger can
              become quite expensive as the ledger grows.
    */
    sles_type sles;

    // The range of transactions
    txs_type txs;
};

//------------------------------------------------------------------------------

/** ReadView that associates keys with digests. */
class DigestAwareReadView : public ReadView
{
public:
    using digest_type = uint256;

    DigestAwareReadView() = default;
    DigestAwareReadView(DigestAwareReadView const&) = default;

    /** Return the digest associated with the key.

        @return std::nullopt if the item does not exist.
    */
    virtual std::optional<digest_type>
    digest(key_type const& key) const = 0;
};

//------------------------------------------------------------------------------

Rules
makeRulesGivenLedger(DigestAwareReadView const& ledger, Rules const& current);

Rules
makeRulesGivenLedger(
    DigestAwareReadView const& ledger,
    std::unordered_set<uint256, beast::uhash<>> const& presets);

}  // namespace ripple

#include <xrpld/ledger/detail/ReadViewFwdRange.ipp>

#endif
