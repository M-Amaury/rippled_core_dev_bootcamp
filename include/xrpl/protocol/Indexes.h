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

#ifndef RIPPLE_PROTOCOL_INDEXES_H_INCLUDED
#define RIPPLE_PROTOCOL_INDEXES_H_INCLUDED

#include <xrpl/basics/base_uint.h>
#include <xrpl/protocol/Book.h>
#include <xrpl/protocol/Keylet.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/PublicKey.h>
#include <xrpl/protocol/STXChainBridge.h>
#include <xrpl/protocol/Serializer.h>
#include <xrpl/protocol/UintTypes.h>
#include <xrpl/protocol/jss.h>

#include <cstdint>

namespace ripple {

class SeqProxy;
/** Keylet computation funclets.

    Entries in the ledger are located using 256-bit locators. The locators are
    calculated using a wide range of parameters specific to the entry whose
    locator we are calculating (e.g. an account's locator is derived from the
    account's address, whereas the locator for an offer is derived from the
    account and the offer sequence.)

    To enhance type safety during lookup and make the code more robust, we use
    keylets, which contain not only the locator of the object but also the type
    of the object being referenced.

    These functions each return a type-specific keylet.
*/
namespace keylet {

/** AccountID root */
Keylet
account(AccountID const& id) noexcept;

/** The index of the amendment table */
Keylet const&
amendments() noexcept;

/** Any item that can be in an owner dir. */
Keylet
child(uint256 const& key) noexcept;

/** The index of the "short" skip list

    The "short" skip list is a node (at a fixed index) that holds the hashes
    of ledgers since the last flag ledger. It will contain, at most, 256 hashes.
*/
Keylet const&
skip() noexcept;

/** The index of the long skip for a particular ledger range.

    The "long" skip list is a node that holds the hashes of (up to) 256 flag
    ledgers.

    It can be used to efficiently skip back to any ledger using only two hops:
    the first hop gets the "long" skip list for the ledger it wants to retrieve
    and uses it to get the hash of the flag ledger whose short skip list will
    contain the hash of the requested ledger.
*/
Keylet
skip(LedgerIndex ledger) noexcept;

/** The (fixed) index of the object containing the ledger fees. */
Keylet const&
fees() noexcept;

/** The (fixed) index of the object containing the ledger negativeUNL. */
Keylet const&
negativeUNL() noexcept;

/** The beginning of an order book */
struct book_t
{
    explicit book_t() = default;

    Keylet
    operator()(Book const& b) const;
};
static book_t const book{};

/** The index of a trust line for a given currency

    Note that a trustline is *shared* between two accounts (commonly referred
    to as the issuer and the holder); if Alice sets up a trust line to Bob for
    BTC, and Bob trusts Alice for BTC, here is only a single BTC trust line
    between them.
*/
/** @{ */
Keylet
line(
    AccountID const& id0,
    AccountID const& id1,
    Currency const& currency) noexcept;

inline Keylet
line(AccountID const& id, Issue const& issue) noexcept
{
    return line(id, issue.account, issue.currency);
}
/** @} */

/** An offer from an account */
/** @{ */
Keylet
offer(AccountID const& id, std::uint32_t seq) noexcept;

inline Keylet
offer(uint256 const& key) noexcept
{
    return {ltOFFER, key};
}
/** @} */

/** The initial directory page for a specific quality */
Keylet
quality(Keylet const& k, std::uint64_t q) noexcept;

/** The directory for the next lower quality */
struct next_t
{
    explicit next_t() = default;

    Keylet
    operator()(Keylet const& k) const;
};
static next_t const next{};

/** A ticket belonging to an account */
struct ticket_t
{
    explicit ticket_t() = default;

    Keylet
    operator()(AccountID const& id, std::uint32_t ticketSeq) const;

    Keylet
    operator()(AccountID const& id, SeqProxy ticketSeq) const;

    Keylet
    operator()(uint256 const& key) const
    {
        return {ltTICKET, key};
    }
};
static ticket_t const ticket{};

/** A SignerList */
Keylet
signers(AccountID const& account) noexcept;

/** A Check */
/** @{ */
Keylet
check(AccountID const& id, std::uint32_t seq) noexcept;

inline Keylet
check(uint256 const& key) noexcept
{
    return {ltCHECK, key};
}
/** @} */

/** A DepositPreauth */
/** @{ */
Keylet
depositPreauth(AccountID const& owner, AccountID const& preauthorized) noexcept;

Keylet
depositPreauth(
    AccountID const& owner,
    std::set<std::pair<AccountID, Slice>> const& authCreds) noexcept;

inline Keylet
depositPreauth(uint256 const& key) noexcept
{
    return {ltDEPOSIT_PREAUTH, key};
}
/** @} */

//------------------------------------------------------------------------------

/** Any ledger entry */
Keylet
unchecked(uint256 const& key) noexcept;

/** The root page of an account's directory */
Keylet
ownerDir(AccountID const& id) noexcept;

/** A page in a directory */
/** @{ */
Keylet
page(uint256 const& root, std::uint64_t index = 0) noexcept;

inline Keylet
page(Keylet const& root, std::uint64_t index = 0) noexcept
{
    XRPL_ASSERT(
        root.type == ltDIR_NODE, "ripple::keylet::page : valid root type");
    return page(root.key, index);
}
/** @} */

/** An escrow entry */
Keylet
escrow(AccountID const& src, std::uint32_t seq) noexcept;

/** A PaymentChannel */
Keylet
payChan(AccountID const& src, AccountID const& dst, std::uint32_t seq) noexcept;

/** NFT page keylets

    Unlike objects whose ledger identifiers are produced by hashing data,
    NFT page identifiers are composite identifiers, consisting of the owner's
    160-bit AccountID, followed by a 96-bit value that determines which NFT
    tokens are candidates for that page.
 */
/** @{ */
/** A keylet for the owner's first possible NFT page. */
Keylet
nftpage_min(AccountID const& owner);

/** A keylet for the owner's last possible NFT page. */
Keylet
nftpage_max(AccountID const& owner);

Keylet
nftpage(Keylet const& k, uint256 const& token);
/** @} */

/** An offer from an account to buy or sell an NFT */
Keylet
nftoffer(AccountID const& owner, std::uint32_t seq);

inline Keylet
nftoffer(uint256 const& offer)
{
    return {ltNFTOKEN_OFFER, offer};
}

/** The directory of buy offers for the specified NFT */
Keylet
nft_buys(uint256 const& id) noexcept;

/** The directory of sell offers for the specified NFT */
Keylet
nft_sells(uint256 const& id) noexcept;

/** AMM entry */
Keylet
amm(Asset const& issue1, Asset const& issue2) noexcept;

Keylet
amm(uint256 const& amm) noexcept;

/** A keylet for Delegate object */
Keylet
delegate(AccountID const& account, AccountID const& authorizedAccount) noexcept;

Keylet
bridge(STXChainBridge const& bridge, STXChainBridge::ChainType chainType);

Keylet
xChainClaimID(STXChainBridge const& bridge, std::uint64_t seq);

Keylet
xChainCreateAccountClaimID(STXChainBridge const& bridge, std::uint64_t seq);

Keylet
did(AccountID const& account) noexcept;

Keylet
oracle(AccountID const& account, std::uint32_t const& documentID) noexcept;

Keylet
credential(
    AccountID const& subject,
    AccountID const& issuer,
    Slice const& credType) noexcept;

inline Keylet
credential(uint256 const& key) noexcept
{
    return {ltCREDENTIAL, key};
}

Keylet
mptIssuance(std::uint32_t seq, AccountID const& issuer) noexcept;

Keylet
mptIssuance(MPTID const& issuanceID) noexcept;

inline Keylet
mptIssuance(uint256 const& issuanceKey)
{
    return {ltMPTOKEN_ISSUANCE, issuanceKey};
}

Keylet
mptoken(MPTID const& issuanceID, AccountID const& holder) noexcept;

inline Keylet
mptoken(uint256 const& mptokenKey)
{
    return {ltMPTOKEN, mptokenKey};
}

Keylet
mptoken(uint256 const& issuanceKey, AccountID const& holder) noexcept;

Keylet
vault(AccountID const& owner, std::uint32_t seq) noexcept;

inline Keylet
vault(uint256 const& vaultKey)
{
    return {ltVAULT, vaultKey};
}

Keylet
permissionedDomain(AccountID const& account, std::uint32_t seq) noexcept;

Keylet
permissionedDomain(uint256 const& domainID) noexcept;

Keylet
recurring_payment(AccountID const& account, AccountID const& destination, std::uint32_t seq) noexcept;
}  // namespace keylet



// Everything below is deprecated and should be removed in favor of keylets:

uint256
getBookBase(Book const& book);

uint256
getQualityNext(uint256 const& uBase);

// VFALCO This name could be better
std::uint64_t
getQuality(uint256 const& uBase);

uint256
getTicketIndex(AccountID const& account, std::uint32_t uSequence);

uint256
getTicketIndex(AccountID const& account, SeqProxy ticketSeq);

template <class... keyletParams>
struct keyletDesc
{
    std::function<Keylet(keyletParams...)> function;
    Json::StaticString expectedLEName;
    bool includeInTests;
};

// This list should include all of the keylet functions that take a single
// AccountID parameter.
std::array<keyletDesc<AccountID const&>, 6> const directAccountKeylets{
    {{&keylet::account, jss::AccountRoot, false},
     {&keylet::ownerDir, jss::DirectoryNode, true},
     {&keylet::signers, jss::SignerList, true},
     // It's normally impossible to create an item at nftpage_min, but
     // test it anyway, since the invariant checks for it.
     {&keylet::nftpage_min, jss::NFTokenPage, true},
     {&keylet::nftpage_max, jss::NFTokenPage, true},
     {&keylet::did, jss::DID, true}}};

MPTID
makeMptID(std::uint32_t sequence, AccountID const& account);

}  // namespace ripple

#endif
