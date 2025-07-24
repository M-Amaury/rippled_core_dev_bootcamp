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


#include <xrpld/app/ledger/Ledger.h>
#include <xrpld/app/tx/detail/RecurringPaymentSet.h>
#include <xrpld/ledger/ApplyView.h>
#include <xrpld/ledger/View.h>

#include <xrpl/basics/Log.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>

namespace ripple {

NotTEC
RecurringPaymentSet::preflight(PreflightContext const& ctx){
    if (auto const ret = preflight1(ctx); !isTesSuccess(ret)){
        return ret;
    }

    std::uint32_t const txFlags = ctx.tx.getFlags();
    if(txFlags & tfUniversalMask){
        JLOG(ctx.j.error()) << "RecurringPaymentSet: Invalid flags set";
        return temINVALID_FLAG;
    }

    if(ctx.tx[sfAccount] == ctx.tx[sfDestination]){
        JLOG(ctx.j.error()) << "RecurringPaymentSet: Account and Destination cannot be the same";
        return temREDUNDANT;
    }

    if(!ctx.tx.isFieldPresent(sfDestination)){
        if(!ctx.tx.isFieldPresent(sfPublicKey)){
            JLOG(ctx.j.error()) << "RecurringPaymentSet: Publickey is required when distination is omited";
            return temMALFORMED;
        }
        if(ctx.tx.getFieldVL(sfPublicKey) != ctx.tx.getFieldVL(sfSigningPubKey) ){
            JLOG(ctx.j.error()) << "RecurringPaymentSet: Publickey and SigningPubKey must match";
            return temMALFORMED;
        }
    }

    if(!ctx.tx.isFieldPresent(sfAmount) || ctx.tx.getFieldAmount(sfAmount).signum() <= 0){
        JLOG(ctx.j.error()) << "RecurringPaymentSet: Amount must be there and greater than 0";
        return temBAD_AMOUNT;
    }

    // auto const SYSTEM_MINIMUM = std::chrono::seconds(2592000);
    // if(!ctx.tx.isFieldPresent(sfFrequency) || ctx.tx.getFieldU64(sfFrequency) <= SYSTEM_MINIMUM){
    //     JLOG(ctx.j.error()) << "RecurringPaymentSet: Frequency must be there and greater than 0";
    //     return temMALFORMED;
    // }

    // if(ctx.tx.isFieldPresent(sfExpiration) && ctx.tx.getFieldU32(sfExpiration) <= ctx.tx.getBlockTime()){
    //     JLOG(ctx.j.debug()) << "RecurringPaymentSet: Expiration must be greater than current time";
    //     return temBAD_EXPIRATION;
    // }
    return preflight2(ctx);
}

TER
RecurringPaymentSet::checkPermission(ReadView const& view, STTx const& tx){
    return tesSUCCESS;
}

TER
RecurringPaymentSet::preclaim(PreclaimContext const& ctx){
    return tesSUCCESS;
}

TER
RecurringPaymentSet::doApply(){
    return tesSUCCESS;
}

} // namespace ripple
