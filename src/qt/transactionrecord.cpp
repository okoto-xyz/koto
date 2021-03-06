// Copyright (c) 2011-2015 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "transactionrecord.h"

#include "base58.h"
#include "key_io.h"
#include "consensus/consensus.h"
#include "main.h"
#include "timedata.h"
#include "wallet/wallet.h"

#include "univalue.h"
#include "rpc/server.h"

#include <stdint.h>

std::map<std::string, UniValue> TransactionRecord::ztxMap;

/* Return positive answer if transaction should be shown in list.
 */
bool TransactionRecord::showTransaction(const CWalletTx &wtx)
{
    if (wtx.IsCoinBase())
    {
        // Ensures we show generated coins / mined transactions at depth 1
        if (!wtx.IsInMainChain())
        {
            return false;
        }
    }
    return true;
}

bool TransactionRecord::findZTransaction(const CWallet *wallet, const uint256 &hash, CAmount &amout, std::string &address)
{
    std::set<libzcash::SproutPaymentAddress> addresses;
    wallet->GetSproutPaymentAddresses(addresses);
    KeyIO keyIO(Params());
    for (auto addr : addresses ) {
        if (wallet->HaveSproutSpendingKey(addr)) {
            std::string address = keyIO.EncodePaymentAddress(addr);
            UniValue ret;
            if (ztxMap.count(address)) {
                ret = ztxMap[address];
            } else {
                UniValue params(UniValue::VARR);
                params.push_back(address);
                params.push_back(0);
                ret = z_listreceivedbyaddress(params, false);
                ztxMap[address] = ret;
            }
            for (const UniValue& entry : ret.getValues()) {
                UniValue txid = find_value(entry, "txid");
                UniValue amount = find_value(entry, "amount");
                if(txid.get_str() == hash.GetHex())
                {
                    amout = AmountFromValue(amount);
                    address = keyIO.EncodePaymentAddress(addr);
                    return true;
                }
            }
        }
    }

    std::set<libzcash::SaplingPaymentAddress> addressesSapling;
    wallet->GetSaplingPaymentAddresses(addressesSapling);
    libzcash::SaplingIncomingViewingKey ivk;
    libzcash::SaplingExtendedFullViewingKey extfvk;
    for (auto addr : addressesSapling ) {
        if (wallet->GetSaplingIncomingViewingKey(addr, ivk) &&
            wallet->GetSaplingFullViewingKey(ivk, extfvk) &&
            wallet->HaveSaplingSpendingKey(extfvk)) {
            std::string address = keyIO.EncodePaymentAddress(addr);
            UniValue ret;
            if (ztxMap.count(address)) {
                ret = ztxMap[address];
            } else {
                UniValue params(UniValue::VARR);
                params.push_back(address);
                params.push_back(0);
                ret = z_listreceivedbyaddress(params, false);
                ztxMap[address] = ret;
            }
            for (const UniValue& entry : ret.getValues()) {
                UniValue txid = find_value(entry, "txid");
                UniValue amount = find_value(entry, "amount");
                if(txid.get_str() == hash.GetHex())
                {
                    amout = AmountFromValue(amount);
                    address = keyIO.EncodePaymentAddress(addr);
                    return true;
                }
            }
        }
    }
    return false;
}

/*
 * Decompose CWallet transaction to model transaction records.
 */
QList<TransactionRecord> TransactionRecord::decomposeTransaction(const CWallet *wallet, const CWalletTx &wtx)
{
    QList<TransactionRecord> parts;

    if(wtx.vJoinSplit.size() > 0)
    {
        parts = TransactionRecord::decomposeZTransaction(wallet, wtx);
    }
    else
    {
        parts = TransactionRecord::decomposeTTransaction(wallet, wtx);
    }

    return parts;
}

QList<TransactionRecord> TransactionRecord::decomposeZTransaction(const CWallet *wallet, const CWalletTx &wtx)
{
    QList<TransactionRecord> parts;

    CAmount nCredit = 0;
    CAmount nDebit = 0;
    CAmount nChange = wtx.GetChange();
    int64_t nTime = wtx.GetTxTime();
    uint256 hash = wtx.GetHash();
    std::string address;

    bool found = findZTransaction(wallet, hash, nCredit, address);
    KeyIO keyIO(Params());

    if(found)
    {
        // Received on Z-Addeess or Sent to T-Address or ?Sent to T-Address?
        if((wtx.vin.size() > 0) && (wtx.vout.size() > 0) && (wtx.mapSproutNoteData.size() > 0 || wtx.mapSaplingNoteData.size() > 0))
        {
            // Received Z<-T
            parts.append(TransactionRecord(hash, nTime, TransactionRecord::RecvWithAddress, address,
                            -(nDebit - nChange), nCredit - nChange));
        }
        else if((wtx.vin.size() == 0) && (wtx.vout.size() > 0) && (wtx.mapSproutNoteData.size() > 0 || wtx.mapSaplingNoteData.size() > 0))
        {
            // Sent Z->T
            for (const CTxOut& txout : wtx.vout)
            {
                nDebit += txout.nValue;
                CTxDestination tAddress;
                if (ExtractDestination(txout.scriptPubKey, tAddress))
                {
                    // Received by Koto Address
                    address = keyIO.EncodeDestination(tAddress);
                }

            }
            CAmount nValue = 0;
            for (auto js : wtx.vJoinSplit) {
                nValue += js.vpub_new;
            }
            
            nChange = nValue - nDebit;
            CAmount fee = AmountFromValue(5) - nChange;
            nCredit = fee;
            nDebit = -(nDebit);

            parts.append(TransactionRecord(hash, nTime, TransactionRecord::SendToAddress, address,
                            (nDebit - fee - nChange), nCredit));
        }
        else if((wtx.vin.size() == 0) && (wtx.vout.size() == 0) && (wtx.mapSproutNoteData.size() > 0 || wtx.mapSaplingNoteData.size() > 0))
        {
            // Received Z<-Z
            parts.append(TransactionRecord(hash, nTime, TransactionRecord::RecvWithAddress, address,
                            -(nDebit - nChange), nCredit - nChange));
        }
        else if((wtx.vin.size() > 0) && (wtx.vout.size() == 0) && (wtx.mapSproutNoteData.size() > 0 || wtx.mapSaplingNoteData.size() > 0))
        {
            // Shielding transaction
            nDebit = wtx.GetDebit(ISMINE_ALL);
            nDebit = -(nDebit);

            parts.append(TransactionRecord(hash, nTime, TransactionRecord::Shielded, address,
                            (nDebit), (nCredit)));
        }
        else
        {
            // this should never occur, but in case the record is processed by the original function
            parts = TransactionRecord::decomposeTTransaction(wallet, wtx);
            return parts;
        }

        parts.last().involvesWatchAddress = false;
    }
    else
    {
        // Sent or Received on T-Address
        if((wtx.vin.size() > 0) && (wtx.vout.size() > 0) && (wtx.mapSproutNoteData.size() == 0 && wtx.mapSaplingNoteData.size() == 0))
        {
            // Sent T->Z
            address = "Z Address not listed by wallet!";

            for (auto js : wtx.vJoinSplit) {
                nDebit += js.vpub_old;
            }

            nCredit = nChange;
            CAmount nTxFee = AmountFromValue(5) - nChange;
            nDebit = -(nDebit);

            parts.append(TransactionRecord(hash, nTime, TransactionRecord::SendToAddress, address,
                            (nDebit - nTxFee - nChange), nCredit));
        }
        else if((wtx.vin.size() == 0) && (wtx.vout.size() > 0) && (wtx.mapSproutNoteData.size() == 0 && wtx.mapSaplingNoteData.size() == 0))
        {
            // Received T<-Z
            for (const CTxOut& txout : wtx.vout)
            {
                nCredit += txout.nValue;
                CTxDestination tAddress;
                if (ExtractDestination(txout.scriptPubKey, tAddress))
                {
                    // Received by Koto Address
                    address = keyIO.EncodeDestination(tAddress);
                }
            }
            nDebit = 0;

            parts.append(TransactionRecord(hash, nTime, TransactionRecord::RecvWithAddress, address,
                            -(nDebit - nChange), nCredit - nChange));
        }
        else
        {
            // this should never occur, but in case the record is processed by the original function
            parts = TransactionRecord::decomposeTTransaction(wallet, wtx);
            return parts;
        }

        parts.last().involvesWatchAddress = false;
    }
    return parts;
}

QList<TransactionRecord> TransactionRecord::decomposeTTransaction(const CWallet *wallet, const CWalletTx &wtx)
{
    QList<TransactionRecord> parts;
    int64_t nTime = wtx.GetTxTime();
    CAmount nCredit = wtx.GetCredit(ISMINE_ALL);
    CAmount nDebit = wtx.GetDebit(ISMINE_ALL);
    CAmount nNet = nCredit - nDebit;
    uint256 hash = wtx.GetHash();
    std::map<std::string, std::string> mapValue = wtx.mapValue;
    KeyIO keyIO(Params());

    if (nNet > 0 || wtx.IsCoinBase())
    {
        //
        // Credit
        //
        for (const CTxOut& txout : wtx.vout)
        {
            isminetype mine = wallet->IsMine(txout);
            if(mine)
            {
                TransactionRecord sub(hash, nTime);
                CTxDestination address;
                sub.idx = parts.size(); // sequence number
                sub.credit = txout.nValue;
                sub.involvesWatchAddress = mine & ISMINE_WATCH_ONLY;
                if (ExtractDestination(txout.scriptPubKey, address) && IsMine(*wallet, address))
                {
                    // Received by Koto Address
                    sub.type = TransactionRecord::RecvWithAddress;
                    sub.address = keyIO.EncodeDestination(address);
                }
                else
                {
                    // Received by IP connection (deprecated features), or a multisignature or other non-simple transaction
                    sub.type = TransactionRecord::RecvFromOther;
                    sub.address = mapValue["from"];
                }
                if (wtx.IsCoinBase())
                {
                    // Generated
                    sub.type = TransactionRecord::Generated;
                }

                parts.append(sub);
            }
        }
    }
    else
    {
        bool involvesWatchAddress = false;
        isminetype fAllFromMe = ISMINE_SPENDABLE;
        for (const CTxIn& txin : wtx.vin)
        {
            isminetype mine = wallet->IsMine(txin);
            if(mine & ISMINE_WATCH_ONLY) involvesWatchAddress = true;
            if(fAllFromMe > mine) fAllFromMe = mine;
        }

        isminetype fAllToMe = ISMINE_SPENDABLE;
        for (const CTxOut& txout : wtx.vout)
        {
            isminetype mine = wallet->IsMine(txout);
            if(mine & ISMINE_WATCH_ONLY) involvesWatchAddress = true;
            if(fAllToMe > mine) fAllToMe = mine;
        }

        if (fAllFromMe && fAllToMe)
        {
            // Payment to self
            CAmount nChange = wtx.GetChange();

            parts.append(TransactionRecord(hash, nTime, TransactionRecord::SendToSelf, "",
                            -(nDebit - nChange), nCredit - nChange));
            parts.last().involvesWatchAddress = involvesWatchAddress;   // maybe pass to TransactionRecord as constructor argument
        }
        else if (fAllFromMe)
        {
            //
            // Debit
            //
            CAmount nTxFee = nDebit - wtx.GetValueOut();

            for (unsigned int nOut = 0; nOut < wtx.vout.size(); nOut++)
            {
                const CTxOut& txout = wtx.vout[nOut];
                TransactionRecord sub(hash, nTime);
                sub.idx = parts.size();
                sub.involvesWatchAddress = involvesWatchAddress;

                if(wallet->IsMine(txout))
                {
                    // Ignore parts sent to self, as this is usually the change
                    // from a transaction sent back to our own address.
                    continue;
                }

                CTxDestination address;
                if (ExtractDestination(txout.scriptPubKey, address))
                {
                    // Sent to Koto Address
                    sub.type = TransactionRecord::SendToAddress;
                    sub.address = keyIO.EncodeDestination(address);
                }
                else
                {
                    // Sent to IP, or other non-address transaction like OP_EVAL
                    sub.type = TransactionRecord::SendToOther;
                    sub.address = mapValue["to"];
                }

                CAmount nValue = txout.nValue;
                /* Add fee to first output */
                if (nTxFee > 0)
                {
                    nValue += nTxFee;
                    nTxFee = 0;
                }
                sub.debit = -nValue;

                parts.append(sub);
            }
        }
        else
        {
            //
            // Mixed debit transaction, can't break down payees
            //
            parts.append(TransactionRecord(hash, nTime, TransactionRecord::Other, "", nNet, 0));
            parts.last().involvesWatchAddress = involvesWatchAddress;
        }
    }

    return parts;
}

void TransactionRecord::updateStatus(const CWalletTx &wtx)
{
    AssertLockHeld(cs_main);
    // Determine transaction status

    // Find the block the tx is in
    CBlockIndex* pindex = NULL;
    BlockMap::iterator mi = mapBlockIndex.find(wtx.hashBlock);
    if (mi != mapBlockIndex.end())
        pindex = (*mi).second;

    // Sort order, unrecorded transactions sort to the top
    status.sortKey = strprintf("%010d-%01d-%010u-%03d",
        (pindex ? pindex->nHeight : std::numeric_limits<int>::max()),
        (wtx.IsCoinBase() ? 1 : 0),
        wtx.nTimeReceived,
        idx);
    status.countsForBalance = wtx.IsTrusted() && !(wtx.GetBlocksToMaturity() > 0);
    status.depth = wtx.GetDepthInMainChain();
    status.cur_num_blocks = chainActive.Height();

    if (!CheckFinalTx(wtx))
    {
        if (wtx.nLockTime < LOCKTIME_THRESHOLD)
        {
            status.status = TransactionStatus::OpenUntilBlock;
            status.open_for = wtx.nLockTime - chainActive.Height();
        }
        else
        {
            status.status = TransactionStatus::OpenUntilDate;
            status.open_for = wtx.nLockTime;
        }
    }
    // For generated transactions, determine maturity
    else if(type == TransactionRecord::Generated)
    {
        if (wtx.GetBlocksToMaturity() > 0)
        {
            status.status = TransactionStatus::Immature;

            if (wtx.IsInMainChain())
            {
                status.matures_in = wtx.GetBlocksToMaturity();

                // Check if the block was requested by anyone
                if (GetTime() - wtx.nTimeReceived > 2 * 60 && wtx.GetRequestCount() == 0)
                    status.status = TransactionStatus::MaturesWarning;
            }
            else
            {
                status.status = TransactionStatus::NotAccepted;
            }
        }
        else
        {
            status.status = TransactionStatus::Confirmed;
        }
    }
    else
    {
        if (status.depth < 0)
        {
            status.status = TransactionStatus::Conflicted;
        }
        else if (GetTime() - wtx.nTimeReceived > 2 * 60 && wtx.GetRequestCount() == 0)
        {
            status.status = TransactionStatus::Offline;
        }
        else if (status.depth == 0)
        {
            status.status = TransactionStatus::Unconfirmed;
        }
        else if (status.depth < RecommendedNumConfirmations)
        {
            status.status = TransactionStatus::Confirming;
        }
        else
        {
            status.status = TransactionStatus::Confirmed;
        }
    }

}

bool TransactionRecord::statusUpdateNeeded()
{
    AssertLockHeld(cs_main);
    return status.cur_num_blocks != chainActive.Height();
}

QString TransactionRecord::getTxID() const
{
    return QString::fromStdString(hash.ToString());
}

int TransactionRecord::getOutputIndex() const
{
    return idx;
}
