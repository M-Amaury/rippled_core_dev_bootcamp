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

#ifndef RIPPLE_PROTOCOL_STLEDGERENTRY_H_INCLUDED
#define RIPPLE_PROTOCOL_STLEDGERENTRY_H_INCLUDED

#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/STObject.h>

namespace ripple {

class Rules;
class Invariants_test;

class STLedgerEntry final : public STObject, public CountedObject<STLedgerEntry>
{
    uint256 key_;
    LedgerEntryType type_;

public:
    using pointer = std::shared_ptr<STLedgerEntry>;
    using ref = std::shared_ptr<STLedgerEntry> const&;

    /** Create an empty object with the given key and type. */
    explicit STLedgerEntry(Keylet const& k);
    STLedgerEntry(LedgerEntryType type, uint256 const& key);
    STLedgerEntry(SerialIter& sit, uint256 const& index);
    STLedgerEntry(SerialIter&& sit, uint256 const& index);
    STLedgerEntry(STObject const& object, uint256 const& index);

    SerializedTypeID
    getSType() const override;

    std::string
    getFullText() const override;

    std::string
    getText() const override;

    Json::Value
    getJson(JsonOptions options) const override;

    /** Returns the 'key' (or 'index') of this item.
        The key identifies this entry's position in
        the SHAMap associative container.
    */
    uint256 const&
    key() const;

    LedgerEntryType
    getType() const;

    // is this a ledger entry that can be threaded
    bool
    isThreadedType(Rules const& rules) const;

    bool
    thread(
        uint256 const& txID,
        std::uint32_t ledgerSeq,
        uint256& prevTxID,
        std::uint32_t& prevLedgerID);

private:
    /*  Make STObject comply with the template for this SLE type
        Can throw
    */
    void
    setSLEType();

    friend Invariants_test;  // this test wants access to the private type_

    STBase*
    copy(std::size_t n, void* buf) const override;
    STBase*
    move(std::size_t n, void* buf) override;

    friend class detail::STVar;
};

using SLE = STLedgerEntry;

inline STLedgerEntry::STLedgerEntry(LedgerEntryType type, uint256 const& key)
    : STLedgerEntry(Keylet(type, key))
{
}

inline STLedgerEntry::STLedgerEntry(SerialIter&& sit, uint256 const& index)
    : STLedgerEntry(sit, index)
{
}

/** Returns the 'key' (or 'index') of this item.
    The key identifies this entry's position in
    the SHAMap associative container.
*/
inline uint256 const&
STLedgerEntry::key() const
{
    return key_;
}

inline LedgerEntryType
STLedgerEntry::getType() const
{
    return type_;
}

}  // namespace ripple

#endif
