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

#include <ripple/app/main/Application.h>
#include <ripple/app/misc/LoadFeeTrack.h>
#include <ripple/app/misc/NetworkOPs.h>
#include <ripple/app/paths/AccountCurrencies.h>
#include <ripple/app/paths/PathRequest.h>
#include <ripple/app/paths/PathRequests.h>
#include <ripple/app/paths/RippleCalc.h>
#include <ripple/app/paths/impl/PathfinderUtils.h>
#include <ripple/basics/Log.h>
#include <ripple/beast/core/LexicalCast.h>
#include <ripple/core/Config.h>
#include <ripple/net/RPCErr.h>
#include <ripple/protocol/ErrorCodes.h>
#include <ripple/protocol/UintTypes.h>

#include <ripple/rpc/impl/Tuning.h>
#include <optional>

#include <tuple>

namespace ripple {

PathRequest::PathRequest(
    Application& app,
    const std::shared_ptr<InfoSub>& subscriber,
    int id,
    PathRequests& owner,
    beast::Journal journal)
    : app_(app)
    , m_journal(journal)
    , mOwner(owner)
    , wpSubscriber(subscriber)
    , consumer_(subscriber->getConsumer())
    , jvStatus(Json::objectValue)
    , mLastIndex(0)
    , mInProgress(false)
    , iLevel(0)
    , bLastSuccess(false)
    , iIdentifier(id)
    , created_(std::chrono::steady_clock::now())
{
    JLOG(m_journal.debug()) << iIdentifier << " created";
}

PathRequest::PathRequest(
    Application& app,
    std::function<void(void)> const& completion,
    Resource::Consumer& consumer,
    int id,
    PathRequests& owner,
    beast::Journal journal)
    : app_(app)
    , m_journal(journal)
    , mOwner(owner)
    , fCompletion(completion)
    , consumer_(consumer)
    , jvStatus(Json::objectValue)
    , mLastIndex(0)
    , mInProgress(false)
    , iLevel(0)
    , bLastSuccess(false)
    , iIdentifier(id)
    , created_(std::chrono::steady_clock::now())
{
    JLOG(m_journal.debug()) << iIdentifier << " created";
}

PathRequest::~PathRequest()
{
    using namespace std::chrono;
    auto stream = m_journal.info();
    if (!stream)
        return;

    std::string fast, full;
    if (quick_reply_ != steady_clock::time_point{})
    {
        fast = " fast:";
        fast += std::to_string(
            duration_cast<milliseconds>(quick_reply_ - created_).count());
        fast += "ms";
    }
    if (full_reply_ != steady_clock::time_point{})
    {
        full = " full:";
        full += std::to_string(
            duration_cast<milliseconds>(full_reply_ - created_).count());
        full += "ms";
    }
    stream
        << iIdentifier << " complete:" << fast << full << " total:"
        << duration_cast<milliseconds>(steady_clock::now() - created_).count()
        << "ms";
}

bool
PathRequest::isNew()
{
    std::lock_guard sl(mIndexLock);

    // does this path request still need its first full path
    return mLastIndex == 0;
}

bool
PathRequest::needsUpdate(bool newOnly, LedgerIndex index)
{
    std::lock_guard sl(mIndexLock);

    if (mInProgress)
    {
        // Another thread is handling this
        return false;
    }

    if (newOnly && (mLastIndex != 0))
    {
        // Only handling new requests, this isn't new
        return false;
    }

    if (mLastIndex >= index)
    {
        return false;
    }

    mInProgress = true;
    return true;
}

bool
PathRequest::hasCompletion()
{
    return bool(fCompletion);
}

void
PathRequest::updateComplete()
{
    std::lock_guard sl(mIndexLock);

    assert(mInProgress);
    mInProgress = false;

    if (fCompletion)
    {
        fCompletion();
        fCompletion = std::function<void(void)>();
    }
}

bool
PathRequest::isValid(std::shared_ptr<RippleLineCache> const& crCache)
{
    if (!raSrcAccount || !raDstAccount)
        return false;

    if (!convert_all_ && (saSendMax || saDstAmount <= beast::zero))
    {
        // If send max specified, dst amt must be -1.
        jvStatus = rpcError(rpcDST_AMT_MALFORMED);
        return false;
    }

    auto const& lrLedger = crCache->getLedger();

    if (!lrLedger->exists(keylet::account(*raSrcAccount)))
    {
        // Source account does not exist.
        jvStatus = rpcError(rpcSRC_ACT_NOT_FOUND);
        return false;
    }

    auto const sleDest = lrLedger->read(keylet::account(*raDstAccount));

    boost::json::array& jvDestCur =
        (jvStatus[jss::destination_currencies.c_str()].emplace_array());

    if (!sleDest)
    {
        jvDestCur.emplace_back(boost::json::string(systemCurrencyCode()));
        if (!saDstAmount.native())
        {
            // Only XRP can be send to a non-existent account.
            jvStatus = rpcError(rpcACT_NOT_FOUND);
            return false;
        }

        if (!convert_all_ &&
            saDstAmount < STAmount(lrLedger->fees().accountReserve(0)))
        {
            // Payment must meet reserve.
            jvStatus = rpcError(rpcDST_AMT_MALFORMED);
            return false;
        }
    }
    else
    {
        bool const disallowXRP(sleDest->getFlags() & lsfDisallowXRP);

        auto usDestCurrID =
            accountDestCurrencies(*raDstAccount, crCache, !disallowXRP);

        for (auto const& currency : usDestCurrID)
            jvDestCur.emplace_back(to_string(currency));
        jvStatus[jss::destination_tag.c_str()] =
            (sleDest->getFlags() & lsfRequireDestTag);
    }

    jvStatus[jss::ledger_hash.c_str()] = to_string(lrLedger->info().hash);
    jvStatus[jss::ledger_index.c_str()] = lrLedger->seq();
    return true;
}

/*  If this is a normal path request, we want to run it once "fast" now
    to give preliminary results.

    If this is a legacy path request, we are only going to run it once,
    and we can't run it in full now, so we don't want to run it at all.

    If there's an error, we need to be sure to return it to the caller
    in all cases.
*/
std::pair<bool, boost::json::object>
PathRequest::doCreate(
    std::shared_ptr<RippleLineCache> const& cache,
    boost::json::object const& value)
{
    bool valid = false;

    if (parseJson(value) != PFR_PJ_INVALID)
    {
        valid = isValid(cache);
        if (!hasCompletion() && valid)
            doUpdate(cache, true);
    }

    if (auto stream = m_journal.debug())
    {
        if (valid)
        {
            stream << iIdentifier << " valid: " << toBase58(*raSrcAccount);
            stream << iIdentifier << " deliver: " << saDstAmount.getFullText();
        }
        else
        {
            stream << iIdentifier << " invalid";
        }
    }

    return {valid, jvStatus};
}

int
PathRequest::parseJson(boost::json::object const& jvParams)
{
    if (!jvParams.contains(jss::source_account.c_str()))
    {
        jvStatus = rpcError(rpcSRC_ACT_MISSING);
        return PFR_PJ_INVALID;
    }

    if (!jvParams.contains(jss::destination_account.c_str()))
    {
        jvStatus = rpcError(rpcDST_ACT_MISSING);
        return PFR_PJ_INVALID;
    }

    if (!jvParams.contains(jss::destination_amount.c_str()))
    {
        jvStatus = rpcError(rpcDST_AMT_MISSING);
        return PFR_PJ_INVALID;
    }

    raSrcAccount =
        parseBase58<AccountID>(std::string{jvParams.at(jss::source_account.c_str()).as_string()});
    if (!raSrcAccount)
    {
        jvStatus = rpcError(rpcSRC_ACT_MALFORMED);
        return PFR_PJ_INVALID;
    }

    raDstAccount =
        parseBase58<AccountID>(std::string{jvParams.at(jss::destination_account.c_str()).as_string()});
    if (!raDstAccount)
    {
        jvStatus = rpcError(rpcDST_ACT_MALFORMED);
        return PFR_PJ_INVALID;
    }

    if (!amountFromJsonNoThrow(saDstAmount, jvParams.at(jss::destination_amount.c_str())))
    {
        jvStatus = rpcError(rpcDST_AMT_MALFORMED);
        return PFR_PJ_INVALID;
    }

    convert_all_ = saDstAmount == STAmount(saDstAmount.issue(), 1u, 0, true);

    if ((saDstAmount.getCurrency().isZero() &&
         saDstAmount.getIssuer().isNonZero()) ||
        (saDstAmount.getCurrency() == badCurrency()) ||
        (!convert_all_ && saDstAmount <= beast::zero))
    {
        jvStatus = rpcError(rpcDST_AMT_MALFORMED);
        return PFR_PJ_INVALID;
    }

    if (jvParams.contains(jss::send_max.c_str()))
    {
        // Send_max requires destination amount to be -1.
        if (!convert_all_)
        {
            jvStatus = rpcError(rpcDST_AMT_MALFORMED);
            return PFR_PJ_INVALID;
        }

        saSendMax.emplace();
        if (!amountFromJsonNoThrow(*saSendMax, jvParams.at(jss::send_max.c_str())) ||
            (saSendMax->getCurrency().isZero() &&
             saSendMax->getIssuer().isNonZero()) ||
            (saSendMax->getCurrency() == badCurrency()) ||
            (*saSendMax <= beast::zero &&
             *saSendMax != STAmount(saSendMax->issue(), 1u, 0, true)))
        {
            jvStatus = rpcError(rpcSENDMAX_MALFORMED);
            return PFR_PJ_INVALID;
        }
    }

    if (jvParams.contains(jss::source_currencies.c_str()))
    {
        boost::json::value const& jvSrcCurrencies = jvParams.at(jss::source_currencies.c_str());
        if (!jvSrcCurrencies.is_array() || jvSrcCurrencies.as_array().size() == 0 ||
            jvSrcCurrencies.as_array().size() > RPC::Tuning::max_src_cur)
        {
            jvStatus = rpcError(rpcSRC_CUR_MALFORMED);
            return PFR_PJ_INVALID;
        }

        sciSourceCurrencies.clear();

        for (auto const& c : jvSrcCurrencies.as_array())
        {
            // Mandatory currency
            Currency srcCurrencyID;
            if (!c.is_object() || !c.as_object().contains(jss::currency.c_str()) ||
                !c.as_object().at(jss::currency.c_str()).is_string() ||
                !to_currency(srcCurrencyID, std::string{c.as_object().at(jss::currency.c_str()).as_string()}))
            {
                jvStatus = rpcError(rpcSRC_CUR_MALFORMED);
                return PFR_PJ_INVALID;
            }

            // Optional issuer
            AccountID srcIssuerID;
            if (c.as_object().contains(jss::issuer.c_str()) &&
                (!c.as_object().at(jss::issuer.c_str()).is_string() ||
                 !to_issuer(srcIssuerID, std::string{c.as_object().at(jss::issuer.c_str()).as_string()})))
            {
                jvStatus = rpcError(rpcSRC_ISR_MALFORMED);
                return PFR_PJ_INVALID;
            }

            if (srcCurrencyID.isZero())
            {
                if (srcIssuerID.isNonZero())
                {
                    jvStatus = rpcError(rpcSRC_CUR_MALFORMED);
                    return PFR_PJ_INVALID;
                }
            }
            else if (srcIssuerID.isZero())
            {
                srcIssuerID = *raSrcAccount;
            }

            if (saSendMax)
            {
                // If the currencies don't match, ignore the source currency.
                if (srcCurrencyID == saSendMax->getCurrency())
                {
                    // If neither is the source and they are not equal, then the
                    // source issuer is illegal.
                    if (srcIssuerID != *raSrcAccount &&
                        saSendMax->getIssuer() != *raSrcAccount &&
                        srcIssuerID != saSendMax->getIssuer())
                    {
                        jvStatus = rpcError(rpcSRC_ISR_MALFORMED);
                        return PFR_PJ_INVALID;
                    }

                    // If both are the source, use the source.
                    // Otherwise, use the one that's not the source.
                    if (srcIssuerID != *raSrcAccount)
                    {
                        sciSourceCurrencies.insert(
                            {srcCurrencyID, srcIssuerID});
                    }
                    else if (saSendMax->getIssuer() != *raSrcAccount)
                    {
                        sciSourceCurrencies.insert(
                            {srcCurrencyID, saSendMax->getIssuer()});
                    }
                    else
                    {
                        sciSourceCurrencies.insert(
                            {srcCurrencyID, *raSrcAccount});
                    }
                }
            }
            else
            {
                sciSourceCurrencies.insert({srcCurrencyID, srcIssuerID});
            }
        }
    }

    if (jvParams.contains(jss::id.c_str()))
        jvId = jvParams.at(jss::id.c_str());

    return PFR_PJ_NOCHANGE;
}

boost::json::object
PathRequest::doClose()
{
    JLOG(m_journal.debug()) << iIdentifier << " closed";
    std::lock_guard sl(mLock);
    jvStatus[jss::closed.c_str()] = true;
    return jvStatus;
}

boost::json::object
PathRequest::doStatus(boost::json::value const&)
{
    std::lock_guard sl(mLock);
    jvStatus[jss::status.c_str()] = jss::success;
    return jvStatus;
}

void
PathRequest::doAborting() const
{
    JLOG(m_journal.info()) << iIdentifier << " aborting early";
}

std::unique_ptr<Pathfinder> const&
PathRequest::getPathFinder(
    std::shared_ptr<RippleLineCache> const& cache,
    hash_map<Currency, std::unique_ptr<Pathfinder>>& currency_map,
    Currency const& currency,
    STAmount const& dst_amount,
    int const level,
    std::function<bool(void)> const& continueCallback)
{
    auto i = currency_map.find(currency);
    if (i != currency_map.end())
        return i->second;
    auto pathfinder = std::make_unique<Pathfinder>(
        cache,
        *raSrcAccount,
        *raDstAccount,
        currency,
        std::nullopt,
        dst_amount,
        saSendMax,
        app_);
    if (pathfinder->findPaths(level, continueCallback))
        pathfinder->computePathRanks(max_paths_, continueCallback);
    else
        pathfinder.reset();  // It's a bad request - clear it.
    return currency_map[currency] = std::move(pathfinder);
}

bool
PathRequest::findPaths(
    std::shared_ptr<RippleLineCache> const& cache,
    int const level,
    boost::json::array& jvArray,
    std::function<bool(void)> const& continueCallback)
{
    auto sourceCurrencies = sciSourceCurrencies;
    if (sourceCurrencies.empty() && saSendMax)
    {
        sourceCurrencies.insert(saSendMax->issue());
    }
    if (sourceCurrencies.empty())
    {
        auto currencies = accountSourceCurrencies(*raSrcAccount, cache, true);
        bool const sameAccount = *raSrcAccount == *raDstAccount;
        for (auto const& c : currencies)
        {
            if (!sameAccount || c != saDstAmount.getCurrency())
            {
                if (sourceCurrencies.size() >= RPC::Tuning::max_auto_src_cur)
                    return false;
                sourceCurrencies.insert(
                    {c, c.isZero() ? xrpAccount() : *raSrcAccount});
            }
        }
    }

    auto const dst_amount = convertAmount(saDstAmount, convert_all_);
    hash_map<Currency, std::unique_ptr<Pathfinder>> currency_map;
    for (auto const& issue : sourceCurrencies)
    {
        if (continueCallback && !continueCallback())
            break;
        JLOG(m_journal.debug())
            << iIdentifier
            << " Trying to find paths: " << STAmount(issue, 1).getFullText();

        auto& pathfinder = getPathFinder(
            cache,
            currency_map,
            issue.currency,
            dst_amount,
            level,
            continueCallback);
        if (!pathfinder)
        {
            assert(continueCallback && !continueCallback());
            JLOG(m_journal.debug()) << iIdentifier << " No paths found";
            continue;
        }

        STPath fullLiquidityPath;
        auto ps = pathfinder->getBestPaths(
            max_paths_,
            fullLiquidityPath,
            mContext[issue],
            issue.account,
            continueCallback);
        mContext[issue] = ps;

        auto const& sourceAccount = [&] {
            if (!isXRP(issue.account))
                return issue.account;

            if (isXRP(issue.currency))
                return xrpAccount();

            return *raSrcAccount;
        }();

        STAmount saMaxAmount = saSendMax.value_or(
            STAmount({issue.currency, sourceAccount}, 1u, 0, true));

        JLOG(m_journal.debug())
            << iIdentifier << " Paths found, calling rippleCalc";

        path::RippleCalc::Input rcInput;
        if (convert_all_)
            rcInput.partialPaymentAllowed = true;
        auto sandbox =
            std::make_unique<PaymentSandbox>(&*cache->getLedger(), tapNONE);
        auto rc = path::RippleCalc::rippleCalculate(
            *sandbox,
            saMaxAmount,    // --> Amount to send is unlimited
                            //     to get an estimate.
            dst_amount,     // --> Amount to deliver.
            *raDstAccount,  // --> Account to deliver to.
            *raSrcAccount,  // --> Account sending from.
            ps,             // --> Path set.
            app_.logs(),
            &rcInput);

        if (!convert_all_ && !fullLiquidityPath.empty() &&
            (rc.result() == terNO_LINE || rc.result() == tecPATH_PARTIAL))
        {
            JLOG(m_journal.debug())
                << iIdentifier << " Trying with an extra path element";

            ps.push_back(fullLiquidityPath);
            sandbox =
                std::make_unique<PaymentSandbox>(&*cache->getLedger(), tapNONE);
            rc = path::RippleCalc::rippleCalculate(
                *sandbox,
                saMaxAmount,    // --> Amount to send is unlimited
                                //     to get an estimate.
                dst_amount,     // --> Amount to deliver.
                *raDstAccount,  // --> Account to deliver to.
                *raSrcAccount,  // --> Account sending from.
                ps,             // --> Path set.
                app_.logs());

            if (rc.result() != tesSUCCESS)
            {
                JLOG(m_journal.warn())
                    << iIdentifier << " Failed with covering path "
                    << transHuman(rc.result());
            }
            else
            {
                JLOG(m_journal.debug())
                    << iIdentifier << " Extra path element gives "
                    << transHuman(rc.result());
            }
        }

        if (rc.result() == tesSUCCESS)
        {
            boost::json::object jvEntry;
            rc.actualAmountIn.setIssuer(sourceAccount);
            jvEntry[jss::source_amount.c_str()] =
                rc.actualAmountIn.getJson(JsonOptions::none);
            jvEntry[jss::paths_computed.c_str()] = ps.getJson(JsonOptions::none);

            if (convert_all_)
                jvEntry[jss::destination_amount.c_str()] =
                    rc.actualAmountOut.getJson(JsonOptions::none);

            if (hasCompletion())
            {
                // Old ripple_path_find API requires this
                jvEntry[jss::paths_canonical.c_str()] = Json::arrayValue;
            }

            jvArray.emplace_back(jvEntry);
        }
        else
        {
            JLOG(m_journal.debug()) << iIdentifier << " rippleCalc returns "
                                    << transHuman(rc.result());
        }
    }

    /*  The resource fee is based on the number of source currencies used.
        The minimum cost is 50 and the maximum is 400. The cost increases
        after four source currencies, 50 - (4 * 4) = 34.
    */
    int const size = sourceCurrencies.size();
    consumer_.charge({std::clamp(size * size + 34, 50, 400), "path update"});
    return true;
}

boost::json::object
PathRequest::doUpdate(
    std::shared_ptr<RippleLineCache> const& cache,
    bool fast,
    std::function<bool(void)> const& continueCallback)
{
    using namespace std::chrono;
    JLOG(m_journal.debug())
        << iIdentifier << " update " << (fast ? "fast" : "normal");

    {
        std::lock_guard sl(mLock);

        if (!isValid(cache))
            return jvStatus;
    }

    boost::json::object newStatus;

    if (hasCompletion())
    {
        // Old ripple_path_find API gives destination_currencies
        boost::json::array& destCurrencies =
            (newStatus[jss::destination_currencies.c_str()].emplace_array());
        auto usCurrencies = accountDestCurrencies(*raDstAccount, cache, true);
        for (auto const& c : usCurrencies)
            destCurrencies.emplace_back(to_string(c));
    }

    newStatus[jss::source_account.c_str()] = toBase58(*raSrcAccount);
    newStatus[jss::destination_account.c_str()] = toBase58(*raDstAccount);
    newStatus[jss::destination_amount.c_str()] = saDstAmount.getJson(JsonOptions::none);
    newStatus[jss::full_reply.c_str()] = !fast;

    if (!jvId.is_null())
        newStatus[jss::id.c_str()] = jvId;

    bool loaded = app_.getFeeTrack().isLoadedLocal();

    if (iLevel == 0)
    {
        // first pass
        if (loaded || fast)
            iLevel = app_.config().PATH_SEARCH_FAST;
        else
            iLevel = app_.config().PATH_SEARCH;
    }
    else if ((iLevel == app_.config().PATH_SEARCH_FAST) && !fast)
    {
        // leaving fast pathfinding
        iLevel = app_.config().PATH_SEARCH;
        if (loaded && (iLevel > app_.config().PATH_SEARCH_FAST))
            --iLevel;
    }
    else if (bLastSuccess)
    {
        // decrement, if possible
        if (iLevel > app_.config().PATH_SEARCH ||
            (loaded && (iLevel > app_.config().PATH_SEARCH_FAST)))
            --iLevel;
    }
    else
    {
        // adjust as needed
        if (!loaded && (iLevel < app_.config().PATH_SEARCH_MAX))
            ++iLevel;
        if (loaded && (iLevel > app_.config().PATH_SEARCH_FAST))
            --iLevel;
    }

    JLOG(m_journal.debug()) << iIdentifier << " processing at level " << iLevel;

    boost::json::array jvArray;
    if (findPaths(cache, iLevel, jvArray, continueCallback))
    {
        bLastSuccess = jvArray.size() != 0;
        newStatus[jss::alternatives.c_str()] = std::move(jvArray);
    }
    else
    {
        bLastSuccess = false;
        newStatus = rpcError(rpcINTERNAL);
    }

    if (fast && quick_reply_ == steady_clock::time_point{})
    {
        quick_reply_ = steady_clock::now();
        mOwner.reportFast(duration_cast<milliseconds>(quick_reply_ - created_));
    }
    else if (!fast && full_reply_ == steady_clock::time_point{})
    {
        full_reply_ = steady_clock::now();
        mOwner.reportFull(duration_cast<milliseconds>(full_reply_ - created_));
    }

    {
        std::lock_guard sl(mLock);
        jvStatus = newStatus;
    }

    JLOG(m_journal.debug())
        << iIdentifier << " update finished " << (fast ? "fast" : "normal");
    return newStatus;
}

InfoSub::pointer
PathRequest::getSubscriber() const
{
    return wpSubscriber.lock();
}

}  // namespace ripple
