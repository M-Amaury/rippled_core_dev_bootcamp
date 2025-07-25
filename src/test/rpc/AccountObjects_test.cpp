//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2016 Ripple Labs Inc.

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

#include <test/jtx.h>
#include <test/jtx/AMM.h>
#include <test/jtx/xchain_bridge.h>

#include <xrpld/app/tx/detail/NFTokenMint.h>

#include <xrpl/json/json_reader.h>
#include <xrpl/json/json_value.h>
#include <xrpl/json/to_string.h>
#include <xrpl/protocol/jss.h>
#include <xrpl/protocol/nft.h>

#include <boost/utility/string_ref.hpp>

#include <algorithm>

namespace ripple {
namespace test {

static char const* bobs_account_objects[] = {
    R"json({
  "Account" : "rPMh7Pi9ct699iZUTWaytJUoHcJ7cgyziK",
  "BookDirectory" : "50AD0A9E54D2B381288D535EB724E4275FFBF41580D28A925D038D7EA4C68000",
  "BookNode" : "0",
  "Flags" : 65536,
  "LedgerEntryType" : "Offer",
  "OwnerNode" : "0",
  "Sequence" : 6,
  "TakerGets" : {
    "currency" : "USD",
    "issuer" : "rPMh7Pi9ct699iZUTWaytJUoHcJ7cgyziK",
    "value" : "1"
  },
  "TakerPays" : "100000000",
  "index" : "29665262716C19830E26AEEC0916E476FC7D8EF195FF3B4F06829E64F82A3B3E"
})json",
    R"json({
    "Balance" : {
        "currency" : "USD",
        "issuer" : "rrrrrrrrrrrrrrrrrrrrBZbvji",
        "value" : "-1000"
    },
    "Flags" : 131072,
    "HighLimit" : {
        "currency" : "USD",
        "issuer" : "rPMh7Pi9ct699iZUTWaytJUoHcJ7cgyziK",
        "value" : "1000"
    },
    "HighNode" : "0",
    "LedgerEntryType" : "RippleState",
    "LowLimit" : {
        "currency" : "USD",
        "issuer" : "r9cZvwKU3zzuZK9JFovGg1JC5n7QiqNL8L",
        "value" : "0"
    },
    "LowNode" : "0",
    "index" : "D13183BCFFC9AAC9F96AEBB5F66E4A652AD1F5D10273AEB615478302BEBFD4A4"
})json",
    R"json({
    "Balance" : {
        "currency" : "USD",
        "issuer" : "rrrrrrrrrrrrrrrrrrrrBZbvji",
        "value" : "-1000"
    },
    "Flags" : 131072,
    "HighLimit" : {
        "currency" : "USD",
        "issuer" : "rPMh7Pi9ct699iZUTWaytJUoHcJ7cgyziK",
        "value" : "1000"
    },
    "HighNode" : "0",
    "LedgerEntryType" : "RippleState",
    "LowLimit" : {
        "currency" : "USD",
        "issuer" : "r32rQHyesiTtdWFU7UJVtff4nCR5SHCbJW",
        "value" : "0"
    },
    "LowNode" : "0",
    "index" : "D89BC239086183EB9458C396E643795C1134963E6550E682A190A5F021766D43"
})json",
    R"json({
    "Account" : "rPMh7Pi9ct699iZUTWaytJUoHcJ7cgyziK",
    "BookDirectory" : "B025997A323F5C3E03DDF1334471F5984ABDE31C59D463525D038D7EA4C68000",
    "BookNode" : "0",
    "Flags" : 65536,
    "LedgerEntryType" : "Offer",
    "OwnerNode" : "0",
    "Sequence" : 7,
    "TakerGets" : {
        "currency" : "USD",
        "issuer" : "r32rQHyesiTtdWFU7UJVtff4nCR5SHCbJW",
        "value" : "1"
    },
    "TakerPays" : "100000000",
    "index" : "F03ABE26CB8C5F4AFB31A86590BD25C64C5756FCE5CE9704C27AFE291A4A29A1"
})json"};

class AccountObjects_test : public beast::unit_test::suite
{
public:
    void
    testErrors()
    {
        testcase("error cases");

        using namespace jtx;
        Env env(*this);

        // test error on no account
        {
            Json::Value params;
            auto resp = env.rpc("json", "account_objects", to_string(params));
            BEAST_EXPECT(
                resp[jss::result][jss::error_message] ==
                "Missing field 'account'.");
        }
        // test account non-string
        {
            auto testInvalidAccountParam = [&](auto const& param) {
                Json::Value params;
                params[jss::account] = param;
                auto jrr = env.rpc(
                    "json", "account_objects", to_string(params))[jss::result];
                BEAST_EXPECT(jrr[jss::error] == "invalidParams");
                BEAST_EXPECT(
                    jrr[jss::error_message] == "Invalid field 'account'.");
            };

            testInvalidAccountParam(1);
            testInvalidAccountParam(1.1);
            testInvalidAccountParam(true);
            testInvalidAccountParam(Json::Value(Json::nullValue));
            testInvalidAccountParam(Json::Value(Json::objectValue));
            testInvalidAccountParam(Json::Value(Json::arrayValue));
        }
        // test error on  malformed account string.
        {
            Json::Value params;
            params[jss::account] =
                "n94JNrQYkDrpt62bbSR7nVEhdyAvcJXRAsjEkFYyqRkh9SUTYEqV";
            auto resp = env.rpc("json", "account_objects", to_string(params));
            BEAST_EXPECT(
                resp[jss::result][jss::error_message] == "Account malformed.");
        }
        // test error on account that's not in the ledger.
        {
            Json::Value params;
            params[jss::account] = Account{"bogie"}.human();
            auto resp = env.rpc("json", "account_objects", to_string(params));
            BEAST_EXPECT(
                resp[jss::result][jss::error_message] == "Account not found.");
        }
        Account const bob{"bob"};
        // test error on large ledger_index.
        {
            Json::Value params;
            params[jss::account] = bob.human();
            params[jss::ledger_index] = 10;
            auto resp = env.rpc("json", "account_objects", to_string(params));
            BEAST_EXPECT(
                resp[jss::result][jss::error_message] == "ledgerNotFound");
        }

        env.fund(XRP(1000), bob);
        // test error on type param not a string
        {
            Json::Value params;
            params[jss::account] = bob.human();
            params[jss::type] = 10;
            auto resp = env.rpc("json", "account_objects", to_string(params));
            BEAST_EXPECT(
                resp[jss::result][jss::error_message] ==
                "Invalid field 'type', not string.");
        }
        // test error on type param not a valid type
        {
            Json::Value params;
            params[jss::account] = bob.human();
            params[jss::type] = "expedited";
            auto resp = env.rpc("json", "account_objects", to_string(params));
            BEAST_EXPECT(
                resp[jss::result][jss::error_message] ==
                "Invalid field 'type'.");
        }
        // test error on limit -ve
        {
            Json::Value params;
            params[jss::account] = bob.human();
            params[jss::limit] = -1;
            auto resp = env.rpc("json", "account_objects", to_string(params));
            BEAST_EXPECT(
                resp[jss::result][jss::error_message] ==
                "Invalid field 'limit', not unsigned integer.");
        }
        // test errors on marker
        {
            Account const gw{"G"};
            env.fund(XRP(1000), gw);
            auto const USD = gw["USD"];
            env.trust(USD(1000), bob);
            env(pay(gw, bob, XRP(1)));
            env(offer(bob, XRP(100), bob["USD"](1)), txflags(tfPassive));

            Json::Value params;
            params[jss::account] = bob.human();
            params[jss::limit] = 1;
            auto resp = env.rpc("json", "account_objects", to_string(params));

            auto resume_marker = resp[jss::result][jss::marker];
            std::string mark = to_string(resume_marker);
            params[jss::marker] = 10;
            resp = env.rpc("json", "account_objects", to_string(params));
            BEAST_EXPECT(
                resp[jss::result][jss::error_message] ==
                "Invalid field 'marker', not string.");

            params[jss::marker] = "This is a string with no comma";
            resp = env.rpc("json", "account_objects", to_string(params));
            BEAST_EXPECT(
                resp[jss::result][jss::error_message] ==
                "Invalid field 'marker'.");

            params[jss::marker] = "This string has a comma, but is not hex";
            resp = env.rpc("json", "account_objects", to_string(params));
            BEAST_EXPECT(
                resp[jss::result][jss::error_message] ==
                "Invalid field 'marker'.");

            params[jss::marker] = std::string(&mark[1U], 64);
            resp = env.rpc("json", "account_objects", to_string(params));
            BEAST_EXPECT(
                resp[jss::result][jss::error_message] ==
                "Invalid field 'marker'.");

            params[jss::marker] = std::string(&mark[1U], 65);
            resp = env.rpc("json", "account_objects", to_string(params));
            BEAST_EXPECT(
                resp[jss::result][jss::error_message] ==
                "Invalid field 'marker'.");

            params[jss::marker] = std::string(&mark[1U], 65) + "not hex";
            resp = env.rpc("json", "account_objects", to_string(params));
            BEAST_EXPECT(
                resp[jss::result][jss::error_message] ==
                "Invalid field 'marker'.");

            // Should this be an error?
            // A hex digit is absent from the end of marker.
            // No account objects returned.
            params[jss::marker] = std::string(&mark[1U], 128);
            resp = env.rpc("json", "account_objects", to_string(params));
            BEAST_EXPECT(resp[jss::result][jss::account_objects].size() == 0);
        }
    }

    void
    testUnsteppedThenStepped()
    {
        testcase("unsteppedThenStepped");

        using namespace jtx;
        Env env(*this);

        Account const gw1{"G1"};
        Account const gw2{"G2"};
        Account const bob{"bob"};

        auto const USD1 = gw1["USD"];
        auto const USD2 = gw2["USD"];

        env.fund(XRP(1000), gw1, gw2, bob);
        env.trust(USD1(1000), bob);
        env.trust(USD2(1000), bob);

        env(pay(gw1, bob, USD1(1000)));
        env(pay(gw2, bob, USD2(1000)));

        env(offer(bob, XRP(100), bob["USD"](1)), txflags(tfPassive));
        env(offer(bob, XRP(100), USD1(1)), txflags(tfPassive));

        Json::Value bobj[4];
        for (int i = 0; i < 4; ++i)
            Json::Reader{}.parse(bobs_account_objects[i], bobj[i]);

        // test 'unstepped'
        // i.e. request account objects without explicit limit/marker paging
        {
            Json::Value params;
            params[jss::account] = bob.human();
            auto resp = env.rpc("json", "account_objects", to_string(params));
            BEAST_EXPECT(!resp.isMember(jss::marker));

            BEAST_EXPECT(resp[jss::result][jss::account_objects].size() == 4);
            for (int i = 0; i < 4; ++i)
            {
                auto& aobj = resp[jss::result][jss::account_objects][i];
                aobj.removeMember("PreviousTxnID");
                aobj.removeMember("PreviousTxnLgrSeq");
                BEAST_EXPECT(aobj == bobj[i]);
            }
        }
        // test request with type parameter as filter, unstepped
        {
            Json::Value params;
            params[jss::account] = bob.human();
            params[jss::type] = jss::state;
            auto resp = env.rpc("json", "account_objects", to_string(params));
            BEAST_EXPECT(!resp.isMember(jss::marker));

            BEAST_EXPECT(resp[jss::result][jss::account_objects].size() == 2);
            for (int i = 0; i < 2; ++i)
            {
                auto& aobj = resp[jss::result][jss::account_objects][i];
                aobj.removeMember("PreviousTxnID");
                aobj.removeMember("PreviousTxnLgrSeq");
                BEAST_EXPECT(aobj == bobj[i + 1]);
            }
        }
        // test stepped one-at-a-time with limit=1, resume from prev marker
        {
            Json::Value params;
            params[jss::account] = bob.human();
            params[jss::limit] = 1;
            for (int i = 0; i < 4; ++i)
            {
                auto resp =
                    env.rpc("json", "account_objects", to_string(params));
                auto& aobjs = resp[jss::result][jss::account_objects];
                BEAST_EXPECT(aobjs.size() == 1);
                auto& aobj = aobjs[0U];
                if (i < 3)
                    BEAST_EXPECT(resp[jss::result][jss::limit] == 1);
                else
                    BEAST_EXPECT(!resp[jss::result].isMember(jss::limit));

                aobj.removeMember("PreviousTxnID");
                aobj.removeMember("PreviousTxnLgrSeq");

                BEAST_EXPECT(aobj == bobj[i]);

                params[jss::marker] = resp[jss::result][jss::marker];
            }
        }
    }

    void
    testUnsteppedThenSteppedWithNFTs()
    {
        // The preceding test case, unsteppedThenStepped(), found a bug in the
        // support for NFToken Pages.  So we're leaving that test alone when
        // adding tests to exercise NFTokenPages.
        testcase("unsteppedThenSteppedWithNFTs");

        using namespace jtx;
        Env env(*this);

        Account const gw1{"G1"};
        Account const gw2{"G2"};
        Account const bob{"bob"};

        auto const USD1 = gw1["USD"];
        auto const USD2 = gw2["USD"];

        env.fund(XRP(1000), gw1, gw2, bob);
        env.close();

        // Check behavior if there are no account objects.
        {
            // Unpaged
            Json::Value params;
            params[jss::account] = bob.human();
            auto resp = env.rpc("json", "account_objects", to_string(params));
            BEAST_EXPECT(!resp.isMember(jss::marker));
            BEAST_EXPECT(resp[jss::result][jss::account_objects].size() == 0);

            // Limit == 1
            params[jss::limit] = 1;
            resp = env.rpc("json", "account_objects", to_string(params));
            BEAST_EXPECT(!resp.isMember(jss::marker));
            BEAST_EXPECT(resp[jss::result][jss::account_objects].size() == 0);
        }

        // Check behavior if there are only NFTokens.
        env(token::mint(bob, 0u), txflags(tfTransferable));
        env.close();

        // test 'unstepped'
        // i.e. request account objects without explicit limit/marker paging
        Json::Value unpaged;
        {
            Json::Value params;
            params[jss::account] = bob.human();
            auto resp = env.rpc("json", "account_objects", to_string(params));
            BEAST_EXPECT(!resp.isMember(jss::marker));

            unpaged = resp[jss::result][jss::account_objects];
            BEAST_EXPECT(unpaged.size() == 1);
        }
        // test request with type parameter as filter, unstepped
        {
            Json::Value params;
            params[jss::account] = bob.human();
            params[jss::type] = jss::nft_page;
            auto resp = env.rpc("json", "account_objects", to_string(params));
            BEAST_EXPECT(!resp.isMember(jss::marker));
            Json::Value& aobjs = resp[jss::result][jss::account_objects];
            BEAST_EXPECT(aobjs.size() == 1);
            BEAST_EXPECT(
                aobjs[0u][sfLedgerEntryType.jsonName] == jss::NFTokenPage);
            BEAST_EXPECT(aobjs[0u][sfNFTokens.jsonName].size() == 1);
        }
        // test stepped one-at-a-time with limit=1, resume from prev marker
        {
            Json::Value params;
            params[jss::account] = bob.human();
            params[jss::limit] = 1;

            Json::Value resp =
                env.rpc("json", "account_objects", to_string(params));
            Json::Value& aobjs = resp[jss::result][jss::account_objects];
            BEAST_EXPECT(aobjs.size() == 1);
            auto& aobj = aobjs[0U];
            BEAST_EXPECT(!resp[jss::result].isMember(jss::limit));
            BEAST_EXPECT(!resp[jss::result].isMember(jss::marker));

            BEAST_EXPECT(aobj == unpaged[0u]);
        }

        // Add more objects in addition to the NFToken Page.
        env.trust(USD1(1000), bob);
        env.trust(USD2(1000), bob);

        env(pay(gw1, bob, USD1(1000)));
        env(pay(gw2, bob, USD2(1000)));

        env(offer(bob, XRP(100), bob["USD"](1)), txflags(tfPassive));
        env(offer(bob, XRP(100), USD1(1)), txflags(tfPassive));
        env.close();

        // test 'unstepped'
        {
            Json::Value params;
            params[jss::account] = bob.human();
            auto resp = env.rpc("json", "account_objects", to_string(params));
            BEAST_EXPECT(!resp.isMember(jss::marker));

            unpaged = resp[jss::result][jss::account_objects];
            BEAST_EXPECT(unpaged.size() == 5);
        }
        // test request with type parameter as filter, unstepped
        {
            Json::Value params;
            params[jss::account] = bob.human();
            params[jss::type] = jss::nft_page;
            auto resp = env.rpc("json", "account_objects", to_string(params));
            BEAST_EXPECT(!resp.isMember(jss::marker));
            Json::Value& aobjs = resp[jss::result][jss::account_objects];
            BEAST_EXPECT(aobjs.size() == 1);
            BEAST_EXPECT(
                aobjs[0u][sfLedgerEntryType.jsonName] == jss::NFTokenPage);
            BEAST_EXPECT(aobjs[0u][sfNFTokens.jsonName].size() == 1);
        }
        // test stepped one-at-a-time with limit=1, resume from prev marker
        {
            Json::Value params;
            params[jss::account] = bob.human();
            params[jss::limit] = 1;
            for (int i = 0; i < 5; ++i)
            {
                Json::Value resp =
                    env.rpc("json", "account_objects", to_string(params));
                Json::Value& aobjs = resp[jss::result][jss::account_objects];
                BEAST_EXPECT(aobjs.size() == 1);
                auto& aobj = aobjs[0U];
                if (i < 4)
                {
                    BEAST_EXPECT(resp[jss::result][jss::limit] == 1);
                    BEAST_EXPECT(resp[jss::result].isMember(jss::marker));
                }
                else
                {
                    BEAST_EXPECT(!resp[jss::result].isMember(jss::limit));
                    BEAST_EXPECT(!resp[jss::result].isMember(jss::marker));
                }

                BEAST_EXPECT(aobj == unpaged[i]);

                params[jss::marker] = resp[jss::result][jss::marker];
            }
        }

        // Make sure things still work if there is more than 1 NFT Page.
        for (int i = 0; i < 32; ++i)
        {
            env(token::mint(bob, 0u), txflags(tfTransferable));
            env.close();
        }
        // test 'unstepped'
        {
            Json::Value params;
            params[jss::account] = bob.human();
            auto resp = env.rpc("json", "account_objects", to_string(params));
            BEAST_EXPECT(!resp.isMember(jss::marker));

            unpaged = resp[jss::result][jss::account_objects];
            BEAST_EXPECT(unpaged.size() == 6);
        }
        // test request with type parameter as filter, unstepped
        {
            Json::Value params;
            params[jss::account] = bob.human();
            params[jss::type] = jss::nft_page;
            auto resp = env.rpc("json", "account_objects", to_string(params));
            BEAST_EXPECT(!resp.isMember(jss::marker));
            Json::Value& aobjs = resp[jss::result][jss::account_objects];
            BEAST_EXPECT(aobjs.size() == 2);
        }
        // test stepped one-at-a-time with limit=1, resume from prev marker
        {
            Json::Value params;
            params[jss::account] = bob.human();
            params[jss::limit] = 1;
            for (int i = 0; i < 6; ++i)
            {
                Json::Value resp =
                    env.rpc("json", "account_objects", to_string(params));
                Json::Value& aobjs = resp[jss::result][jss::account_objects];
                BEAST_EXPECT(aobjs.size() == 1);
                auto& aobj = aobjs[0U];
                if (i < 5)
                {
                    BEAST_EXPECT(resp[jss::result][jss::limit] == 1);
                    BEAST_EXPECT(resp[jss::result].isMember(jss::marker));
                }
                else
                {
                    BEAST_EXPECT(!resp[jss::result].isMember(jss::limit));
                    BEAST_EXPECT(!resp[jss::result].isMember(jss::marker));
                }

                BEAST_EXPECT(aobj == unpaged[i]);

                params[jss::marker] = resp[jss::result][jss::marker];
            }
        }
    }

    void
    testObjectTypes()
    {
        testcase("object types");

        // Give gw a bunch of ledger objects and make sure we can retrieve
        // them by type.
        using namespace jtx;

        Account const alice{"alice"};
        Account const gw{"gateway"};
        auto const USD = gw["USD"];

        auto const features = supported_amendments() | featureXChainBridge |
            featurePermissionedDomains;
        Env env(*this, features);

        // Make a lambda we can use to get "account_objects" easily.
        auto acctObjs = [&env](
                            AccountID const& acct,
                            std::optional<Json::StaticString> const& type,
                            std::optional<std::uint16_t> limit = std::nullopt,
                            std::optional<std::string> marker = std::nullopt) {
            Json::Value params;
            params[jss::account] = to_string(acct);
            if (type)
                params[jss::type] = *type;
            if (limit)
                params[jss::limit] = *limit;
            if (marker)
                params[jss::marker] = *marker;
            params[jss::ledger_index] = "validated";
            return env.rpc("json", "account_objects", to_string(params));
        };

        // Make a lambda that easily identifies the size of account objects.
        auto acctObjsIsSize = [](Json::Value const& resp, unsigned size) {
            return resp[jss::result][jss::account_objects].isArray() &&
                (resp[jss::result][jss::account_objects].size() == size);
        };

        // Make a lambda that checks if the response has error for invalid type
        auto acctObjsTypeIsInvalid = [](Json::Value const& resp) {
            return resp[jss::result].isMember(jss::error) &&
                resp[jss::result][jss::error_message] ==
                "Invalid field \'type\'.";
        };

        env.fund(XRP(10000), gw, alice);
        env.close();

        // Since the account is empty now, all account objects should come
        // back empty.
        BEAST_EXPECT(acctObjsIsSize(acctObjs(gw, jss::account), 0));
        BEAST_EXPECT(acctObjsIsSize(acctObjs(gw, jss::check), 0));
        BEAST_EXPECT(acctObjsIsSize(acctObjs(gw, jss::deposit_preauth), 0));
        BEAST_EXPECT(acctObjsIsSize(acctObjs(gw, jss::escrow), 0));
        BEAST_EXPECT(acctObjsIsSize(acctObjs(gw, jss::nft_page), 0));
        BEAST_EXPECT(acctObjsIsSize(acctObjs(gw, jss::offer), 0));
        BEAST_EXPECT(acctObjsIsSize(acctObjs(gw, jss::payment_channel), 0));
        BEAST_EXPECT(acctObjsIsSize(acctObjs(gw, jss::signer_list), 0));
        BEAST_EXPECT(acctObjsIsSize(acctObjs(gw, jss::state), 0));
        BEAST_EXPECT(acctObjsIsSize(acctObjs(gw, jss::ticket), 0));
        BEAST_EXPECT(acctObjsIsSize(acctObjs(gw, jss::amm), 0));
        BEAST_EXPECT(acctObjsIsSize(acctObjs(gw, jss::did), 0));
        BEAST_EXPECT(acctObjsIsSize(acctObjs(gw, jss::permissioned_domain), 0));

        // we expect invalid field type reported for the following types
        BEAST_EXPECT(acctObjsTypeIsInvalid(acctObjs(gw, jss::amendments)));
        BEAST_EXPECT(acctObjsTypeIsInvalid(acctObjs(gw, jss::directory)));
        BEAST_EXPECT(acctObjsTypeIsInvalid(acctObjs(gw, jss::fee)));
        BEAST_EXPECT(acctObjsTypeIsInvalid(acctObjs(gw, jss::hashes)));
        BEAST_EXPECT(acctObjsTypeIsInvalid(acctObjs(gw, jss::NegativeUNL)));

        // gw mints an NFT so we can find it.
        uint256 const nftID{token::getNextID(env, gw, 0u, tfTransferable)};
        env(token::mint(gw, 0u), txflags(tfTransferable));
        env.close();
        {
            // Find the NFToken page and make sure it's the right one.
            Json::Value const resp = acctObjs(gw, jss::nft_page);
            BEAST_EXPECT(acctObjsIsSize(resp, 1));

            auto const& nftPage = resp[jss::result][jss::account_objects][0u];
            BEAST_EXPECT(nftPage[sfNFTokens.jsonName].size() == 1);
            BEAST_EXPECT(
                nftPage[sfNFTokens.jsonName][0u][sfNFToken.jsonName]
                       [sfNFTokenID.jsonName] == to_string(nftID));
        }

        // Set up a trust line so we can find it.
        env.trust(USD(1000), alice);
        env.close();
        env(pay(gw, alice, USD(5)));
        env.close();
        {
            // Find the trustline and make sure it's the right one.
            Json::Value const resp = acctObjs(gw, jss::state);
            BEAST_EXPECT(acctObjsIsSize(resp, 1));

            auto const& state = resp[jss::result][jss::account_objects][0u];
            BEAST_EXPECT(state[sfBalance.jsonName][jss::value].asInt() == -5);
            BEAST_EXPECT(
                state[sfHighLimit.jsonName][jss::value].asUInt() == 1000);
        }
        // gw writes a check for USD(10) to alice.
        env(check::create(gw, alice, USD(10)));
        env.close();
        {
            // Find the check.
            Json::Value const resp = acctObjs(gw, jss::check);
            BEAST_EXPECT(acctObjsIsSize(resp, 1));

            auto const& check = resp[jss::result][jss::account_objects][0u];
            BEAST_EXPECT(check[sfAccount.jsonName] == gw.human());
            BEAST_EXPECT(check[sfDestination.jsonName] == alice.human());
            BEAST_EXPECT(check[sfSendMax.jsonName][jss::value].asUInt() == 10);
        }
        // gw preauthorizes payments from alice.
        env(deposit::auth(gw, alice));
        env.close();
        {
            // Find the preauthorization.
            Json::Value const resp = acctObjs(gw, jss::deposit_preauth);
            BEAST_EXPECT(acctObjsIsSize(resp, 1));

            auto const& preauth = resp[jss::result][jss::account_objects][0u];
            BEAST_EXPECT(preauth[sfAccount.jsonName] == gw.human());
            BEAST_EXPECT(preauth[sfAuthorize.jsonName] == alice.human());
        }
        {
            // gw creates an escrow that we can look for in the ledger.
            Json::Value jvEscrow;
            jvEscrow[jss::TransactionType] = jss::EscrowCreate;
            jvEscrow[jss::Account] = gw.human();
            jvEscrow[jss::Destination] = gw.human();
            jvEscrow[jss::Amount] = XRP(100).value().getJson(JsonOptions::none);
            jvEscrow[sfFinishAfter.jsonName] =
                env.now().time_since_epoch().count() + 1;
            env(jvEscrow);
            env.close();
        }
        {
            // Find the escrow.
            Json::Value const resp = acctObjs(gw, jss::escrow);
            BEAST_EXPECT(acctObjsIsSize(resp, 1));

            auto const& escrow = resp[jss::result][jss::account_objects][0u];
            BEAST_EXPECT(escrow[sfAccount.jsonName] == gw.human());
            BEAST_EXPECT(escrow[sfDestination.jsonName] == gw.human());
            BEAST_EXPECT(escrow[sfAmount.jsonName].asUInt() == 100'000'000);
        }

        {
            std::string const credentialType1 = "credential1";
            Account issuer("issuer");
            env.fund(XRP(5000), issuer);

            // gw creates an PermissionedDomain.
            env(pdomain::setTx(gw, {{issuer, credentialType1}}));
            env.close();

            // Find the PermissionedDomain.
            Json::Value const resp = acctObjs(gw, jss::permissioned_domain);
            BEAST_EXPECT(acctObjsIsSize(resp, 1));

            auto const& permissionedDomain =
                resp[jss::result][jss::account_objects][0u];
            BEAST_EXPECT(
                permissionedDomain.isMember(jss::Owner) &&
                (permissionedDomain[jss::Owner] == gw.human()));
            bool const check1 = BEAST_EXPECT(
                permissionedDomain.isMember(jss::AcceptedCredentials) &&
                permissionedDomain[jss::AcceptedCredentials].isArray() &&
                (permissionedDomain[jss::AcceptedCredentials].size() == 1) &&
                (permissionedDomain[jss::AcceptedCredentials][0u].isMember(
                    jss::Credential)));

            if (check1)
            {
                auto const& credential =
                    permissionedDomain[jss::AcceptedCredentials][0u]
                                      [jss::Credential];
                BEAST_EXPECT(
                    credential.isMember(sfIssuer.jsonName) &&
                    (credential[sfIssuer.jsonName] == issuer.human()));
                BEAST_EXPECT(
                    credential.isMember(sfCredentialType.jsonName) &&
                    (credential[sfCredentialType.jsonName] ==
                     strHex(credentialType1)));
            }
        }

        {
            // Create a bridge
            test::jtx::XChainBridgeObjects x;
            Env scEnv(*this, envconfig(), features);
            x.createScBridgeObjects(scEnv);

            auto scEnvAcctObjs = [&](Account const& acct, char const* type) {
                Json::Value params;
                params[jss::account] = acct.human();
                params[jss::type] = type;
                params[jss::ledger_index] = "validated";
                return scEnv.rpc("json", "account_objects", to_string(params));
            };

            Json::Value const resp =
                scEnvAcctObjs(Account::master, jss::bridge);

            BEAST_EXPECT(acctObjsIsSize(resp, 1));
            auto const& acct_bridge =
                resp[jss::result][jss::account_objects][0u];
            BEAST_EXPECT(
                acct_bridge[sfAccount.jsonName] == Account::master.human());
            BEAST_EXPECT(
                acct_bridge[sfLedgerEntryType.getJsonName()] == "Bridge");
            BEAST_EXPECT(
                acct_bridge[sfXChainClaimID.getJsonName()].asUInt() == 0);
            BEAST_EXPECT(
                acct_bridge[sfXChainAccountClaimCount.getJsonName()].asUInt() ==
                0);
            BEAST_EXPECT(
                acct_bridge[sfXChainAccountCreateCount.getJsonName()]
                    .asUInt() == 0);
            BEAST_EXPECT(
                acct_bridge[sfMinAccountCreateAmount.getJsonName()].asUInt() ==
                20000000);
            BEAST_EXPECT(
                acct_bridge[sfSignatureReward.getJsonName()].asUInt() ==
                1000000);
            BEAST_EXPECT(acct_bridge[sfXChainBridge.getJsonName()] == x.jvb);
        }
        {
            // Alice and Bob create a xchain sequence number that we can look
            // for in the ledger.
            test::jtx::XChainBridgeObjects x;
            Env scEnv(*this, envconfig(), features);
            x.createScBridgeObjects(scEnv);

            scEnv(
                xchain_create_claim_id(x.scAlice, x.jvb, x.reward, x.mcAlice));
            scEnv.close();
            scEnv(xchain_create_claim_id(x.scBob, x.jvb, x.reward, x.mcBob));
            scEnv.close();

            auto scEnvAcctObjs = [&](Account const& acct, char const* type) {
                Json::Value params;
                params[jss::account] = acct.human();
                params[jss::type] = type;
                params[jss::ledger_index] = "validated";
                return scEnv.rpc("json", "account_objects", to_string(params));
            };

            {
                // Find the xchain sequence number for Andrea.
                Json::Value const resp =
                    scEnvAcctObjs(x.scAlice, jss::xchain_owned_claim_id);
                BEAST_EXPECT(acctObjsIsSize(resp, 1));

                auto const& xchain_seq =
                    resp[jss::result][jss::account_objects][0u];
                BEAST_EXPECT(
                    xchain_seq[sfAccount.jsonName] == x.scAlice.human());
                BEAST_EXPECT(
                    xchain_seq[sfXChainClaimID.getJsonName()].asUInt() == 1);
            }
            {
                // and the one for Bob
                Json::Value const resp =
                    scEnvAcctObjs(x.scBob, jss::xchain_owned_claim_id);
                BEAST_EXPECT(acctObjsIsSize(resp, 1));

                auto const& xchain_seq =
                    resp[jss::result][jss::account_objects][0u];
                BEAST_EXPECT(xchain_seq[sfAccount.jsonName] == x.scBob.human());
                BEAST_EXPECT(
                    xchain_seq[sfXChainClaimID.getJsonName()].asUInt() == 2);
            }
        }
        {
            test::jtx::XChainBridgeObjects x;
            Env scEnv(*this, envconfig(), features);
            x.createScBridgeObjects(scEnv);
            auto const amt = XRP(1000);

            // send first batch of account create attestations, so the
            // xchain_create_account_claim_id should be present on the door
            // account (Account::master) to collect the signatures until a
            // quorum is reached
            scEnv(test::jtx::create_account_attestation(
                x.scAttester,
                x.jvb,
                x.mcCarol,
                amt,
                x.reward,
                x.payees[0],
                true,
                1,
                x.scuAlice,
                x.signers[0]));
            scEnv.close();

            auto scEnvAcctObjs = [&](Account const& acct, char const* type) {
                Json::Value params;
                params[jss::account] = acct.human();
                params[jss::type] = type;
                params[jss::ledger_index] = "validated";
                return scEnv.rpc("json", "account_objects", to_string(params));
            };

            {
                // Find the xchain_create_account_claim_id
                Json::Value const resp = scEnvAcctObjs(
                    Account::master, jss::xchain_owned_create_account_claim_id);
                BEAST_EXPECT(acctObjsIsSize(resp, 1));

                auto const& xchain_create_account_claim_id =
                    resp[jss::result][jss::account_objects][0u];
                BEAST_EXPECT(
                    xchain_create_account_claim_id[sfAccount.jsonName] ==
                    Account::master.human());
                BEAST_EXPECT(
                    xchain_create_account_claim_id[sfXChainAccountCreateCount
                                                       .getJsonName()]
                        .asUInt() == 1);
            }
        }

        // gw creates an offer that we can look for in the ledger.
        env(offer(gw, USD(7), XRP(14)));
        env.close();
        {
            // Find the offer.
            Json::Value const resp = acctObjs(gw, jss::offer);
            BEAST_EXPECT(acctObjsIsSize(resp, 1));

            auto const& offer = resp[jss::result][jss::account_objects][0u];
            BEAST_EXPECT(offer[sfAccount.jsonName] == gw.human());
            BEAST_EXPECT(offer[sfTakerGets.jsonName].asUInt() == 14'000'000);
            BEAST_EXPECT(offer[sfTakerPays.jsonName][jss::value].asUInt() == 7);
        }
        {
            // Create a payment channel from qw to alice that we can look
            // for.
            Json::Value jvPayChan;
            jvPayChan[jss::TransactionType] = jss::PaymentChannelCreate;
            jvPayChan[jss::Account] = gw.human();
            jvPayChan[jss::Destination] = alice.human();
            jvPayChan[jss::Amount] =
                XRP(300).value().getJson(JsonOptions::none);
            jvPayChan[sfSettleDelay.jsonName] = 24 * 60 * 60;
            jvPayChan[sfPublicKey.jsonName] = strHex(gw.pk().slice());
            env(jvPayChan);
            env.close();
        }
        {
            // Find the payment channel.
            Json::Value const resp = acctObjs(gw, jss::payment_channel);
            BEAST_EXPECT(acctObjsIsSize(resp, 1));

            auto const& payChan = resp[jss::result][jss::account_objects][0u];
            BEAST_EXPECT(payChan[sfAccount.jsonName] == gw.human());
            BEAST_EXPECT(payChan[sfAmount.jsonName].asUInt() == 300'000'000);
            BEAST_EXPECT(
                payChan[sfSettleDelay.jsonName].asUInt() == 24 * 60 * 60);
        }

        {
            // gw creates a DID that we can look for in the ledger.
            Json::Value jvDID;
            jvDID[jss::TransactionType] = jss::DIDSet;
            jvDID[jss::Account] = gw.human();
            jvDID[sfURI.jsonName] = strHex(std::string{"uri"});
            env(jvDID);
            env.close();
        }
        {
            // Find the DID.
            Json::Value const resp = acctObjs(gw, jss::did);
            BEAST_EXPECT(acctObjsIsSize(resp, 1));

            auto const& did = resp[jss::result][jss::account_objects][0u];
            BEAST_EXPECT(did[sfAccount.jsonName] == gw.human());
            BEAST_EXPECT(did[sfURI.jsonName] == strHex(std::string{"uri"}));
        }
        // Make gw multisigning by adding a signerList.
        env(jtx::signers(gw, 6, {{alice, 7}}));
        env.close();
        {
            // Find the signer list.
            Json::Value const resp = acctObjs(gw, jss::signer_list);
            BEAST_EXPECT(acctObjsIsSize(resp, 1));

            auto const& signerList =
                resp[jss::result][jss::account_objects][0u];
            BEAST_EXPECT(signerList[sfSignerQuorum.jsonName] == 6);
            auto const& entry = signerList[sfSignerEntries.jsonName][0u]
                                          [sfSignerEntry.jsonName];
            BEAST_EXPECT(entry[sfAccount.jsonName] == alice.human());
            BEAST_EXPECT(entry[sfSignerWeight.jsonName].asUInt() == 7);
        }

        {
            auto const seq = env.seq(gw);
            // Create a Ticket for gw.
            env(ticket::create(gw, 1));
            env.close();

            // Find the ticket.
            Json::Value const resp = acctObjs(gw, jss::ticket);
            BEAST_EXPECT(acctObjsIsSize(resp, 1));

            auto const& ticket = resp[jss::result][jss::account_objects][0u];
            BEAST_EXPECT(ticket[sfAccount.jsonName] == gw.human());
            BEAST_EXPECT(ticket[sfLedgerEntryType.jsonName] == jss::Ticket);
            BEAST_EXPECT(ticket[sfTicketSequence.jsonName].asUInt() == seq + 1);
        }

        {
            // See how "deletion_blockers_only" handles gw's directory.
            Json::Value params;
            params[jss::account] = gw.human();
            params[jss::deletion_blockers_only] = true;
            auto resp = env.rpc("json", "account_objects", to_string(params));

            std::vector<std::string> const expectedLedgerTypes = [] {
                std::vector<std::string> v{
                    jss::Escrow.c_str(),
                    jss::Check.c_str(),
                    jss::NFTokenPage.c_str(),
                    jss::RippleState.c_str(),
                    jss::PayChannel.c_str(),
                    jss::PermissionedDomain.c_str()};
                std::sort(v.begin(), v.end());
                return v;
            }();

            std::uint32_t const expectedAccountObjects{
                static_cast<std::uint32_t>(std::size(expectedLedgerTypes))};

            if (BEAST_EXPECT(acctObjsIsSize(resp, expectedAccountObjects)))
            {
                auto const& aobjs = resp[jss::result][jss::account_objects];
                std::vector<std::string> gotLedgerTypes;
                gotLedgerTypes.reserve(expectedAccountObjects);
                for (std::uint32_t i = 0; i < expectedAccountObjects; ++i)
                {
                    gotLedgerTypes.push_back(
                        aobjs[i]["LedgerEntryType"].asString());
                }
                std::sort(gotLedgerTypes.begin(), gotLedgerTypes.end());
                BEAST_EXPECT(gotLedgerTypes == expectedLedgerTypes);
            }
        }
        {
            // See how "deletion_blockers_only" with `type` handles gw's
            // directory.
            Json::Value params;
            params[jss::account] = gw.human();
            params[jss::deletion_blockers_only] = true;
            params[jss::type] = jss::escrow;
            auto resp = env.rpc("json", "account_objects", to_string(params));

            if (BEAST_EXPECT(acctObjsIsSize(resp, 1u)))
            {
                auto const& aobjs = resp[jss::result][jss::account_objects];
                BEAST_EXPECT(aobjs[0u]["LedgerEntryType"] == jss::Escrow);
            }
        }
        {
            // Make a lambda to get the types
            auto getTypes = [&](Json::Value const& resp,
                                std::vector<std::string>& typesOut) {
                auto const objs = resp[jss::result][jss::account_objects];
                for (auto const& obj : resp[jss::result][jss::account_objects])
                    typesOut.push_back(
                        obj[sfLedgerEntryType.fieldName].asString());
                std::sort(typesOut.begin(), typesOut.end());
            };
            // Make a lambda we can use to check the number of fetched
            // account objects and their ledger type
            auto expectObjects =
                [&](Json::Value const& resp,
                    std::vector<std::string> const& types) -> bool {
                if (!acctObjsIsSize(resp, types.size()))
                    return false;
                std::vector<std::string> typesOut;
                getTypes(resp, typesOut);
                return types == typesOut;
            };
            // Find AMM objects
            AMM amm(env, gw, XRP(1'000), USD(1'000));
            amm.deposit(alice, USD(1));
            // AMM account has 4 objects: AMM object and 3 trustlines
            auto const lines = getAccountLines(env, amm.ammAccount());
            BEAST_EXPECT(lines[jss::lines].size() == 3);
            // request AMM only, doesn't depend on the limit
            BEAST_EXPECT(
                acctObjsIsSize(acctObjs(amm.ammAccount(), jss::amm), 1));
            // request first two objects
            auto resp = acctObjs(amm.ammAccount(), std::nullopt, 2);
            std::vector<std::string> typesOut;
            getTypes(resp, typesOut);
            // request next two objects
            resp = acctObjs(
                amm.ammAccount(),
                std::nullopt,
                10,
                resp[jss::result][jss::marker].asString());
            getTypes(resp, typesOut);
            BEAST_EXPECT(
                (typesOut ==
                 std::vector<std::string>{
                     jss::AMM.c_str(),
                     jss::RippleState.c_str(),
                     jss::RippleState.c_str(),
                     jss::RippleState.c_str()}));
            // filter by state: there are three trustlines
            resp = acctObjs(amm.ammAccount(), jss::state, 10);
            BEAST_EXPECT(expectObjects(
                resp,
                {jss::RippleState.c_str(),
                 jss::RippleState.c_str(),
                 jss::RippleState.c_str()}));
            // AMM account doesn't own offers
            BEAST_EXPECT(
                acctObjsIsSize(acctObjs(amm.ammAccount(), jss::offer), 0));
            // gw account doesn't own AMM object
            BEAST_EXPECT(acctObjsIsSize(acctObjs(gw, jss::amm), 0));
        }

        // we still expect invalid field type reported for the following types
        BEAST_EXPECT(acctObjsTypeIsInvalid(acctObjs(gw, jss::amendments)));
        BEAST_EXPECT(acctObjsTypeIsInvalid(acctObjs(gw, jss::directory)));
        BEAST_EXPECT(acctObjsTypeIsInvalid(acctObjs(gw, jss::fee)));
        BEAST_EXPECT(acctObjsTypeIsInvalid(acctObjs(gw, jss::hashes)));
        BEAST_EXPECT(acctObjsTypeIsInvalid(acctObjs(gw, jss::NegativeUNL)));

        // Run up the number of directory entries so gw has two
        // directory nodes.
        for (int d = 1'000'032; d >= 1'000'000; --d)
        {
            env(offer(gw, USD(1), drops(d)));
            env.close();
        }

        // Verify that the non-returning types still don't return anything.
        BEAST_EXPECT(acctObjsIsSize(acctObjs(gw, jss::account), 0));
    }

    void
    testNFTsMarker()
    {
        // there's some bug found in account_nfts method that it did not
        // return invalid params when providing unassociated nft marker.
        // this test tests both situations when providing valid nft marker
        // and unassociated nft marker.
        testcase("NFTsMarker");

        using namespace jtx;
        Env env(*this);

        Account const bob{"bob"};
        env.fund(XRP(10000), bob);

        static constexpr unsigned nftsSize = 10;
        for (unsigned i = 0; i < nftsSize; i++)
        {
            env(token::mint(bob, 0));
        }

        env.close();

        // save the NFTokenIDs to use later
        std::vector<Json::Value> tokenIDs;
        {
            Json::Value params;
            params[jss::account] = bob.human();
            params[jss::ledger_index] = "validated";
            Json::Value const resp =
                env.rpc("json", "account_nfts", to_string(params));
            Json::Value const& nfts = resp[jss::result][jss::account_nfts];
            for (Json::Value const& nft : nfts)
                tokenIDs.push_back(nft["NFTokenID"]);
        }

        // this lambda function is used to check if the account_nfts method
        // returns the correct token information. lastIndex is used to query the
        // last marker.
        auto compareNFTs = [&tokenIDs, &env, &bob](
                               unsigned const limit, unsigned const lastIndex) {
            Json::Value params;
            params[jss::account] = bob.human();
            params[jss::limit] = limit;
            params[jss::marker] = tokenIDs[lastIndex];
            params[jss::ledger_index] = "validated";
            Json::Value const resp =
                env.rpc("json", "account_nfts", to_string(params));

            if (resp[jss::result].isMember(jss::error))
                return false;

            Json::Value const& nfts = resp[jss::result][jss::account_nfts];
            unsigned const nftsCount = tokenIDs.size() - lastIndex - 1 < limit
                ? tokenIDs.size() - lastIndex - 1
                : limit;

            if (nfts.size() != nftsCount)
                return false;

            for (unsigned i = 0; i < nftsCount; i++)
            {
                if (nfts[i]["NFTokenID"] != tokenIDs[lastIndex + 1 + i])
                    return false;
            }

            return true;
        };

        // test a valid marker which is equal to the third tokenID
        BEAST_EXPECT(compareNFTs(4, 2));

        // test a valid marker which is equal to the 8th tokenID
        BEAST_EXPECT(compareNFTs(4, 7));

        // lambda that holds common code for invalid cases.
        auto testInvalidMarker = [&env, &bob](
                                     auto marker, char const* errorMessage) {
            Json::Value params;
            params[jss::account] = bob.human();
            params[jss::limit] = 4;
            params[jss::ledger_index] = jss::validated;
            params[jss::marker] = marker;
            Json::Value const resp =
                env.rpc("json", "account_nfts", to_string(params));
            return resp[jss::result][jss::error_message] == errorMessage;
        };

        // test an invalid marker that is not a string
        BEAST_EXPECT(
            testInvalidMarker(17, "Invalid field \'marker\', not string."));

        // test an invalid marker that has a non-hex character
        BEAST_EXPECT(testInvalidMarker(
            "00000000F51DFC2A09D62CBBA1DFBDD4691DAC96AD98B900000000000000000G",
            "Invalid field \'marker\'."));

        // this lambda function is used to create some fake marker using given
        // taxon and sequence because we want to test some unassociated markers
        // later
        auto createFakeNFTMarker = [](AccountID const& issuer,
                                      std::uint32_t taxon,
                                      std::uint32_t tokenSeq,
                                      std::uint16_t flags = 0,
                                      std::uint16_t fee = 0) {
            // the marker has the exact same format as an NFTokenID
            return to_string(NFTokenMint::createNFTokenID(
                flags, fee, issuer, nft::toTaxon(taxon), tokenSeq));
        };

        // test an unassociated marker which does not exist in the NFTokenIDs
        BEAST_EXPECT(testInvalidMarker(
            createFakeNFTMarker(bob.id(), 0x000000000, 0x00000000),
            "Invalid field \'marker\'."));

        // test an unassociated marker which exceeds the maximum value of the
        // existing NFTokenID
        BEAST_EXPECT(testInvalidMarker(
            createFakeNFTMarker(bob.id(), 0xFFFFFFFF, 0xFFFFFFFF),
            "Invalid field \'marker\'."));
    }

    void
    testAccountNFTs()
    {
        testcase("account_nfts");

        using namespace jtx;
        Env env(*this);

        // test validation
        {
            auto testInvalidAccountParam = [&](auto const& param) {
                Json::Value params;
                params[jss::account] = param;
                auto jrr = env.rpc(
                    "json", "account_nfts", to_string(params))[jss::result];
                BEAST_EXPECT(jrr[jss::error] == "invalidParams");
                BEAST_EXPECT(
                    jrr[jss::error_message] == "Invalid field 'account'.");
            };

            testInvalidAccountParam(1);
            testInvalidAccountParam(1.1);
            testInvalidAccountParam(true);
            testInvalidAccountParam(Json::Value(Json::nullValue));
            testInvalidAccountParam(Json::Value(Json::objectValue));
            testInvalidAccountParam(Json::Value(Json::arrayValue));
        }
    }

    void
    testAccountObjectMarker()
    {
        testcase("AccountObjectMarker");

        using namespace jtx;
        Env env(*this);

        Account const alice{"alice"};
        Account const bob{"bob"};
        Account const carol{"carol"};
        env.fund(XRP(10000), alice, bob, carol);

        unsigned const accountObjectSize = 30;
        for (unsigned i = 0; i < accountObjectSize; i++)
            env(check::create(alice, bob, XRP(10)));

        for (unsigned i = 0; i < 10; i++)
            env(token::mint(carol, 0));

        env.close();

        unsigned const limit = 11;
        Json::Value marker;

        // test account_objects with a limit and update marker
        {
            Json::Value params;
            params[jss::account] = bob.human();
            params[jss::limit] = limit;
            params[jss::ledger_index] = "validated";
            auto resp = env.rpc("json", "account_objects", to_string(params));
            auto& accountObjects = resp[jss::result][jss::account_objects];
            marker = resp[jss::result][jss::marker];
            BEAST_EXPECT(!resp[jss::result].isMember(jss::error));
            BEAST_EXPECT(accountObjects.size() == limit);
        }

        // test account_objects with valid marker and update marker
        {
            Json::Value params;
            params[jss::account] = bob.human();
            params[jss::limit] = limit;
            params[jss::marker] = marker;
            params[jss::ledger_index] = "validated";
            auto resp = env.rpc("json", "account_objects", to_string(params));
            auto& accountObjects = resp[jss::result][jss::account_objects];
            marker = resp[jss::result][jss::marker];
            BEAST_EXPECT(!resp[jss::result].isMember(jss::error));
            BEAST_EXPECT(accountObjects.size() == limit);
        }

        // this lambda function is used to check invalid marker response.
        auto testInvalidMarker = [&](std::string& marker) {
            Json::Value params;
            params[jss::account] = bob.human();
            params[jss::limit] = limit;
            params[jss::ledger_index] = jss::validated;
            params[jss::marker] = marker;
            Json::Value const resp =
                env.rpc("json", "account_objects", to_string(params));
            return resp[jss::result][jss::error_message] ==
                "Invalid field \'marker\'.";
        };

        auto const markerStr = marker.asString();
        auto const& idx = markerStr.find(',');
        auto const dirIndex = markerStr.substr(0, idx);
        auto const entryIndex = markerStr.substr(idx + 1);

        // test account_objects with an invalid marker that contains no ','
        {
            std::string s = dirIndex + entryIndex;
            BEAST_EXPECT(testInvalidMarker(s));
        }

        // test invalid marker by adding invalid string after the maker:
        // "dirIndex,entryIndex,1234"
        {
            std::string s = markerStr + ",1234";
            BEAST_EXPECT(testInvalidMarker(s));
        }

        // test account_objects with an invalid marker containing invalid
        // dirIndex by replacing some characters from the dirIndex.
        {
            std::string s = markerStr;
            s.replace(0, 7, "FFFFFFF");
            BEAST_EXPECT(testInvalidMarker(s));
        }

        // test account_objects with an invalid marker containing invalid
        // entryIndex by replacing some characters from the entryIndex.
        {
            std::string s = entryIndex;
            s.replace(0, 7, "FFFFFFF");
            s = dirIndex + ',' + s;
            BEAST_EXPECT(testInvalidMarker(s));
        }

        // test account_objects with an invalid marker containing invalid
        // dirIndex with marker: ",entryIndex"
        {
            std::string s = ',' + entryIndex;
            BEAST_EXPECT(testInvalidMarker(s));
        }

        // test account_objects with marker: "0,entryIndex", this is still
        // valid, because when dirIndex = 0, we will use root key to find
        // dir.
        {
            std::string s = "0," + entryIndex;
            Json::Value params;
            params[jss::account] = bob.human();
            params[jss::limit] = limit;
            params[jss::marker] = s;
            params[jss::ledger_index] = "validated";
            auto resp = env.rpc("json", "account_objects", to_string(params));
            auto& accountObjects = resp[jss::result][jss::account_objects];
            BEAST_EXPECT(!resp[jss::result].isMember(jss::error));
            BEAST_EXPECT(accountObjects.size() == limit);
        }

        // test account_objects with an invalid marker containing invalid
        // entryIndex with marker: "dirIndex,"
        {
            std::string s = dirIndex + ',';
            BEAST_EXPECT(testInvalidMarker(s));
        }

        // test account_objects with an invalid marker containing invalid
        // entryIndex with marker: "dirIndex,0"
        {
            std::string s = dirIndex + ",0";
            BEAST_EXPECT(testInvalidMarker(s));
        }

        // continue getting account_objects with valid marker. This will be the
        // last page, so response will not contain any marker.
        {
            Json::Value params;
            params[jss::account] = bob.human();
            params[jss::limit] = limit;
            params[jss::marker] = marker;
            params[jss::ledger_index] = "validated";
            auto resp = env.rpc("json", "account_objects", to_string(params));
            auto& accountObjects = resp[jss::result][jss::account_objects];
            BEAST_EXPECT(!resp[jss::result].isMember(jss::error));
            BEAST_EXPECT(
                accountObjects.size() == accountObjectSize - limit * 2);
            BEAST_EXPECT(!resp[jss::result].isMember(jss::marker));
        }

        // test account_objects when the account only have nft pages, but
        // provided invalid entry index.
        {
            Json::Value params;
            params[jss::account] = carol.human();
            params[jss::limit] = 10;
            params[jss::marker] = "0," + entryIndex;
            params[jss::ledger_index] = "validated";
            auto resp = env.rpc("json", "account_objects", to_string(params));
            auto& accountObjects = resp[jss::result][jss::account_objects];
            BEAST_EXPECT(accountObjects.size() == 0);
        }
    }

    void
    run() override
    {
        testErrors();
        testUnsteppedThenStepped();
        testUnsteppedThenSteppedWithNFTs();
        testObjectTypes();
        testNFTsMarker();
        testAccountNFTs();
        testAccountObjectMarker();
    }
};

BEAST_DEFINE_TESTSUITE(AccountObjects, rpc, ripple);

}  // namespace test
}  // namespace ripple
