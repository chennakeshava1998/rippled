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

#ifndef RIPPLE_RPC_HANDLERS_HANDLERS_H_INCLUDED
#define RIPPLE_RPC_HANDLERS_HANDLERS_H_INCLUDED

#include <ripple/rpc/handlers/LedgerHandler.h>

namespace ripple {

boost::json::object
doAccountCurrencies(RPC::JsonContext&);
boost::json::object
doAccountInfo(RPC::JsonContext&);
boost::json::object
doAccountLines(RPC::JsonContext&);
boost::json::object
doAccountChannels(RPC::JsonContext&);
boost::json::object
doAccountNFTs(RPC::JsonContext&);
boost::json::object
doAccountObjects(RPC::JsonContext&);
boost::json::object
doAccountOffers(RPC::JsonContext&);
boost::json::object
doAccountTxJson(RPC::JsonContext&);
boost::json::object
doBookOffers(RPC::JsonContext&);
boost::json::object
doBookChanges(RPC::JsonContext&);
boost::json::object
doBlackList(RPC::JsonContext&);
boost::json::object
doCanDelete(RPC::JsonContext&);
boost::json::object
doChannelAuthorize(RPC::JsonContext&);
boost::json::object
doChannelVerify(RPC::JsonContext&);
boost::json::object
doConnect(RPC::JsonContext&);
boost::json::object
doConsensusInfo(RPC::JsonContext&);
boost::json::object
doDepositAuthorized(RPC::JsonContext&);
boost::json::object
doDownloadShard(RPC::JsonContext&);
boost::json::object
doFeature(RPC::JsonContext&);
boost::json::object
doFee(RPC::JsonContext&);
boost::json::object
doFetchInfo(RPC::JsonContext&);
boost::json::object
doGatewayBalances(RPC::JsonContext&);
boost::json::object
doGetCounts(RPC::JsonContext&);
boost::json::object
doLedgerAccept(RPC::JsonContext&);
boost::json::object
doLedgerCleaner(RPC::JsonContext&);
boost::json::object
doLedgerClosed(RPC::JsonContext&);
boost::json::object
doLedgerCurrent(RPC::JsonContext&);
boost::json::object
doLedgerData(RPC::JsonContext&);
boost::json::object
doLedgerEntry(RPC::JsonContext&);
boost::json::object
doLedgerHeader(RPC::JsonContext&);
boost::json::object
doLedgerRequest(RPC::JsonContext&);
boost::json::object
doLogLevel(RPC::JsonContext&);
boost::json::object
doLogRotate(RPC::JsonContext&);
boost::json::object
doManifest(RPC::JsonContext&);
boost::json::object
doNFTBuyOffers(RPC::JsonContext&);
boost::json::object
doNFTSellOffers(RPC::JsonContext&);
boost::json::object
doNodeToShard(RPC::JsonContext&);
boost::json::object
doNoRippleCheck(RPC::JsonContext&);
boost::json::object
doOwnerInfo(RPC::JsonContext&);
boost::json::object
doPathFind(RPC::JsonContext&);
boost::json::object
doPause(RPC::JsonContext&);
boost::json::object
doPeers(RPC::JsonContext&);
boost::json::object
doPing(RPC::JsonContext&);
boost::json::object
doPrint(RPC::JsonContext&);
boost::json::object
doRandom(RPC::JsonContext&);
boost::json::object
doResume(RPC::JsonContext&);
boost::json::object
doPeerReservationsAdd(RPC::JsonContext&);
boost::json::object
doPeerReservationsDel(RPC::JsonContext&);
boost::json::object
doPeerReservationsList(RPC::JsonContext&);
boost::json::object
doRipplePathFind(RPC::JsonContext&);
boost::json::object
doServerInfo(RPC::JsonContext&);  // for humans
boost::json::object
doServerState(RPC::JsonContext&);  // for machines
boost::json::object
doSign(RPC::JsonContext&);
boost::json::object
doSignFor(RPC::JsonContext&);
boost::json::object
doCrawlShards(RPC::JsonContext&);
boost::json::object
doStop(RPC::JsonContext&);
boost::json::object
doSubmit(RPC::JsonContext&);
boost::json::object
doSubmitMultiSigned(RPC::JsonContext&);
boost::json::object
doSubscribe(RPC::JsonContext&);
boost::json::object
doTransactionEntry(RPC::JsonContext&);
boost::json::object
doTxJson(RPC::JsonContext&);
boost::json::object
doTxHistory(RPC::JsonContext&);
boost::json::object
doTxReduceRelay(RPC::JsonContext&);
boost::json::object
doUnlList(RPC::JsonContext&);
boost::json::object
doUnsubscribe(RPC::JsonContext&);
boost::json::object
doValidationCreate(RPC::JsonContext&);
boost::json::object
doWalletPropose(RPC::JsonContext&);
boost::json::object
doValidators(RPC::JsonContext&);
boost::json::object
doValidatorListSites(RPC::JsonContext&);
boost::json::object
doValidatorInfo(RPC::JsonContext&);
}  // namespace ripple

#endif
