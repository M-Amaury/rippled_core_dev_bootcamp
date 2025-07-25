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

#include <xrpld/app/ledger/OrderBookDB.h>
#include <xrpld/app/misc/AMMHelpers.h>
#include <xrpld/app/misc/AMMUtils.h>
#include <xrpld/app/tx/detail/AMMCreate.h>
#include <xrpld/ledger/Sandbox.h>
#include <xrpld/ledger/View.h>

#include <xrpl/protocol/AMMCore.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/STIssue.h>
#include <xrpl/protocol/TxFlags.h>

namespace ripple {

NotTEC
AMMCreate::preflight(PreflightContext const& ctx)
{
    if (!ammEnabled(ctx.rules))
        return temDISABLED;

    if (auto const ret = preflight1(ctx); !isTesSuccess(ret))
        return ret;

    if (ctx.tx.getFlags() & tfUniversalMask)
    {
        JLOG(ctx.j.debug()) << "AMM Instance: invalid flags.";
        return temINVALID_FLAG;
    }

    auto const amount = ctx.tx[sfAmount];
    auto const amount2 = ctx.tx[sfAmount2];

    if (amount.issue() == amount2.issue())
    {
        JLOG(ctx.j.debug())
            << "AMM Instance: tokens can not have the same currency/issuer.";
        return temBAD_AMM_TOKENS;
    }

    if (auto const err = invalidAMMAmount(amount))
    {
        JLOG(ctx.j.debug()) << "AMM Instance: invalid asset1 amount.";
        return err;
    }

    if (auto const err = invalidAMMAmount(amount2))
    {
        JLOG(ctx.j.debug()) << "AMM Instance: invalid asset2 amount.";
        return err;
    }

    if (ctx.tx[sfTradingFee] > TRADING_FEE_THRESHOLD)
    {
        JLOG(ctx.j.debug()) << "AMM Instance: invalid trading fee.";
        return temBAD_FEE;
    }

    return preflight2(ctx);
}

XRPAmount
AMMCreate::calculateBaseFee(ReadView const& view, STTx const& tx)
{
    // The fee required for AMMCreate is one owner reserve.
    return view.fees().increment;
}

TER
AMMCreate::preclaim(PreclaimContext const& ctx)
{
    auto const accountID = ctx.tx[sfAccount];
    auto const amount = ctx.tx[sfAmount];
    auto const amount2 = ctx.tx[sfAmount2];

    // Check if AMM already exists for the token pair
    if (auto const ammKeylet = keylet::amm(amount.issue(), amount2.issue());
        ctx.view.read(ammKeylet))
    {
        JLOG(ctx.j.debug()) << "AMM Instance: ltAMM already exists.";
        return tecDUPLICATE;
    }

    if (auto const ter = requireAuth(ctx.view, amount.issue(), accountID);
        ter != tesSUCCESS)
    {
        JLOG(ctx.j.debug())
            << "AMM Instance: account is not authorized, " << amount.issue();
        return ter;
    }

    if (auto const ter = requireAuth(ctx.view, amount2.issue(), accountID);
        ter != tesSUCCESS)
    {
        JLOG(ctx.j.debug())
            << "AMM Instance: account is not authorized, " << amount2.issue();
        return ter;
    }

    // Globally or individually frozen
    if (isFrozen(ctx.view, accountID, amount.issue()) ||
        isFrozen(ctx.view, accountID, amount2.issue()))
    {
        JLOG(ctx.j.debug()) << "AMM Instance: involves frozen asset.";
        return tecFROZEN;
    }

    auto noDefaultRipple = [](ReadView const& view, Issue const& issue) {
        if (isXRP(issue))
            return false;

        if (auto const issuerAccount =
                view.read(keylet::account(issue.account)))
            return (issuerAccount->getFlags() & lsfDefaultRipple) == 0;

        return false;
    };

    if (noDefaultRipple(ctx.view, amount.issue()) ||
        noDefaultRipple(ctx.view, amount2.issue()))
    {
        JLOG(ctx.j.debug()) << "AMM Instance: DefaultRipple not set";
        return terNO_RIPPLE;
    }

    // Check the reserve for LPToken trustline
    STAmount const xrpBalance = xrpLiquid(ctx.view, accountID, 1, ctx.j);
    // Insufficient reserve
    if (xrpBalance <= beast::zero)
    {
        JLOG(ctx.j.debug()) << "AMM Instance: insufficient reserves";
        return tecINSUF_RESERVE_LINE;
    }

    auto insufficientBalance = [&](STAmount const& asset) {
        if (isXRP(asset))
            return xrpBalance < asset;
        return accountID != asset.issue().account &&
            accountHolds(
                ctx.view,
                accountID,
                asset.issue(),
                FreezeHandling::fhZERO_IF_FROZEN,
                ctx.j) < asset;
    };

    if (insufficientBalance(amount) || insufficientBalance(amount2))
    {
        JLOG(ctx.j.debug())
            << "AMM Instance: insufficient funds, " << amount << " " << amount2;
        return tecUNFUNDED_AMM;
    }

    auto isLPToken = [&](STAmount const& amount) -> bool {
        if (auto const sle =
                ctx.view.read(keylet::account(amount.issue().account)))
            return sle->isFieldPresent(sfAMMID);
        return false;
    };

    if (isLPToken(amount) || isLPToken(amount2))
    {
        JLOG(ctx.j.debug()) << "AMM Instance: can't create with LPTokens "
                            << amount << " " << amount2;
        return tecAMM_INVALID_TOKENS;
    }

    if (ctx.view.rules().enabled(featureSingleAssetVault))
    {
        if (auto const accountId = pseudoAccountAddress(
                ctx.view, keylet::amm(amount.issue(), amount2.issue()).key);
            accountId == beast::zero)
            return terADDRESS_COLLISION;
    }

    // If featureAMMClawback is enabled, allow AMMCreate without checking
    // if the issuer has clawback enabled
    if (ctx.view.rules().enabled(featureAMMClawback))
        return tesSUCCESS;

    // Disallow AMM if the issuer has clawback enabled when featureAMMClawback
    // is not enabled
    auto clawbackDisabled = [&](Issue const& issue) -> TER {
        if (isXRP(issue))
            return tesSUCCESS;
        if (auto const sle = ctx.view.read(keylet::account(issue.account));
            !sle)
            return tecINTERNAL;
        else if (sle->getFlags() & lsfAllowTrustLineClawback)
            return tecNO_PERMISSION;
        return tesSUCCESS;
    };

    if (auto const ter = clawbackDisabled(amount.issue()); ter != tesSUCCESS)
        return ter;
    return clawbackDisabled(amount2.issue());
}

static std::pair<TER, bool>
applyCreate(
    ApplyContext& ctx_,
    Sandbox& sb,
    AccountID const& account_,
    beast::Journal j_)
{
    auto const amount = ctx_.tx[sfAmount];
    auto const amount2 = ctx_.tx[sfAmount2];

    auto const ammKeylet = keylet::amm(amount.issue(), amount2.issue());

    // Mitigate same account exists possibility
    auto const maybeAccount = createPseudoAccount(sb, ammKeylet.key, sfAMMID);
    // AMM account already exists (should not happen)
    if (!maybeAccount)
    {
        JLOG(j_.error()) << "AMM Instance: failed to create pseudo account.";
        return {maybeAccount.error(), false};
    }
    auto& account = *maybeAccount;
    auto const accountId = (*account)[sfAccount];

    // LP Token already exists. (should not happen)
    auto const lptIss = ammLPTIssue(
        amount.issue().currency, amount2.issue().currency, accountId);
    if (sb.read(keylet::line(accountId, lptIss)))
    {
        JLOG(j_.error()) << "AMM Instance: LP Token already exists.";
        return {tecDUPLICATE, false};
    }

    // Note, that the trustlines created by AMM have 0 credit limit.
    // This prevents shifting the balance between accounts via AMM,
    // or sending unsolicited LPTokens. This is a desired behavior.
    // A user can only receive LPTokens through affirmative action -
    // either an AMMDeposit, TrustSet, crossing an offer, etc.

    // Calculate initial LPT balance.
    auto const lpTokens = ammLPTokens(amount, amount2, lptIss);

    // Create ltAMM
    auto ammSle = std::make_shared<SLE>(ammKeylet);
    ammSle->setAccountID(sfAccount, accountId);
    ammSle->setFieldAmount(sfLPTokenBalance, lpTokens);
    auto const& [issue1, issue2] = std::minmax(amount.issue(), amount2.issue());
    ammSle->setFieldIssue(sfAsset, STIssue{sfAsset, issue1});
    ammSle->setFieldIssue(sfAsset2, STIssue{sfAsset2, issue2});
    // AMM creator gets the auction slot and the voting slot.
    initializeFeeAuctionVote(
        ctx_.view(), ammSle, account_, lptIss, ctx_.tx[sfTradingFee]);

    // Add owner directory to link the root account and AMM object.
    if (auto ter = dirLink(sb, accountId, ammSle); ter)
    {
        JLOG(j_.debug()) << "AMM Instance: failed to insert owner dir";
        return {ter, false};
    }
    sb.insert(ammSle);

    // Send LPT to LP.
    auto res = accountSend(sb, accountId, account_, lpTokens, ctx_.journal);
    if (res != tesSUCCESS)
    {
        JLOG(j_.debug()) << "AMM Instance: failed to send LPT " << lpTokens;
        return {res, false};
    }

    auto sendAndTrustSet = [&](STAmount const& amount) -> TER {
        if (auto const res = accountSend(
                sb,
                account_,
                accountId,
                amount,
                ctx_.journal,
                WaiveTransferFee::Yes))
            return res;
        // Set AMM flag on AMM trustline
        if (!isXRP(amount))
        {
            if (SLE::pointer sleRippleState =
                    sb.peek(keylet::line(accountId, amount.issue()));
                !sleRippleState)
                return tecINTERNAL;
            else
            {
                auto const flags = sleRippleState->getFlags();
                sleRippleState->setFieldU32(sfFlags, flags | lsfAMMNode);
                sb.update(sleRippleState);
            }
        }
        return tesSUCCESS;
    };

    // Send asset1.
    res = sendAndTrustSet(amount);
    if (res != tesSUCCESS)
    {
        JLOG(j_.debug()) << "AMM Instance: failed to send " << amount;
        return {res, false};
    }

    // Send asset2.
    res = sendAndTrustSet(amount2);
    if (res != tesSUCCESS)
    {
        JLOG(j_.debug()) << "AMM Instance: failed to send " << amount2;
        return {res, false};
    }

    JLOG(j_.debug()) << "AMM Instance: success " << accountId << " "
                     << ammKeylet.key << " " << lpTokens << " " << amount << " "
                     << amount2;
    auto addOrderBook =
        [&](Issue const& issueIn, Issue const& issueOut, std::uint64_t uRate) {
            Book const book{issueIn, issueOut, std::nullopt};
            auto const dir = keylet::quality(keylet::book(book), uRate);
            if (auto const bookExisted = static_cast<bool>(sb.read(dir));
                !bookExisted)
                ctx_.app.getOrderBookDB().addOrderBook(book);
        };
    addOrderBook(amount.issue(), amount2.issue(), getRate(amount2, amount));
    addOrderBook(amount2.issue(), amount.issue(), getRate(amount, amount2));

    return {res, res == tesSUCCESS};
}

TER
AMMCreate::doApply()
{
    // This is the ledger view that we work against. Transactions are applied
    // as we go on processing transactions.
    Sandbox sb(&ctx_.view());

    auto const result = applyCreate(ctx_, sb, account_, j_);
    if (result.second)
        sb.apply(ctx_.rawView());

    return result.first;
}

}  // namespace ripple
