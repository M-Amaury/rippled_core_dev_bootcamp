//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2021 Ripple Labs Inc.

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

#include <test/jtx/token.h>
#include <xrpl/protocol/jss.h>
#include <xrpl/protocol/TxFormats.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/basics/strHex.h>

namespace ripple {
namespace test {
namespace jtx {
namespace token {

void
xferFee::operator()(Env&, JTx& jtx) const
{
    jtx.jv[sfTransferFee.jsonName] = xferFee_;
}

void
issuer::operator()(Env&, JTx& jtx) const
{
    jtx.jv[jss::Issuer] = issuer_;
}

void
uri::operator()(Env&, JTx& jtx) const
{
    jtx.jv[jss::URI] = uri_;
}

void
amount::operator()(Env&, JTx& jtx) const
{
    jtx.jv[jss::Amount] = amount_.getJson(JsonOptions::none);
}

void
owner::operator()(Env&, JTx& jtx) const
{
    jtx.jv[jss::Owner] = owner_;
}

void
expiration::operator()(Env&, JTx& jtx) const
{
    jtx.jv[jss::expiration] = expires_;
}

void
destination::operator()(Env&, JTx& jtx) const
{
    jtx.jv[jss::Destination] = dest_;
}

void
rootIndex::operator()(Env&, JTx& jtx) const
{
    jtx.jv[sfNFTokenOffers.jsonName] = Json::arrayValue;
    Json::Value offer;
    offer[sfRootIndex.jsonName] = rootIndex_;
    jtx.jv[sfNFTokenOffers.jsonName].append(offer);
}

void
brokerFee::operator()(Env&, JTx& jtx) const
{
    jtx.jv[sfNFTokenBrokerFee.jsonName] = brokerFee_.getJson(JsonOptions::none);
}

Json::Value
mint(jtx::Account const& account, std::uint32_t tokenTaxon)
{
    Json::Value jv;
    jv[jss::TransactionType] = jss::NFTokenMint;
    jv[jss::Account] = account.human();
    jv[sfNFTokenTaxon.jsonName] = tokenTaxon;
    return jv;
}

uint256
getNextID(
    jtx::Env const& env,
    jtx::Account const& account,
    std::uint32_t nftokenTaxon,
    std::uint16_t flags,
    std::uint16_t xferFee)
{
    // Simplified implementation - return a deterministic but unique value
    auto const accountID = account.id();
    auto const root = env.le(account);
    std::uint32_t seq = root ? (*root)[sfSequence] : 0;
    
    // Create a simple NFTokenID based on account, sequence, taxon and flags
    uint256 result;
    // Basic construction similar to existing NFToken logic
    std::memcpy(result.data(), accountID.data(), 20); // Account ID
    result.data()[20] = (seq >> 24) & 0xFF;
    result.data()[21] = (seq >> 16) & 0xFF;
    result.data()[22] = (seq >> 8) & 0xFF;
    result.data()[23] = seq & 0xFF;
    result.data()[24] = (nftokenTaxon >> 24) & 0xFF;
    result.data()[25] = (nftokenTaxon >> 16) & 0xFF;
    result.data()[26] = (nftokenTaxon >> 8) & 0xFF;
    result.data()[27] = nftokenTaxon & 0xFF;
    result.data()[28] = (flags >> 8) & 0xFF;
    result.data()[29] = flags & 0xFF;
    result.data()[30] = (xferFee >> 8) & 0xFF;
    result.data()[31] = xferFee & 0xFF;
    
    return result;
}

uint256
getID(
    jtx::Env const& env,
    jtx::Account const& account,
    std::uint32_t tokenTaxon,
    std::uint32_t nftSeq,
    std::uint16_t flags,
    std::uint16_t xferFee)
{
    // Similar to getNextID but use provided nftSeq
    auto const accountID = account.id();
    
    uint256 result;
    std::memcpy(result.data(), accountID.data(), 20); // Account ID
    result.data()[20] = (nftSeq >> 24) & 0xFF;
    result.data()[21] = (nftSeq >> 16) & 0xFF;
    result.data()[22] = (nftSeq >> 8) & 0xFF;
    result.data()[23] = nftSeq & 0xFF;
    result.data()[24] = (tokenTaxon >> 24) & 0xFF;
    result.data()[25] = (tokenTaxon >> 16) & 0xFF;
    result.data()[26] = (tokenTaxon >> 8) & 0xFF;
    result.data()[27] = tokenTaxon & 0xFF;
    result.data()[28] = (flags >> 8) & 0xFF;
    result.data()[29] = flags & 0xFF;
    result.data()[30] = (xferFee >> 8) & 0xFF;
    result.data()[31] = xferFee & 0xFF;
    
    return result;
}

Json::Value
burn(jtx::Account const& account, uint256 const& nftokenID)
{
    Json::Value jv;
    jv[jss::TransactionType] = jss::NFTokenBurn;
    jv[jss::Account] = account.human();
    jv[sfNFTokenID.jsonName] = to_string(nftokenID);
    return jv;
}

Json::Value
createOffer(
    jtx::Account const& account,
    uint256 const& nftokenID,
    STAmount const& amount)
{
    Json::Value jv;
    jv[jss::TransactionType] = jss::NFTokenCreateOffer;
    jv[jss::Account] = account.human();
    jv[sfNFTokenID.jsonName] = to_string(nftokenID);
    jv[jss::Amount] = amount.getJson(JsonOptions::none);
    return jv;
}

Json::Value
cancelOffer(
    jtx::Account const& account,
    std::initializer_list<uint256> const& nftokenOffers)
{
    Json::Value jv;
    jv[jss::TransactionType] = jss::NFTokenCancelOffer;
    jv[jss::Account] = account.human();
    
    Json::Value offers(Json::arrayValue);
    for (auto const& offer : nftokenOffers)
    {
        offers.append(to_string(offer));
    }
    jv[sfNFTokenOffers.jsonName] = offers;
    return jv;
}

Json::Value
cancelOffer(
    jtx::Account const& account,
    std::vector<uint256> const& nftokenOffers)
{
    Json::Value jv;
    jv[jss::TransactionType] = jss::NFTokenCancelOffer;
    jv[jss::Account] = account.human();
    
    Json::Value offers(Json::arrayValue);
    for (auto const& offer : nftokenOffers)
    {
        offers.append(to_string(offer));
    }
    jv[sfNFTokenOffers.jsonName] = offers;
    return jv;
}

Json::Value
acceptBuyOffer(jtx::Account const& account, uint256 const& offerIndex)
{
    Json::Value jv;
    jv[jss::TransactionType] = jss::NFTokenAcceptOffer;
    jv[jss::Account] = account.human();
    jv[sfNFTokenBuyOffer.jsonName] = to_string(offerIndex);
    return jv;
}

Json::Value
acceptSellOffer(jtx::Account const& account, uint256 const& offerIndex)
{
    Json::Value jv;
    jv[jss::TransactionType] = jss::NFTokenAcceptOffer;
    jv[jss::Account] = account.human();
    jv[sfNFTokenSellOffer.jsonName] = to_string(offerIndex);
    return jv;
}

Json::Value
brokerOffers(
    jtx::Account const& account,
    uint256 const& buyOfferIndex,
    uint256 const& sellOfferIndex)
{
    Json::Value jv;
    jv[jss::TransactionType] = jss::NFTokenAcceptOffer;
    jv[jss::Account] = account.human();
    jv[sfNFTokenBuyOffer.jsonName] = to_string(buyOfferIndex);
    jv[sfNFTokenSellOffer.jsonName] = to_string(sellOfferIndex);
    return jv;
}

Json::Value
setMinter(jtx::Account const& account, jtx::Account const& minter)
{
    Json::Value jv;
    jv[jss::TransactionType] = jss::AccountSet;
    jv[jss::Account] = account.human();
    jv[sfNFTokenMinter.jsonName] = minter.human();
    return jv;
}

Json::Value
clearMinter(jtx::Account const& account)
{
    Json::Value jv;
    jv[jss::TransactionType] = jss::AccountSet;
    jv[jss::Account] = account.human();
    jv[sfNFTokenMinter.jsonName] = "";
    return jv;
}

Json::Value
modify(jtx::Account const& account, uint256 const& nftokenID)
{
    Json::Value jv;
    jv[jss::TransactionType] = jss::NFTokenModify;
    jv[jss::Account] = account.human();
    jv[sfNFTokenID.jsonName] = to_string(nftokenID);
    return jv;
}

}  // namespace token
}  // namespace jtx
}  // namespace test
}  // namespace ripple