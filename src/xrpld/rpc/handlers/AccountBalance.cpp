//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2024 Ripple Labs Inc.

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

#include <xrpld/app/main/Application.h>
#include <xrpld/ledger/ReadView.h>
#include <xrpld/rpc/Context.h>
#include <xrpld/rpc/detail/RPCHelpers.h>
#include <xrpld/app/ledger/LedgerMaster.h>

#include <xrpl/json/json_value.h>
#include <xrpl/protocol/ErrorCodes.h>
#include <xrpl/protocol/jss.h>
#include <xrpl/protocol/STAmount.h>

namespace ripple {

// {
//   command: "account_balance",
//   account: "rAccount...",
//   ledger_index: "validated",        // optional
//   include_reserves: true,          // optional, default false
//   include_trustlines: true         // optional, default false
// }

Json::Value
doAccountBalance(RPC::JsonContext& context)
{
    auto const& params = context.params;

    //Check param
    if(!params.isMember(jss::account)){
        return RPC::missing_field_error(jss::account);
    }

    if(!params[jss::account].isString()){
        return RPC::invalid_field_error(jss::account);
    }

    //Get ledger
    std::shared_ptr<ReadView const> ledger;
    auto result = RPC::lookupLedger(ledger, context);
    if(!ledger){        
        return result;
    }
    
    //Validate account
    auto id = parseBase58<AccountID>(params[jss::account].asString());
    if(!id){
        RPC::inject_error(rpcACT_MALFORMED, result);
        return result;
    }
    auto accountID = std::move(id.value());

    //Get account
    auto const sleAccount = ledger->read(keylet::account(accountID));
    if(!sleAccount){
        result[jss::account] = params[jss::account].asString();
        RPC::inject_error(rpcACT_NOT_FOUND, result);
        return result;
    }

    //Construct result
    result[jss::account] = params[jss::account].asString();
    result[jss::ledger_index] = context.ledgerMaster.getValidLedgerIndex();
    result[jss::validated] = context.ledgerMaster.isValidated(*ledger);

    //Get balance
    auto const balance = sleAccount->getFieldAmount(sfBalance);
    result[jss::xrp_balance] = balance.getText();

    bool includeReserves = params.isMember(jss::include_reserves) && params[jss::include_reserves].asBool();

    if(includeReserves){
        auto const reserve = ledger->fees().accountReserve(sleAccount->getFieldU32(sfOwnerCount));
        auto const baseReserve = ledger->fees().base;

        result[jss::reserves] = Json::objectValue;
        result[jss::reserves][jss::base_reserve] = to_string(baseReserve);
        result[jss::reserves][jss::owner_reserve] = to_string(reserve - baseReserve);
        result[jss::reserves][jss::total_reserve] = to_string(reserve);

        auto const availabe = balance > reserve ? balance - reserve : STAmount{0};
        result[jss::available] = availabe.getText();
    }
    return result;
}

}  // namespace ripple