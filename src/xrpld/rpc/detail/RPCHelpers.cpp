//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012-2014 Ripple Labs Inc.

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

#include <xrpld/app/ledger/LedgerMaster.h>
#include <xrpld/app/ledger/LedgerToJson.h>
#include <xrpld/app/ledger/OpenLedger.h>
#include <xrpld/app/misc/Transaction.h>
#include <xrpld/app/paths/TrustLine.h>
#include <xrpld/app/rdb/RelationalDatabase.h>
#include <xrpld/app/tx/detail/NFTokenUtils.h>
#include <xrpld/ledger/View.h>
#include <xrpld/rpc/Context.h>
#include <xrpld/rpc/DeliveredAmount.h>
#include <xrpld/rpc/detail/RPCHelpers.h>

#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/RPCErr.h>
#include <xrpl/protocol/nftPageMask.h>
#include <xrpl/resource/Fees.h>

#include <boost/algorithm/string/case_conv.hpp>
#include <boost/algorithm/string/predicate.hpp>

namespace ripple {
namespace RPC {

std::optional<AccountID>
accountFromStringStrict(std::string const& account)
{
    std::optional<AccountID> result;

    auto const publicKey =
        parseBase58<PublicKey>(TokenType::AccountPublic, account);

    if (publicKey)
        result = calcAccountID(*publicKey);
    else
        result = parseBase58<AccountID>(account);

    return result;
}

error_code_i
accountFromStringWithCode(
    AccountID& result,
    std::string const& strIdent,
    bool bStrict)
{
    if (auto accountID = accountFromStringStrict(strIdent))
    {
        result = *accountID;
        return rpcSUCCESS;
    }

    if (bStrict)
        return rpcACT_MALFORMED;

    // We allow the use of the seeds which is poor practice
    // and merely for debugging convenience.
    auto const seed = parseGenericSeed(strIdent);

    if (!seed)
        return rpcBAD_SEED;

    auto const keypair = generateKeyPair(KeyType::secp256k1, *seed);

    result = calcAccountID(keypair.first);
    return rpcSUCCESS;
}

Json::Value
accountFromString(AccountID& result, std::string const& strIdent, bool bStrict)
{
    error_code_i code = accountFromStringWithCode(result, strIdent, bStrict);
    if (code != rpcSUCCESS)
        return rpcError(code);
    else
        return Json::objectValue;
}

std::uint64_t
getStartHint(std::shared_ptr<SLE const> const& sle, AccountID const& accountID)
{
    if (sle->getType() == ltRIPPLE_STATE)
    {
        if (sle->getFieldAmount(sfLowLimit).getIssuer() == accountID)
            return sle->getFieldU64(sfLowNode);
        else if (sle->getFieldAmount(sfHighLimit).getIssuer() == accountID)
            return sle->getFieldU64(sfHighNode);
    }

    if (!sle->isFieldPresent(sfOwnerNode))
        return 0;

    return sle->getFieldU64(sfOwnerNode);
}

bool
isRelatedToAccount(
    ReadView const& ledger,
    std::shared_ptr<SLE const> const& sle,
    AccountID const& accountID)
{
    if (sle->getType() == ltRIPPLE_STATE)
    {
        return (sle->getFieldAmount(sfLowLimit).getIssuer() == accountID) ||
            (sle->getFieldAmount(sfHighLimit).getIssuer() == accountID);
    }
    else if (sle->isFieldPresent(sfAccount))
    {
        // If there's an sfAccount present, also test the sfDestination, if
        // present. This will match objects such as Escrows (ltESCROW), Payment
        // Channels (ltPAYCHAN), and Checks (ltCHECK) because those are added to
        // the Destination account's directory. It intentionally EXCLUDES
        // NFToken Offers (ltNFTOKEN_OFFER). NFToken Offers are NOT added to the
        // Destination account's directory.
        return sle->getAccountID(sfAccount) == accountID ||
            (sle->isFieldPresent(sfDestination) &&
             sle->getAccountID(sfDestination) == accountID);
    }
    else if (sle->getType() == ltSIGNER_LIST)
    {
        Keylet const accountSignerList = keylet::signers(accountID);
        return sle->key() == accountSignerList.key;
    }
    else if (sle->getType() == ltNFTOKEN_OFFER)
    {
        // Do not check the sfDestination field. NFToken Offers are NOT added to
        // the Destination account's directory.
        return sle->getAccountID(sfOwner) == accountID;
    }

    return false;
}

bool
getAccountObjects(
    ReadView const& ledger,
    AccountID const& account,
    std::optional<std::vector<LedgerEntryType>> const& typeFilter,
    uint256 dirIndex,
    uint256 entryIndex,
    std::uint32_t const limit,
    Json::Value& jvResult)
{
    // check if dirIndex is valid
    if (!dirIndex.isZero() && !ledger.read({ltDIR_NODE, dirIndex}))
        return false;

    auto typeMatchesFilter = [](std::vector<LedgerEntryType> const& typeFilter,
                                LedgerEntryType ledgerType) {
        auto it = std::find(typeFilter.begin(), typeFilter.end(), ledgerType);
        return it != typeFilter.end();
    };

    // if dirIndex != 0, then all NFTs have already been returned.  only
    // iterate NFT pages if the filter says so AND dirIndex == 0
    bool iterateNFTPages =
        (!typeFilter.has_value() ||
         typeMatchesFilter(typeFilter.value(), ltNFTOKEN_PAGE)) &&
        dirIndex == beast::zero;

    Keylet const firstNFTPage = keylet::nftpage_min(account);

    // we need to check the marker to see if it is an NFTTokenPage index.
    if (iterateNFTPages && entryIndex != beast::zero)
    {
        // if it is we will try to iterate the pages up to the limit
        // and then change over to the owner directory

        if (firstNFTPage.key != (entryIndex & ~nft::pageMask))
            iterateNFTPages = false;
    }

    auto& jvObjects = (jvResult[jss::account_objects] = Json::arrayValue);

    // this is a mutable version of limit, used to seemlessly switch
    // to iterating directory entries when nftokenpages are exhausted
    uint32_t mlimit = limit;

    // iterate NFTokenPages preferentially
    if (iterateNFTPages)
    {
        Keylet const first = entryIndex == beast::zero
            ? firstNFTPage
            : Keylet{ltNFTOKEN_PAGE, entryIndex};

        Keylet const last = keylet::nftpage_max(account);

        // current key
        uint256 ck = ledger.succ(first.key, last.key.next()).value_or(last.key);

        // current page
        auto cp = ledger.read(Keylet{ltNFTOKEN_PAGE, ck});

        while (cp)
        {
            jvObjects.append(cp->getJson(JsonOptions::none));
            auto const npm = (*cp)[~sfNextPageMin];
            if (npm)
                cp = ledger.read(Keylet(ltNFTOKEN_PAGE, *npm));
            else
                cp = nullptr;

            if (--mlimit == 0)
            {
                if (cp)
                {
                    jvResult[jss::limit] = limit;
                    jvResult[jss::marker] = std::string("0,") + to_string(ck);
                    return true;
                }
            }

            if (!npm)
                break;

            ck = *npm;
        }

        // if execution reaches here then we're about to transition
        // to iterating the root directory (and the conventional
        // behaviour of this RPC function.) Therefore we should
        // zero entryIndex so as not to terribly confuse things.
        entryIndex = beast::zero;
    }

    auto const root = keylet::ownerDir(account);
    auto found = false;

    if (dirIndex.isZero())
    {
        dirIndex = root.key;
        found = true;
    }

    auto dir = ledger.read({ltDIR_NODE, dirIndex});
    if (!dir)
    {
        // it's possible the user had nftoken pages but no
        // directory entries. If there's no nftoken page, we will
        // give empty array for account_objects.
        if (mlimit >= limit)
            jvResult[jss::account_objects] = Json::arrayValue;

        // non-zero dirIndex validity was checked in the beginning of this
        // function; by this point, it should be zero. This function returns
        // true regardless of nftoken page presence; if absent, account_objects
        // is already set as an empty array. Notice we will only return false in
        // this function when entryIndex can not be found, indicating an invalid
        // marker error.
        return true;
    }

    std::uint32_t i = 0;
    for (;;)
    {
        auto const& entries = dir->getFieldV256(sfIndexes);
        auto iter = entries.begin();

        if (!found)
        {
            iter = std::find(iter, entries.end(), entryIndex);
            if (iter == entries.end())
                return false;

            found = true;
        }

        // it's possible that the returned NFTPages exactly filled the
        // response.  Check for that condition.
        if (i == mlimit && mlimit < limit)
        {
            jvResult[jss::limit] = limit;
            jvResult[jss::marker] =
                to_string(dirIndex) + ',' + to_string(*iter);
            return true;
        }

        for (; iter != entries.end(); ++iter)
        {
            auto const sleNode = ledger.read(keylet::child(*iter));

            if (!typeFilter.has_value() ||
                typeMatchesFilter(typeFilter.value(), sleNode->getType()))
            {
                jvObjects.append(sleNode->getJson(JsonOptions::none));
            }

            if (++i == mlimit)
            {
                if (++iter != entries.end())
                {
                    jvResult[jss::limit] = limit;
                    jvResult[jss::marker] =
                        to_string(dirIndex) + ',' + to_string(*iter);
                    return true;
                }

                break;
            }
        }

        auto const nodeIndex = dir->getFieldU64(sfIndexNext);
        if (nodeIndex == 0)
            return true;

        dirIndex = keylet::page(root, nodeIndex).key;
        dir = ledger.read({ltDIR_NODE, dirIndex});
        if (!dir)
            return true;

        if (i == mlimit)
        {
            auto const& e = dir->getFieldV256(sfIndexes);
            if (!e.empty())
            {
                jvResult[jss::limit] = limit;
                jvResult[jss::marker] =
                    to_string(dirIndex) + ',' + to_string(*e.begin());
            }

            return true;
        }
    }
}

namespace {

bool
isValidatedOld(LedgerMaster& ledgerMaster, bool standalone)
{
    if (standalone)
        return false;

    return ledgerMaster.getValidatedLedgerAge() > Tuning::maxValidatedLedgerAge;
}

template <class T>
Status
ledgerFromRequest(T& ledger, JsonContext& context)
{
    ledger.reset();

    auto& params = context.params;

    auto indexValue = params[jss::ledger_index];
    auto hashValue = params[jss::ledger_hash];

    // We need to support the legacy "ledger" field.
    auto& legacyLedger = params[jss::ledger];
    if (legacyLedger)
    {
        if (legacyLedger.asString().size() > 12)
            hashValue = legacyLedger;
        else
            indexValue = legacyLedger;
    }

    if (hashValue)
    {
        if (!hashValue.isString())
            return {rpcINVALID_PARAMS, "ledgerHashNotString"};

        uint256 ledgerHash;
        if (!ledgerHash.parseHex(hashValue.asString()))
            return {rpcINVALID_PARAMS, "ledgerHashMalformed"};
        return getLedger(ledger, ledgerHash, context);
    }

    auto const index = indexValue.asString();

    if (index == "current" || index.empty())
        return getLedger(ledger, LedgerShortcut::CURRENT, context);

    if (index == "validated")
        return getLedger(ledger, LedgerShortcut::VALIDATED, context);

    if (index == "closed")
        return getLedger(ledger, LedgerShortcut::CLOSED, context);

    std::uint32_t iVal;
    if (beast::lexicalCastChecked(iVal, index))
        return getLedger(ledger, iVal, context);

    return {rpcINVALID_PARAMS, "ledgerIndexMalformed"};
}
}  // namespace

template <class T, class R>
Status
ledgerFromRequest(T& ledger, GRPCContext<R>& context)
{
    R& request = context.params;
    return ledgerFromSpecifier(ledger, request.ledger(), context);
}

// explicit instantiation of above function
template Status
ledgerFromRequest<>(
    std::shared_ptr<ReadView const>&,
    GRPCContext<org::xrpl::rpc::v1::GetLedgerEntryRequest>&);

// explicit instantiation of above function
template Status
ledgerFromRequest<>(
    std::shared_ptr<ReadView const>&,
    GRPCContext<org::xrpl::rpc::v1::GetLedgerDataRequest>&);

// explicit instantiation of above function
template Status
ledgerFromRequest<>(
    std::shared_ptr<ReadView const>&,
    GRPCContext<org::xrpl::rpc::v1::GetLedgerRequest>&);

template <class T>
Status
ledgerFromSpecifier(
    T& ledger,
    org::xrpl::rpc::v1::LedgerSpecifier const& specifier,
    Context& context)
{
    ledger.reset();

    using LedgerCase = org::xrpl::rpc::v1::LedgerSpecifier::LedgerCase;
    LedgerCase ledgerCase = specifier.ledger_case();
    switch (ledgerCase)
    {
        case LedgerCase::kHash: {
            if (auto hash = uint256::fromVoidChecked(specifier.hash()))
            {
                return getLedger(ledger, *hash, context);
            }
            return {rpcINVALID_PARAMS, "ledgerHashMalformed"};
        }
        case LedgerCase::kSequence:
            return getLedger(ledger, specifier.sequence(), context);
        case LedgerCase::kShortcut:
            [[fallthrough]];
        case LedgerCase::LEDGER_NOT_SET: {
            auto const shortcut = specifier.shortcut();
            if (shortcut ==
                org::xrpl::rpc::v1::LedgerSpecifier::SHORTCUT_VALIDATED)
            {
                return getLedger(ledger, LedgerShortcut::VALIDATED, context);
            }
            else
            {
                if (shortcut ==
                        org::xrpl::rpc::v1::LedgerSpecifier::SHORTCUT_CURRENT ||
                    shortcut ==
                        org::xrpl::rpc::v1::LedgerSpecifier::
                            SHORTCUT_UNSPECIFIED)
                {
                    return getLedger(ledger, LedgerShortcut::CURRENT, context);
                }
                else if (
                    shortcut ==
                    org::xrpl::rpc::v1::LedgerSpecifier::SHORTCUT_CLOSED)
                {
                    return getLedger(ledger, LedgerShortcut::CLOSED, context);
                }
            }
        }
    }

    return Status::OK;
}

template <class T>
Status
getLedger(T& ledger, uint256 const& ledgerHash, Context& context)
{
    ledger = context.ledgerMaster.getLedgerByHash(ledgerHash);
    if (ledger == nullptr)
        return {rpcLGR_NOT_FOUND, "ledgerNotFound"};
    return Status::OK;
}

template <class T>
Status
getLedger(T& ledger, uint32_t ledgerIndex, Context& context)
{
    ledger = context.ledgerMaster.getLedgerBySeq(ledgerIndex);
    if (ledger == nullptr)
    {
        auto cur = context.ledgerMaster.getCurrentLedger();
        if (cur->info().seq == ledgerIndex)
        {
            ledger = cur;
        }
    }

    if (ledger == nullptr)
        return {rpcLGR_NOT_FOUND, "ledgerNotFound"};

    if (ledger->info().seq > context.ledgerMaster.getValidLedgerIndex() &&
        isValidatedOld(context.ledgerMaster, context.app.config().standalone()))
    {
        ledger.reset();
        if (context.apiVersion == 1)
            return {rpcNO_NETWORK, "InsufficientNetworkMode"};
        return {rpcNOT_SYNCED, "notSynced"};
    }

    return Status::OK;
}

template <class T>
Status
getLedger(T& ledger, LedgerShortcut shortcut, Context& context)
{
    if (isValidatedOld(context.ledgerMaster, context.app.config().standalone()))
    {
        if (context.apiVersion == 1)
            return {rpcNO_NETWORK, "InsufficientNetworkMode"};
        return {rpcNOT_SYNCED, "notSynced"};
    }

    if (shortcut == LedgerShortcut::VALIDATED)
    {
        ledger = context.ledgerMaster.getValidatedLedger();
        if (ledger == nullptr)
        {
            if (context.apiVersion == 1)
                return {rpcNO_NETWORK, "InsufficientNetworkMode"};
            return {rpcNOT_SYNCED, "notSynced"};
        }

        XRPL_ASSERT(
            !ledger->open(), "ripple::RPC::getLedger : validated is not open");
    }
    else
    {
        if (shortcut == LedgerShortcut::CURRENT)
        {
            ledger = context.ledgerMaster.getCurrentLedger();
            XRPL_ASSERT(
                ledger->open(), "ripple::RPC::getLedger : current is open");
        }
        else if (shortcut == LedgerShortcut::CLOSED)
        {
            ledger = context.ledgerMaster.getClosedLedger();
            XRPL_ASSERT(
                !ledger->open(), "ripple::RPC::getLedger : closed is not open");
        }
        else
        {
            return {rpcINVALID_PARAMS, "ledgerIndexMalformed"};
        }

        if (ledger == nullptr)
        {
            if (context.apiVersion == 1)
                return {rpcNO_NETWORK, "InsufficientNetworkMode"};
            return {rpcNOT_SYNCED, "notSynced"};
        }

        static auto const minSequenceGap = 10;

        if (ledger->info().seq + minSequenceGap <
            context.ledgerMaster.getValidLedgerIndex())
        {
            ledger.reset();
            if (context.apiVersion == 1)
                return {rpcNO_NETWORK, "InsufficientNetworkMode"};
            return {rpcNOT_SYNCED, "notSynced"};
        }
    }
    return Status::OK;
}

// Explicit instantiaion of above three functions
template Status
getLedger<>(std::shared_ptr<ReadView const>&, uint32_t, Context&);

template Status
getLedger<>(
    std::shared_ptr<ReadView const>&,
    LedgerShortcut shortcut,
    Context&);

template Status
getLedger<>(std::shared_ptr<ReadView const>&, uint256 const&, Context&);

// The previous version of the lookupLedger command would accept the
// "ledger_index" argument as a string and silently treat it as a request to
// return the current ledger which, while not strictly wrong, could cause a lot
// of confusion.
//
// The code now robustly validates the input and ensures that the only possible
// values for the "ledger_index" parameter are the index of a ledger passed as
// an integer or one of the strings "current", "closed" or "validated".
// Additionally, the code ensures that the value passed in "ledger_hash" is a
// string and a valid hash. Invalid values will return an appropriate error
// code.
//
// In the absence of the "ledger_hash" or "ledger_index" parameters, the code
// assumes that "ledger_index" has the value "current".
//
// Returns a Json::objectValue.  If there was an error, it will be in that
// return value.  Otherwise, the object contains the field "validated" and
// optionally the fields "ledger_hash", "ledger_index" and
// "ledger_current_index", if they are defined.
Status
lookupLedger(
    std::shared_ptr<ReadView const>& ledger,
    JsonContext& context,
    Json::Value& result)
{
    if (auto status = ledgerFromRequest(ledger, context))
        return status;

    auto& info = ledger->info();

    if (!ledger->open())
    {
        result[jss::ledger_hash] = to_string(info.hash);
        result[jss::ledger_index] = info.seq;
    }
    else
    {
        result[jss::ledger_current_index] = info.seq;
    }

    result[jss::validated] = context.ledgerMaster.isValidated(*ledger);
    return Status::OK;
}

Json::Value
lookupLedger(std::shared_ptr<ReadView const>& ledger, JsonContext& context)
{
    Json::Value result;
    if (auto status = lookupLedger(ledger, context, result))
        status.inject(result);

    return result;
}

hash_set<AccountID>
parseAccountIds(Json::Value const& jvArray)
{
    hash_set<AccountID> result;
    for (auto const& jv : jvArray)
    {
        if (!jv.isString())
            return hash_set<AccountID>();
        auto const id = parseBase58<AccountID>(jv.asString());
        if (!id)
            return hash_set<AccountID>();
        result.insert(*id);
    }
    return result;
}

void
injectSLE(Json::Value& jv, SLE const& sle)
{
    jv = sle.getJson(JsonOptions::none);
    if (sle.getType() == ltACCOUNT_ROOT)
    {
        if (sle.isFieldPresent(sfEmailHash))
        {
            auto const& hash = sle.getFieldH128(sfEmailHash);
            Blob const b(hash.begin(), hash.end());
            std::string md5 = strHex(makeSlice(b));
            boost::to_lower(md5);
            // VFALCO TODO Give a name and move this constant
            //             to a more visible location. Also
            //             shouldn't this be https?
            jv[jss::urlgravatar] =
                str(boost::format("http://www.gravatar.com/avatar/%s") % md5);
        }
    }
    else
    {
        jv[jss::Invalid] = true;
    }
}

std::optional<Json::Value>
readLimitField(
    unsigned int& limit,
    Tuning::LimitRange const& range,
    JsonContext const& context)
{
    limit = range.rdefault;
    if (auto const& jvLimit = context.params[jss::limit])
    {
        if (!(jvLimit.isUInt() || (jvLimit.isInt() && jvLimit.asInt() >= 0)))
            return RPC::expected_field_error(jss::limit, "unsigned integer");

        limit = jvLimit.asUInt();
        if (!isUnlimited(context.role))
            limit = std::max(range.rmin, std::min(range.rmax, limit));
    }
    return std::nullopt;
}

std::optional<Seed>
parseRippleLibSeed(Json::Value const& value)
{
    // ripple-lib encodes seed used to generate an Ed25519 wallet in a
    // non-standard way. While rippled never encode seeds that way, we
    // try to detect such keys to avoid user confusion.
    if (!value.isString())
        return std::nullopt;

    auto const result = decodeBase58Token(value.asString(), TokenType::None);

    if (result.size() == 18 &&
        static_cast<std::uint8_t>(result[0]) == std::uint8_t(0xE1) &&
        static_cast<std::uint8_t>(result[1]) == std::uint8_t(0x4B))
        return Seed(makeSlice(result.substr(2)));

    return std::nullopt;
}

std::optional<Seed>
getSeedFromRPC(Json::Value const& params, Json::Value& error)
{
    using string_to_seed_t =
        std::function<std::optional<Seed>(std::string const&)>;
    using seed_match_t = std::pair<char const*, string_to_seed_t>;

    static seed_match_t const seedTypes[]{
        {jss::passphrase.c_str(),
         [](std::string const& s) { return parseGenericSeed(s); }},
        {jss::seed.c_str(),
         [](std::string const& s) { return parseBase58<Seed>(s); }},
        {jss::seed_hex.c_str(), [](std::string const& s) {
             uint128 i;
             if (i.parseHex(s))
                 return std::optional<Seed>(Slice(i.data(), i.size()));
             return std::optional<Seed>{};
         }}};

    // Identify which seed type is in use.
    seed_match_t const* seedType = nullptr;
    int count = 0;
    for (auto const& t : seedTypes)
    {
        if (params.isMember(t.first))
        {
            ++count;
            seedType = &t;
        }
    }

    if (count != 1)
    {
        error = RPC::make_param_error(
            "Exactly one of the following must be specified: " +
            std::string(jss::passphrase) + ", " + std::string(jss::seed) +
            " or " + std::string(jss::seed_hex));
        return std::nullopt;
    }

    // Make sure a string is present
    auto const& param = params[seedType->first];
    if (!param.isString())
    {
        error = RPC::expected_field_error(seedType->first, "string");
        return std::nullopt;
    }

    auto const fieldContents = param.asString();

    // Convert string to seed.
    std::optional<Seed> seed = seedType->second(fieldContents);

    if (!seed)
        error = rpcError(rpcBAD_SEED);

    return seed;
}

std::optional<std::pair<PublicKey, SecretKey>>
keypairForSignature(
    Json::Value const& params,
    Json::Value& error,
    unsigned int apiVersion)
{
    bool const has_key_type = params.isMember(jss::key_type);

    // All of the secret types we allow, but only one at a time.
    static char const* const secretTypes[]{
        jss::passphrase.c_str(),
        jss::secret.c_str(),
        jss::seed.c_str(),
        jss::seed_hex.c_str()};

    // Identify which secret type is in use.
    char const* secretType = nullptr;
    int count = 0;
    for (auto t : secretTypes)
    {
        if (params.isMember(t))
        {
            ++count;
            secretType = t;
        }
    }

    if (count == 0 || secretType == nullptr)
    {
        error = RPC::missing_field_error(jss::secret);
        return {};
    }

    if (count > 1)
    {
        error = RPC::make_param_error(
            "Exactly one of the following must be specified: " +
            std::string(jss::passphrase) + ", " + std::string(jss::secret) +
            ", " + std::string(jss::seed) + " or " +
            std::string(jss::seed_hex));
        return {};
    }

    std::optional<KeyType> keyType;
    std::optional<Seed> seed;

    if (has_key_type)
    {
        if (!params[jss::key_type].isString())
        {
            error = RPC::expected_field_error(jss::key_type, "string");
            return {};
        }

        keyType = keyTypeFromString(params[jss::key_type].asString());

        if (!keyType)
        {
            if (apiVersion > 1u)
                error = RPC::make_error(rpcBAD_KEY_TYPE);
            else
                error = RPC::invalid_field_error(jss::key_type);
            return {};
        }

        // using strcmp as pointers may not match (see
        // https://developercommunity.visualstudio.com/t/assigning-constexpr-char--to-static-cha/10021357?entry=problem)
        if (strcmp(secretType, jss::secret.c_str()) == 0)
        {
            error = RPC::make_param_error(
                "The secret field is not allowed if " +
                std::string(jss::key_type) + " is used.");
            return {};
        }
    }

    // ripple-lib encodes seed used to generate an Ed25519 wallet in a
    // non-standard way. While we never encode seeds that way, we try
    // to detect such keys to avoid user confusion.
    // using strcmp as pointers may not match (see
    // https://developercommunity.visualstudio.com/t/assigning-constexpr-char--to-static-cha/10021357?entry=problem)
    if (strcmp(secretType, jss::seed_hex.c_str()) != 0)
    {
        seed = RPC::parseRippleLibSeed(params[secretType]);

        if (seed)
        {
            // If the user passed in an Ed25519 seed but *explicitly*
            // requested another key type, return an error.
            if (keyType.value_or(KeyType::ed25519) != KeyType::ed25519)
            {
                error = RPC::make_error(
                    rpcBAD_SEED, "Specified seed is for an Ed25519 wallet.");
                return {};
            }

            keyType = KeyType::ed25519;
        }
    }

    if (!keyType)
        keyType = KeyType::secp256k1;

    if (!seed)
    {
        if (has_key_type)
            seed = getSeedFromRPC(params, error);
        else
        {
            if (!params[jss::secret].isString())
            {
                error = RPC::expected_field_error(jss::secret, "string");
                return {};
            }

            seed = parseGenericSeed(params[jss::secret].asString());
        }
    }

    if (!seed)
    {
        if (!contains_error(error))
        {
            error = RPC::make_error(
                rpcBAD_SEED, RPC::invalid_field_message(secretType));
        }

        return {};
    }

    if (keyType != KeyType::secp256k1 && keyType != KeyType::ed25519)
        LogicError("keypairForSignature: invalid key type");

    return generateKeyPair(*keyType, *seed);
}

std::pair<RPC::Status, LedgerEntryType>
chooseLedgerEntryType(Json::Value const& params)
{
    std::pair<RPC::Status, LedgerEntryType> result{RPC::Status::OK, ltANY};
    if (params.isMember(jss::type))
    {
        static constexpr auto types = std::to_array<
            std::tuple<char const*, char const*, LedgerEntryType>>({
#pragma push_macro("LEDGER_ENTRY")
#undef LEDGER_ENTRY

#define LEDGER_ENTRY(tag, value, name, rpcName, fields) \
    {jss::name, jss::rpcName, tag},

#include <xrpl/protocol/detail/ledger_entries.macro>

#undef LEDGER_ENTRY
#pragma pop_macro("LEDGER_ENTRY")
        });

        auto const& p = params[jss::type];
        if (!p.isString())
        {
            result.first = RPC::Status{
                rpcINVALID_PARAMS, "Invalid field 'type', not string."};
            XRPL_ASSERT(
                result.first.type() == RPC::Status::Type::error_code_i,
                "ripple::RPC::chooseLedgerEntryType : first valid result type");
            return result;
        }

        // Use the passed in parameter to find a ledger type based on matching
        // against the canonical name (case-insensitive) or the RPC name
        // (case-sensitive).
        auto const filter = p.asString();
        auto const iter =
            std::ranges::find_if(types, [&filter](decltype(types.front())& t) {
                return boost::iequals(std::get<0>(t), filter) ||
                    std::get<1>(t) == filter;
            });
        if (iter == types.end())
        {
            result.first =
                RPC::Status{rpcINVALID_PARAMS, "Invalid field 'type'."};
            XRPL_ASSERT(
                result.first.type() == RPC::Status::Type::error_code_i,
                "ripple::RPC::chooseLedgerEntryType : second valid result "
                "type");
            return result;
        }
        result.second = std::get<2>(*iter);
    }
    return result;
}

bool
isAccountObjectsValidType(LedgerEntryType const& type)
{
    switch (type)
    {
        case LedgerEntryType::ltAMENDMENTS:
        case LedgerEntryType::ltDIR_NODE:
        case LedgerEntryType::ltFEE_SETTINGS:
        case LedgerEntryType::ltLEDGER_HASHES:
        case LedgerEntryType::ltNEGATIVE_UNL:
            return false;
        default:
            return true;
    }
}

beast::SemanticVersion const firstVersion("1.0.0");
beast::SemanticVersion const goodVersion("1.0.0");
beast::SemanticVersion const lastVersion("1.0.0");

unsigned int
getAPIVersionNumber(Json::Value const& jv, bool betaEnabled)
{
    static Json::Value const minVersion(RPC::apiMinimumSupportedVersion);
    static Json::Value const invalidVersion(RPC::apiInvalidVersion);

    Json::Value const maxVersion(
        betaEnabled ? RPC::apiBetaVersion : RPC::apiMaximumSupportedVersion);
    Json::Value requestedVersion(RPC::apiVersionIfUnspecified);
    if (jv.isObject())
    {
        requestedVersion = jv.get(jss::api_version, requestedVersion);
    }
    if (!(requestedVersion.isInt() || requestedVersion.isUInt()) ||
        requestedVersion < minVersion || requestedVersion > maxVersion)
    {
        requestedVersion = invalidVersion;
    }
    return requestedVersion.asUInt();
}

std::variant<std::shared_ptr<Ledger const>, Json::Value>
getLedgerByContext(RPC::JsonContext& context)
{
    auto const hasHash = context.params.isMember(jss::ledger_hash);
    auto const hasIndex = context.params.isMember(jss::ledger_index);
    std::uint32_t ledgerIndex = 0;

    auto& ledgerMaster = context.app.getLedgerMaster();
    LedgerHash ledgerHash;

    if ((hasHash && hasIndex) || !(hasHash || hasIndex))
    {
        return RPC::make_param_error(
            "Exactly one of ledger_hash and ledger_index can be set.");
    }

    context.loadType = Resource::feeHeavyBurdenRPC;

    if (hasHash)
    {
        auto const& jsonHash = context.params[jss::ledger_hash];
        if (!jsonHash.isString() || !ledgerHash.parseHex(jsonHash.asString()))
            return RPC::invalid_field_error(jss::ledger_hash);
    }
    else
    {
        auto const& jsonIndex = context.params[jss::ledger_index];
        if (!jsonIndex.isInt())
            return RPC::invalid_field_error(jss::ledger_index);

        // We need a validated ledger to get the hash from the sequence
        if (ledgerMaster.getValidatedLedgerAge() >
            RPC::Tuning::maxValidatedLedgerAge)
        {
            if (context.apiVersion == 1)
                return rpcError(rpcNO_CURRENT);
            return rpcError(rpcNOT_SYNCED);
        }

        ledgerIndex = jsonIndex.asInt();
        auto ledger = ledgerMaster.getValidatedLedger();

        if (ledgerIndex >= ledger->info().seq)
            return RPC::make_param_error("Ledger index too large");
        if (ledgerIndex <= 0)
            return RPC::make_param_error("Ledger index too small");

        auto const j = context.app.journal("RPCHandler");
        // Try to get the hash of the desired ledger from the validated
        // ledger
        auto neededHash = hashOfSeq(*ledger, ledgerIndex, j);
        if (!neededHash)
        {
            // Find a ledger more likely to have the hash of the desired
            // ledger
            auto const refIndex = getCandidateLedger(ledgerIndex);
            auto refHash = hashOfSeq(*ledger, refIndex, j);
            XRPL_ASSERT(
                refHash,
                "ripple::RPC::getLedgerByContext : nonzero ledger hash");

            ledger = ledgerMaster.getLedgerByHash(*refHash);
            if (!ledger)
            {
                // We don't have the ledger we need to figure out which
                // ledger they want. Try to get it.

                if (auto il = context.app.getInboundLedgers().acquire(
                        *refHash, refIndex, InboundLedger::Reason::GENERIC))
                {
                    Json::Value jvResult = RPC::make_error(
                        rpcLGR_NOT_FOUND,
                        "acquiring ledger containing requested index");
                    jvResult[jss::acquiring] =
                        getJson(LedgerFill(*il, &context));
                    return jvResult;
                }

                if (auto il = context.app.getInboundLedgers().find(*refHash))
                {
                    Json::Value jvResult = RPC::make_error(
                        rpcLGR_NOT_FOUND,
                        "acquiring ledger containing requested index");
                    jvResult[jss::acquiring] = il->getJson(0);
                    return jvResult;
                }

                // Likely the app is shutting down
                return Json::Value();
            }

            neededHash = hashOfSeq(*ledger, ledgerIndex, j);
        }
        XRPL_ASSERT(
            neededHash,
            "ripple::RPC::getLedgerByContext : nonzero needed hash");
        ledgerHash = neededHash ? *neededHash : beast::zero;  // kludge
    }

    // Try to get the desired ledger
    // Verify all nodes even if we think we have it
    auto ledger = context.app.getInboundLedgers().acquire(
        ledgerHash, ledgerIndex, InboundLedger::Reason::GENERIC);

    // In standalone mode, accept the ledger from the ledger cache
    if (!ledger && context.app.config().standalone())
        ledger = ledgerMaster.getLedgerByHash(ledgerHash);

    if (ledger)
        return ledger;

    if (auto il = context.app.getInboundLedgers().find(ledgerHash))
        return il->getJson(0);

    return RPC::make_error(
        rpcNOT_READY, "findCreate failed to return an inbound ledger");
}

}  // namespace RPC
}  // namespace ripple
