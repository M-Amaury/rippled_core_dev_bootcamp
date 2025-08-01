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

#include <xrpld/app/tx/applySteps.h>
#include <xrpld/app/tx/detail/AMMBid.h>
#include <xrpld/app/tx/detail/AMMClawback.h>
#include <xrpld/app/tx/detail/AMMCreate.h>
#include <xrpld/app/tx/detail/AMMDelete.h>
#include <xrpld/app/tx/detail/AMMDeposit.h>
#include <xrpld/app/tx/detail/AMMVote.h>
#include <xrpld/app/tx/detail/AMMWithdraw.h>
#include <xrpld/app/tx/detail/ApplyContext.h>
#include <xrpld/app/tx/detail/Batch.h>
#include <xrpld/app/tx/detail/CancelCheck.h>
#include <xrpld/app/tx/detail/CancelOffer.h>
#include <xrpld/app/tx/detail/CashCheck.h>
#include <xrpld/app/tx/detail/Change.h>
#include <xrpld/app/tx/detail/Clawback.h>
#include <xrpld/app/tx/detail/CreateCheck.h>
#include <xrpld/app/tx/detail/CreateOffer.h>
#include <xrpld/app/tx/detail/CreateTicket.h>
#include <xrpld/app/tx/detail/Credentials.h>
#include <xrpld/app/tx/detail/DID.h>
#include <xrpld/app/tx/detail/DelegateSet.h>
#include <xrpld/app/tx/detail/DeleteAccount.h>
#include <xrpld/app/tx/detail/DeleteOracle.h>
#include <xrpld/app/tx/detail/DepositPreauth.h>
#include <xrpld/app/tx/detail/Escrow.h>
#include <xrpld/app/tx/detail/LedgerStateFix.h>
#include <xrpld/app/tx/detail/MPTokenAuthorize.h>
#include <xrpld/app/tx/detail/MPTokenIssuanceCreate.h>
#include <xrpld/app/tx/detail/MPTokenIssuanceDestroy.h>
#include <xrpld/app/tx/detail/MPTokenIssuanceSet.h>
#include <xrpld/app/tx/detail/NFTokenAcceptOffer.h>
#include <xrpld/app/tx/detail/NFTokenBurn.h>
#include <xrpld/app/tx/detail/NFTokenCancelOffer.h>
#include <xrpld/app/tx/detail/NFTokenCreateOffer.h>
#include <xrpld/app/tx/detail/NFTokenMint.h>
#include <xrpld/app/tx/detail/NFTokenModify.h>
#include <xrpld/app/tx/detail/PayChan.h>
#include <xrpld/app/tx/detail/Payment.h>
#include <xrpld/app/tx/detail/PermissionedDomainDelete.h>
#include <xrpld/app/tx/detail/PermissionedDomainSet.h>
#include <xrpld/app/tx/detail/RecurringPaymentSet.h>
#include <xrpld/app/tx/detail/SetAccount.h>
#include <xrpld/app/tx/detail/SetOracle.h>
#include <xrpld/app/tx/detail/SetRegularKey.h>
#include <xrpld/app/tx/detail/SetSignerList.h>
#include <xrpld/app/tx/detail/SetTrust.h>
#include <xrpld/app/tx/detail/VaultClawback.h>
#include <xrpld/app/tx/detail/VaultCreate.h>
#include <xrpld/app/tx/detail/VaultDelete.h>
#include <xrpld/app/tx/detail/VaultDeposit.h>
#include <xrpld/app/tx/detail/VaultSet.h>
#include <xrpld/app/tx/detail/VaultWithdraw.h>
#include <xrpld/app/tx/detail/XChainBridge.h>

#include <xrpl/protocol/TxFormats.h>

#include <stdexcept>

namespace ripple {

namespace {

struct UnknownTxnType : std::exception
{
    TxType txnType;
    UnknownTxnType(TxType t) : txnType{t}
    {
    }
};

// Call a lambda with the concrete transaction type as a template parameter
// throw an "UnknownTxnType" exception on error
template <class F>
auto
with_txn_type(TxType txnType, F&& f)
{
    switch (txnType)
    {
#pragma push_macro("TRANSACTION")
#undef TRANSACTION

#define TRANSACTION(tag, value, name, delegatable, fields) \
    case tag:                                              \
        return f.template operator()<name>();

#include <xrpl/protocol/detail/transactions.macro>

#undef TRANSACTION
#pragma pop_macro("TRANSACTION")
        default:
            throw UnknownTxnType(txnType);
    }
}
}  // namespace

// Templates so preflight does the right thing with T::ConsequencesFactory.
//
// This could be done more easily using if constexpr, but Visual Studio
// 2017 doesn't handle if constexpr correctly.  So once we're no longer
// building with Visual Studio 2017 we can consider replacing the four
// templates with a single template function that uses if constexpr.
//
// For Transactor::Normal
//

// clang-format off
// Current formatter for rippled is based on clang-10, which does not handle `requires` clauses
template <class T>
requires(T::ConsequencesFactory == Transactor::Normal)
TxConsequences
    consequences_helper(PreflightContext const& ctx)
{
    return TxConsequences(ctx.tx);
};

// For Transactor::Blocker
template <class T>
requires(T::ConsequencesFactory == Transactor::Blocker)
TxConsequences
    consequences_helper(PreflightContext const& ctx)
{
    return TxConsequences(ctx.tx, TxConsequences::blocker);
};

// For Transactor::Custom
template <class T>
requires(T::ConsequencesFactory == Transactor::Custom)
TxConsequences
    consequences_helper(PreflightContext const& ctx)
{
    return T::makeTxConsequences(ctx);
};
// clang-format on

static std::pair<NotTEC, TxConsequences>
invoke_preflight(PreflightContext const& ctx)
{
    try
    {
        return with_txn_type(ctx.tx.getTxnType(), [&]<typename T>() {
            auto const tec = T::preflight(ctx);
            return std::make_pair(
                tec,
                isTesSuccess(tec) ? consequences_helper<T>(ctx)
                                  : TxConsequences{tec});
        });
    }
    catch (UnknownTxnType const& e)
    {
        // Should never happen
        JLOG(ctx.j.fatal())
            << "Unknown transaction type in preflight: " << e.txnType;
        UNREACHABLE("ripple::invoke_preflight : unknown transaction type");
        return {temUNKNOWN, TxConsequences{temUNKNOWN}};
    }
}

static TER
invoke_preclaim(PreclaimContext const& ctx)
{
    try
    {
        // use name hiding to accomplish compile-time polymorphism of static
        // class functions for Transactor and derived classes.
        return with_txn_type(ctx.tx.getTxnType(), [&]<typename T>() {
            // If the transactor requires a valid account and the transaction
            // doesn't list one, preflight will have already a flagged a
            // failure.
            auto const id = ctx.tx.getAccountID(sfAccount);

            if (id != beast::zero)
            {
                TER result = T::checkSeqProxy(ctx.view, ctx.tx, ctx.j);

                if (result != tesSUCCESS)
                    return result;

                result = T::checkPriorTxAndLastLedger(ctx);

                if (result != tesSUCCESS)
                    return result;

                result = T::checkFee(ctx, calculateBaseFee(ctx.view, ctx.tx));

                if (result != tesSUCCESS)
                    return result;

                result = T::checkPermission(ctx.view, ctx.tx);

                if (result != tesSUCCESS)
                    return result;

                result = T::checkSign(ctx);

                if (result != tesSUCCESS)
                    return result;
            }

            return T::preclaim(ctx);
        });
    }
    catch (UnknownTxnType const& e)
    {
        // Should never happen
        JLOG(ctx.j.fatal())
            << "Unknown transaction type in preclaim: " << e.txnType;
        UNREACHABLE("ripple::invoke_preclaim : unknown transaction type");
        return temUNKNOWN;
    }
}

/**
 * @brief Calculates the base fee for a given transaction.
 *
 * This function determines the base fee required for the specified transaction
 * by invoking the appropriate fee calculation logic based on the transaction
 * type. It uses a type-dispatch mechanism to select the correct calculation
 * method.
 *
 * @param view The ledger view to use for fee calculation.
 * @param tx The transaction for which the base fee is to be calculated.
 * @return The calculated base fee as an XRPAmount.
 *
 * @throws std::exception If an error occurs during fee calculation, including
 * but not limited to unknown transaction types or internal errors, the function
 * logs an error and returns an XRPAmount of zero.
 */
static XRPAmount
invoke_calculateBaseFee(ReadView const& view, STTx const& tx)
{
    try
    {
        return with_txn_type(tx.getTxnType(), [&]<typename T>() {
            return T::calculateBaseFee(view, tx);
        });
    }
    catch (UnknownTxnType const& e)
    {
        UNREACHABLE(
            "ripple::invoke_calculateBaseFee : unknown transaction type");
        return XRPAmount{0};
    }
}

TxConsequences::TxConsequences(NotTEC pfresult)
    : isBlocker_(false)
    , fee_(beast::zero)
    , potentialSpend_(beast::zero)
    , seqProx_(SeqProxy::sequence(0))
    , sequencesConsumed_(0)
{
    XRPL_ASSERT(
        !isTesSuccess(pfresult),
        "ripple::TxConsequences::TxConsequences : is not tesSUCCESS");
}

TxConsequences::TxConsequences(STTx const& tx)
    : isBlocker_(false)
    , fee_(
          tx[sfFee].native() && !tx[sfFee].negative() ? tx[sfFee].xrp()
                                                      : beast::zero)
    , potentialSpend_(beast::zero)
    , seqProx_(tx.getSeqProxy())
    , sequencesConsumed_(tx.getSeqProxy().isSeq() ? 1 : 0)
{
}

TxConsequences::TxConsequences(STTx const& tx, Category category)
    : TxConsequences(tx)
{
    isBlocker_ = (category == blocker);
}

TxConsequences::TxConsequences(STTx const& tx, XRPAmount potentialSpend)
    : TxConsequences(tx)
{
    potentialSpend_ = potentialSpend;
}

TxConsequences::TxConsequences(STTx const& tx, std::uint32_t sequencesConsumed)
    : TxConsequences(tx)
{
    sequencesConsumed_ = sequencesConsumed;
}

static ApplyResult
invoke_apply(ApplyContext& ctx)
{
    try
    {
        return with_txn_type(ctx.tx.getTxnType(), [&]<typename T>() {
            T p(ctx);
            return p();
        });
    }
    catch (UnknownTxnType const& e)
    {
        // Should never happen
        JLOG(ctx.journal.fatal())
            << "Unknown transaction type in apply: " << e.txnType;
        UNREACHABLE("ripple::invoke_apply : unknown transaction type");
        return {temUNKNOWN, false};
    }
}

PreflightResult
preflight(
    Application& app,
    Rules const& rules,
    STTx const& tx,
    ApplyFlags flags,
    beast::Journal j)
{
    PreflightContext const pfctx(app, tx, rules, flags, j);
    try
    {
        return {pfctx, invoke_preflight(pfctx)};
    }
    catch (std::exception const& e)
    {
        JLOG(j.fatal()) << "apply (preflight): " << e.what();
        return {pfctx, {tefEXCEPTION, TxConsequences{tx}}};
    }
}

PreflightResult
preflight(
    Application& app,
    Rules const& rules,
    uint256 const& parentBatchId,
    STTx const& tx,
    ApplyFlags flags,
    beast::Journal j)
{
    PreflightContext const pfctx(app, tx, parentBatchId, rules, flags, j);
    try
    {
        return {pfctx, invoke_preflight(pfctx)};
    }
    catch (std::exception const& e)
    {
        JLOG(j.fatal()) << "apply (preflight): " << e.what();
        return {pfctx, {tefEXCEPTION, TxConsequences{tx}}};
    }
}

PreclaimResult
preclaim(
    PreflightResult const& preflightResult,
    Application& app,
    OpenView const& view)
{
    std::optional<PreclaimContext const> ctx;
    if (preflightResult.rules != view.rules())
    {
        auto secondFlight = [&]() {
            if (preflightResult.parentBatchId)
                return preflight(
                    app,
                    view.rules(),
                    preflightResult.parentBatchId.value(),
                    preflightResult.tx,
                    preflightResult.flags,
                    preflightResult.j);

            return preflight(
                app,
                view.rules(),
                preflightResult.tx,
                preflightResult.flags,
                preflightResult.j);
        }();

        ctx.emplace(
            app,
            view,
            secondFlight.ter,
            secondFlight.tx,
            secondFlight.flags,
            secondFlight.parentBatchId,
            secondFlight.j);
    }
    else
    {
        ctx.emplace(
            app,
            view,
            preflightResult.ter,
            preflightResult.tx,
            preflightResult.flags,
            preflightResult.parentBatchId,
            preflightResult.j);
    }

    try
    {
        if (ctx->preflightResult != tesSUCCESS)
            return {*ctx, ctx->preflightResult};
        return {*ctx, invoke_preclaim(*ctx)};
    }
    catch (std::exception const& e)
    {
        JLOG(ctx->j.fatal()) << "apply (preclaim): " << e.what();
        return {*ctx, tefEXCEPTION};
    }
}

XRPAmount
calculateBaseFee(ReadView const& view, STTx const& tx)
{
    return invoke_calculateBaseFee(view, tx);
}

XRPAmount
calculateDefaultBaseFee(ReadView const& view, STTx const& tx)
{
    return Transactor::calculateBaseFee(view, tx);
}

ApplyResult
doApply(PreclaimResult const& preclaimResult, Application& app, OpenView& view)
{
    if (preclaimResult.view.seq() != view.seq())
    {
        // Logic error from the caller. Don't have enough
        // info to recover.
        return {tefEXCEPTION, false};
    }
    try
    {
        if (!preclaimResult.likelyToClaimFee)
            return {preclaimResult.ter, false};
        ApplyContext ctx(
            app,
            view,
            preclaimResult.parentBatchId,
            preclaimResult.tx,
            preclaimResult.ter,
            calculateBaseFee(view, preclaimResult.tx),
            preclaimResult.flags,
            preclaimResult.j);
        return invoke_apply(ctx);
    }
    catch (std::exception const& e)
    {
        JLOG(preclaimResult.j.fatal()) << "apply: " << e.what();
        return {tefEXCEPTION, false};
    }
}

}  // namespace ripple
