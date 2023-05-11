////------------------------------------------------------------------------------
///*
//    This file is part of rippled: https://github.com/ripple/rippled
//    Copyright (c) 2012-2016 Ripple Labs Inc.
//
//    Permission to use, copy, modify, and/or distribute this software for any
//    purpose  with  or without fee is hereby granted, provided that the above
//    copyright notice and this permission notice appear in all copies.
//
//    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
//    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
//    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
//    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
//    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
//    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
//    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
//*/
////==============================================================================
//
//#include <ripple/app/ledger/LedgerMaster.h>
//#include <ripple/beast/unit_test.h>
//#include <ripple/protocol/ErrorCodes.h>
//#include <ripple/protocol/jss.h>
//#include <ripple/rpc/impl/RPCHelpers.h>
//#include <test/jtx.h>
//
//namespace ripple {
//
//namespace RPC {
//
//class LedgerRequestRPC_test : public beast::unit_test::suite
//{
//    static constexpr char const* hash1 =
//        "3020EB9E7BE24EF7D7A060CB051583EC117384636D1781AFB5B87F3E348DA489";
//    static constexpr char const* accounthash1 =
//        "BD8A3D72CA73DDE887AD63666EC2BAD07875CBA997A102579B5B95ECDFFEAED8";
//
//    static constexpr char const* zerohash =
//        "0000000000000000000000000000000000000000000000000000000000000000";
//
//public:
//    void
//    testLedgerRequest()
//    {
//        using namespace test::jtx;
//
//        Env env(*this);
//
//        env.close();
//        env.close();
//        BEAST_EXPECT(env.current()->info().seq == 5);
//
//        {
//            // arbitrary text is converted to 0.
//            boost::json::object result = env.rpc("ledger_request", "arbitrary_text").as_object();
//            BEAST_EXPECT(
//                RPC::contains_error(result[jss::result.c_str()]) &&
//                result[jss::result.c_str()].as_object()[jss::error_message.c_str()] ==
//                    "Ledger index too small");
//        }
//
//        {
//            boost::json::object result = env.rpc("ledger_request", "-1").as_object();
//            BEAST_EXPECT(
//                RPC::contains_error(result[jss::result.c_str()]) &&
//                result[jss::result.c_str()].as_object()[jss::error_message.c_str()] ==
//                    "Ledger index too small");
//        }
//
//        {
//            boost::json::object result = env.rpc("ledger_request", "0").as_object();
//            BEAST_EXPECT(
//                RPC::contains_error(result[jss::result.c_str()]) &&
//                result[jss::result.c_str()].as_object()[jss::error_message.c_str()] ==
//                    "Ledger index too small");
//        }
//
//        {
//            boost::json::object result = env.rpc("ledger_request", "1").as_object();
//            BEAST_EXPECT(
//                !RPC::contains_error(result[jss::result.c_str()]) &&
//                result[jss::result.c_str()].as_object()[jss::ledger_index.c_str()] == 1 &&
//                result[jss::result.c_str()].as_object().contains(jss::ledger.c_str()));
//            BEAST_EXPECT(
//                result[jss::result.c_str()].as_object()[jss::ledger.c_str()].as_object().contains(jss::ledger_hash.c_str()) &&
//                result[jss::result.c_str()].as_object()[jss::ledger.c_str()].as_object()[jss::ledger_hash.c_str()].is_string());
//        }
//
//        {
//            boost::json::object result = env.rpc("ledger_request", "2").as_object();
//            BEAST_EXPECT(
//                !RPC::contains_error(result[jss::result.c_str()]) &&
//                result[jss::result.c_str()].as_object()[jss::ledger_index.c_str()] == 2 &&
//                result[jss::result.c_str()].as_object().contains(jss::ledger.c_str()));
//            BEAST_EXPECT(
//                result[jss::result.c_str()].as_object()[jss::ledger.c_str()].as_object().contains(jss::ledger_hash.c_str()) &&
//                result[jss::result.c_str()].as_object()[jss::ledger.c_str()].as_object()[jss::ledger_hash.c_str()].is_string());
//        }
//
//        {
//            boost::json::object result = env.rpc("ledger_request", "3").as_object();
//            BEAST_EXPECT(
//                !RPC::contains_error(result[jss::result.c_str()]) &&
//                result[jss::result.c_str()].as_object()[jss::ledger_index.c_str()] == 3 &&
//                result[jss::result.c_str()].as_object().contains(jss::ledger.c_str()));
//            BEAST_EXPECT(
//                result[jss::result.c_str()].as_object()[jss::ledger.c_str()].as_object().contains(jss::ledger_hash.c_str()) &&
//                result[jss::result.c_str()].as_object()[jss::ledger.c_str()].as_object()[jss::ledger_hash.c_str()].is_string());
//
//            auto const ledgerHash =
//                std::string{result[jss::result.c_str()].as_object()[jss::ledger.c_str()].as_object()[jss::ledger_hash.c_str()].as_string()};
//
//            {
//                auto r = env.rpc("ledger_request", ledgerHash).as_object();
//                BEAST_EXPECT(
//                    !RPC::contains_error(r[jss::result.c_str()]) &&
//                    r[jss::result.c_str()].as_object()[jss::ledger_index.c_str()] == 3 &&
//                    r[jss::result.c_str()].as_object().contains(jss::ledger.c_str()));
//                BEAST_EXPECT(
//                    r[jss::result.c_str()].as_object()[jss::ledger.c_str()].as_object().contains(jss::ledger_hash.c_str()) &&
//                    r[jss::result.c_str()].as_object()[jss::ledger.c_str()].as_object()[jss::ledger_hash.c_str()].as_string() ==
//                        ledgerHash);
//            }
//        }
//
//        {
//            std::string ledgerHash(64, 'q');
//
//            boost::json::object result = env.rpc("ledger_request", ledgerHash).as_object();
//
//            BEAST_EXPECT(
//                RPC::contains_error(result[jss::result.c_str()]) &&
//                result[jss::result.c_str()].as_object()[jss::error_message.c_str()] ==
//                    "Invalid field 'ledger_hash'.");
//        }
//
//        {
//            std::string ledgerHash(64, '1');
//
//            boost::json::object result = env.rpc("ledger_request", ledgerHash).as_object();
//
//            BEAST_EXPECT(
//                !RPC::contains_error(result[jss::result.c_str()]) &&
//                result[jss::result.c_str()].as_object()[jss::have_header.c_str()] == false);
//        }
//
//        {
//            boost::json::object result = env.rpc("ledger_request", "4").as_object();
//            BEAST_EXPECT(
//                RPC::contains_error(result[jss::result.c_str()]) &&
//                result[jss::result.c_str()].as_object()[jss::error_message.c_str()] ==
//                    "Ledger index too large");
//        }
//
//        {
//            boost::json::object result = env.rpc("ledger_request", "5").as_object();
//            BEAST_EXPECT(
//                RPC::contains_error(result[jss::result.c_str()]) &&
//                result[jss::result.c_str()].as_object()[jss::error_message.c_str()] ==
//                    "Ledger index too large");
//        }
//    }
//
//    void
//    testEvolution()
//    {
//        using namespace test::jtx;
//        Env env{*this, FeatureBitset{}};  // the hashes being checked below
//                                          // assume no amendments
//        Account const gw{"gateway"};
//        auto const USD = gw["USD"];
//        env.fund(XRP(100000), gw);
//        env.close();
//
//        env.memoize("bob");
//        env.fund(XRP(1000), "bob");
//        env.close();
//
//        env.memoize("alice");
//        env.fund(XRP(1000), "alice");
//        env.close();
//
//        env.memoize("carol");
//        env.fund(XRP(1000), "carol");
//        env.close();
//
//        boost::json::object result = env.rpc("ledger_request", "1").as_object()[jss::result.c_str()].as_object();
//        BEAST_EXPECT(result[jss::ledger.c_str()].as_object()[jss::ledger_index.c_str()] == "1");
//        BEAST_EXPECT(
//            result[jss::ledger.c_str()].as_object()[jss::total_coins.c_str()] == "100000000000000000");
//        BEAST_EXPECT(result[jss::ledger.c_str()].as_object()[jss::closed.c_str()] == true);
//        BEAST_EXPECT(result[jss::ledger.c_str()].as_object()[jss::ledger_hash.c_str()] == hash1);
//        BEAST_EXPECT(result[jss::ledger.c_str()].as_object()[jss::parent_hash.c_str()] == zerohash);
//        BEAST_EXPECT(result[jss::ledger.c_str()].as_object()[jss::account_hash.c_str()] == accounthash1);
//        BEAST_EXPECT(result[jss::ledger.c_str()].as_object()[jss::transaction_hash.c_str()] == zerohash);
//
//        result = env.rpc("ledger_request", "2").as_object()[jss::result.c_str()].as_object();
//        constexpr char const* hash2 =
//            "CCC3B3E88CCAC17F1BE6B4A648A55999411F19E3FE55EB721960EB0DF28EDDA5";
//        BEAST_EXPECT(result[jss::ledger.c_str()].as_object()[jss::ledger_index.c_str()] == "2");
//        BEAST_EXPECT(
//            result[jss::ledger.c_str()].as_object()[jss::total_coins.c_str()] == "100000000000000000");
//        BEAST_EXPECT(result[jss::ledger.c_str()].as_object()[jss::closed.c_str()] == true);
//        BEAST_EXPECT(result[jss::ledger.c_str()].as_object()[jss::ledger_hash.c_str()] == hash2);
//        BEAST_EXPECT(result[jss::ledger.c_str()].as_object()[jss::parent_hash.c_str()] == hash1);
//        BEAST_EXPECT(
//            result[jss::ledger.c_str()].as_object()[jss::account_hash.c_str()] ==
//            "3C834285F7F464FBE99AFEB84D354A968EB2CAA24523FF26797A973D906A3D29");
//        BEAST_EXPECT(result[jss::ledger.c_str()].as_object()[jss::transaction_hash.c_str()] == zerohash);
//
//        result = env.rpc("ledger_request", "3").as_object()[jss::result.c_str()].as_object();
//        constexpr char const* hash3 =
//            "8D631B20BC989AF568FBA97375290544B0703A5ADC1CF9E9053580461690C9EE";
//        BEAST_EXPECT(result[jss::ledger.c_str()].as_object()[jss::ledger_index.c_str()] == "3");
//        BEAST_EXPECT(
//            result[jss::ledger.c_str()].as_object()[jss::total_coins.c_str()] == "99999999999999980");
//        BEAST_EXPECT(result[jss::ledger.c_str()].as_object()[jss::closed.c_str()] == true);
//        BEAST_EXPECT(result[jss::ledger.c_str()].as_object()[jss::ledger_hash.c_str()] == hash3);
//        BEAST_EXPECT(result[jss::ledger.c_str()].as_object()[jss::parent_hash.c_str()] == hash2);
//        BEAST_EXPECT(
//            result[jss::ledger.c_str()].as_object()[jss::account_hash.c_str()] ==
//            "BC9EF2A16BFF80BCFABA6FA84688D858D33BD0FA0435CAA9DF6DA4105A39A29E");
//        BEAST_EXPECT(
//            result[jss::ledger.c_str()].as_object()[jss::transaction_hash.c_str()] ==
//            "0213EC486C058B3942FBE3DAC6839949A5C5B02B8B4244C8998EFDF04DBD8222");
//
//        result = env.rpc("ledger_request", "4").as_object()[jss::result.c_str()].as_object();
//        constexpr char const* hash4 =
//            "1A8E7098B23597E73094DADA58C9D62F3AB93A12C6F7666D56CA85A6CFDE530F";
//        BEAST_EXPECT(result[jss::ledger.c_str()].as_object()[jss::ledger_index.c_str()] == "4");
//        BEAST_EXPECT(
//            result[jss::ledger.c_str()].as_object()[jss::total_coins.c_str()] == "99999999999999960");
//        BEAST_EXPECT(result[jss::ledger.c_str()].as_object()[jss::closed.c_str()] == true);
//        BEAST_EXPECT(result[jss::ledger.c_str()].as_object()[jss::ledger_hash.c_str()] == hash4);
//        BEAST_EXPECT(result[jss::ledger.c_str()].as_object()[jss::parent_hash.c_str()] == hash3);
//        BEAST_EXPECT(
//            result[jss::ledger.c_str()].as_object()[jss::account_hash.c_str()] ==
//            "C690188F123C91355ADA8BDF4AC5B5C927076D3590C215096868A5255264C6DD");
//        BEAST_EXPECT(
//            result[jss::ledger.c_str()].as_object()[jss::transaction_hash.c_str()] ==
//            "3CBDB8F42E04333E1642166BFB93AC9A7E1C6C067092CD5D881D6F3AB3D67E76");
//
//        result = env.rpc("ledger_request", "5").as_object()[jss::result.c_str()].as_object();
//        constexpr char const* hash5 =
//            "C6A222D71AE65D7B4F240009EAD5DEB20D7EEDE5A4064F28BBDBFEEB6FBE48E5";
//        BEAST_EXPECT(result[jss::ledger.c_str()].as_object()[jss::ledger_index.c_str()] == "5");
//        BEAST_EXPECT(
//            result[jss::ledger.c_str()].as_object()[jss::total_coins.c_str()] == "99999999999999940");
//        BEAST_EXPECT(result[jss::ledger.c_str()].as_object()[jss::closed.c_str()] == true);
//        BEAST_EXPECT(result[jss::ledger.c_str()].as_object()[jss::ledger_hash.c_str()] == hash5);
//        BEAST_EXPECT(result[jss::ledger.c_str()].as_object()[jss::parent_hash.c_str()] == hash4);
//        BEAST_EXPECT(
//            result[jss::ledger.c_str()].as_object()[jss::account_hash.c_str()] ==
//            "EA81CD9D36740736F00CB747E0D0E32D3C10B695823D961F0FB9A1CE7133DD4D");
//        BEAST_EXPECT(
//            result[jss::ledger.c_str()].as_object()[jss::transaction_hash.c_str()] ==
//            "C3D086CD6BDB9E97AD1D513B2C049EF2840BD21D0B3E22D84EBBB89B6D2EF59D");
//
//        result = env.rpc("ledger_request", "6").as_object()[jss::result.c_str()].as_object();
//        BEAST_EXPECT(result[jss::error.c_str()] == "invalidParams");
//        BEAST_EXPECT(result[jss::status.c_str()] == "error");
//        BEAST_EXPECT(result[jss::error_message.c_str()] == "Ledger index too large");
//    }
//
//    void
//    testBadInput()
//    {
//        using namespace test::jtx;
//        Env env{*this};
//        Account const gw{"gateway"};
//        auto const USD = gw["USD"];
//        env.fund(XRP(100000), gw);
//        env.close();
//
//        Json::Value jvParams;
//        jvParams[jss::ledger_hash.c_str()] =
//            "AB868A6CFEEC779C2FF845C0AF00A642259986AF40C01976A7F842B6918936C7";
//        jvParams[jss::ledger_index.c_str()] = "1";
//        boost::json::object result = env.rpc(
//            "json", "ledger_request", jvParams.toStyledString()).as_object()[jss::result.c_str()].as_object();
//        BEAST_EXPECT(result[jss::error.c_str()] == "invalidParams");
//        BEAST_EXPECT(result[jss::status.c_str()] == "error");
//        BEAST_EXPECT(
//            result[jss::error_message.c_str()] ==
//            "Exactly one of ledger_hash and ledger_index can be set.");
//
//        // the purpose in this test is to force the ledger expiration/out of
//        // date check to trigger
//        env.timeKeeper().adjustCloseTime(weeks{3});
//        result = env.rpc("ledger_request", "1").as_object()[jss::result.c_str()].as_object();
//        BEAST_EXPECT(result[jss::status.c_str()] == "error");
//        if (RPC::apiMaximumSupportedVersion == 1)
//        {
//            BEAST_EXPECT(result[jss::error.c_str()] == "noCurrent");
//            BEAST_EXPECT(
//                result[jss::error_message.c_str()] == "Current ledger is unavailable.");
//        }
//        else
//        {
//            BEAST_EXPECT(result[jss::error.c_str()] == "notSynced");
//            BEAST_EXPECT(
//                result[jss::error_message.c_str()] == "Not synced to the network.");
//        }
//    }
//
//    void
//    testMoreThan256Closed()
//    {
//        using namespace test::jtx;
//        using namespace std::chrono_literals;
//        Env env{*this, envconfig([](std::unique_ptr<Config> cfg) {
//                    cfg->NODE_SIZE = 0;
//                    return cfg;
//                })};
//        Account const gw{"gateway"};
//        auto const USD = gw["USD"];
//        env.fund(XRP(100000), gw);
//
//        int const max_limit = 256;
//
//        for (auto i = 0; i < max_limit + 10; i++)
//        {
//            Account const bob{std::string("bob") + std::to_string(i)};
//            env.fund(XRP(1000), bob);
//            env.close();
//        }
//
//        boost::json::object result = env.rpc("ledger_request", "1").as_object()[jss::result.c_str()].as_object();
//        BEAST_EXPECT(result[jss::ledger.c_str()].as_object()[jss::ledger_index.c_str()] == "1");
//        BEAST_EXPECT(
//            result[jss::ledger.c_str()].as_object()[jss::total_coins.c_str()] == "100000000000000000");
//        BEAST_EXPECT(result[jss::ledger.c_str()].as_object()[jss::closed.c_str()] == true);
//        BEAST_EXPECT(result[jss::ledger.c_str()].as_object()[jss::ledger_hash.c_str()] == hash1);
//        BEAST_EXPECT(result[jss::ledger.c_str()].as_object()[jss::parent_hash.c_str()] == zerohash);
//        BEAST_EXPECT(result[jss::ledger.c_str()].as_object()[jss::account_hash.c_str()] == accounthash1);
//        BEAST_EXPECT(result[jss::ledger.c_str()].as_object()[jss::transaction_hash.c_str()] == zerohash);
//    }
//
//    void
//    testNonAdmin()
//    {
//        using namespace test::jtx;
//        Env env{*this, envconfig(no_admin)};
//        Account const gw{"gateway"};
//        auto const USD = gw["USD"];
//        env.fund(XRP(100000), gw);
//
//        boost::json::value result = env.rpc("ledger_request", "1").as_object()[jss::result.c_str()];
//        // The current HTTP/S ServerHandler returns an HTTP 403 error code here
//        // rather than a noPermission JSON error.  The JSONRPCClient just eats
//        // that error and returns an null result.
//        BEAST_EXPECT(result.is_null());
//    }
//
//    void
//    run() override
//    {
//        testLedgerRequest();
//        testEvolution();
//        testBadInput();
//        testMoreThan256Closed();
//        testNonAdmin();
//    }
//};
//
//BEAST_DEFINE_TESTSUITE(LedgerRequestRPC, app, ripple);
//
//}  // namespace RPC
//}  // namespace ripple
