//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2023 Ripple Labs Inc.

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

#include <xrpl/json/json_value.h>
#include <xrpl/protocol/NFTSyntheticSerializer.h>
#include <xrpl/protocol/NFTokenID.h>
#include <xrpl/protocol/NFTokenOfferID.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TxMeta.h>
#include <xrpl/protocol/jss.h>

#include <memory>

namespace ripple {
namespace RPC {

void
insertNFTSyntheticInJson(
    Json::Value& response,
    std::shared_ptr<STTx const> const& transaction,
    TxMeta const& transactionMeta)
{
    insertNFTokenID(response[jss::meta], transaction, transactionMeta);
    insertNFTokenOfferID(response[jss::meta], transaction, transactionMeta);
}

}  // namespace RPC
}  // namespace ripple
