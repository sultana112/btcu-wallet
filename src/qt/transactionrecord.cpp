// Copyright (c) 2011-2014 The Bitcoin developers
// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2020 The PIVX developers
// Copyright (c) 2020 The BTCU developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "transactionrecord.h"

#include "obfuscation.h"
#include "swifttx.h"
#include "timedata.h"
#include "wallet/wallet.h"
#include "zbtcuchain.h"
#include "main.h"

#include <algorithm>
#include <stdint.h>

/*
 * Decompose CWallet transaction to model transaction records.
 */
QList<TransactionRecord> TransactionRecord::decomposeTransaction(const CWallet* wallet, const CWalletTx& wtx)
{
    QList<TransactionRecord> parts;
    int64_t nTime = wtx.GetComputedTxTime();
    CAmount nCredit = wtx.GetCredit(ISMINE_ALL);
    CAmount nDebit = wtx.GetDebit(ISMINE_ALL);
    CAmount nNet = nCredit - nDebit;
    uint256 hash = wtx.GetHash();
    std::map<std::string, std::string> mapValue = wtx.mapValue;
    bool fZSpendFromMe = false;

    if (wtx.HasZerocoinSpendInputs()) {
        libzerocoin::CoinSpend zcspend = wtx.HasZerocoinPublicSpendInputs() ? ZBTCUModule::parseCoinSpend(wtx.vin[0]) : TxInToZerocoinSpend(wtx.vin[0]);
        fZSpendFromMe = wallet->IsMyZerocoinSpend(zcspend.getCoinSerialNumber());
    }

    if (wtx.IsCoinStake()) {
        TransactionRecord sub(hash, nTime, wtx.GetTotalSize());

        CTxDestination address;
        if (!wtx.HasZerocoinSpendInputs() && !ExtractDestination(wtx.vout[1].scriptPubKey, address))
            return parts;

        if (wtx.HasZerocoinSpendInputs() && (fZSpendFromMe || wallet->zbtcuTracker->HasMintTx(hash))) {
            //zBTCU stake reward
            sub.involvesWatchAddress = false;
            sub.type = TransactionRecord::StakeZBTCU;
            sub.address = mapValue["zerocoinmint"];
            sub.credit = 0;
            for (const CTxOut& out : wtx.vout) {
                if (out.IsZerocoinMint())
                    sub.credit += out.nValue;
            }
            sub.debit -= wtx.vin[0].nSequence * COIN;
        } else if (isminetype mine = wallet->IsMine(wtx.vout[1])) {

            // Check for cold stakes.
            if (wtx.HasP2CSOutputs()) {
                sub.credit = nCredit;
                sub.debit = -nDebit;
                loadHotOrColdStakeOrContract(wallet, wtx, sub);
                parts.append(sub);
                return parts;
            } else {
                // BTCU stake reward
                sub.involvesWatchAddress = mine & ISMINE_WATCH_ONLY;
                sub.type = TransactionRecord::StakeMint;
                sub.address = CBTCUAddress(address).ToString();
                sub.credit = nNet;
            }
        } else {
            //Masternode reward
            CTxDestination destMN;
            int nIndexMN = wtx.vout.size() - 1;
            if (ExtractDestination(wtx.vout[nIndexMN].scriptPubKey, destMN) && IsMine(*wallet, destMN)) {
                isminetype mine = wallet->IsMine(wtx.vout[nIndexMN]);
                sub.involvesWatchAddress = mine & ISMINE_WATCH_ONLY;
                sub.type = TransactionRecord::MNReward;
                sub.address = CBTCUAddress(destMN).ToString();
                sub.credit = wtx.vout[nIndexMN].nValue;
            }
        }

        parts.append(sub);
    } else if (wtx.HasZerocoinSpendInputs()) {
        //zerocoin spend outputs
        bool fFeeAssigned = false;
        for (const CTxOut& txout : wtx.vout) {
            // change that was reminted as zerocoins
            if (txout.IsZerocoinMint()) {
                // do not display record if this isn't from our wallet
                if (!fZSpendFromMe)
                    continue;

                isminetype mine = wallet->IsMine(txout);
                TransactionRecord sub(hash, nTime, wtx.GetTotalSize());
                sub.involvesWatchAddress = mine & ISMINE_WATCH_ONLY;
                sub.type = TransactionRecord::ZerocoinSpend_Change_zPiv;
                sub.address = mapValue["zerocoinmint"];
                if (!fFeeAssigned) {
                    sub.debit -= (wtx.GetZerocoinSpent() - wtx.GetValueOut());
                    fFeeAssigned = true;
                }
                sub.idx = parts.size();
                parts.append(sub);
                continue;
            }

            std::string strAddress = "";
            CTxDestination address;
            if (ExtractDestination(txout.scriptPubKey, address))
                strAddress = CBTCUAddress(address).ToString();

            // a zerocoinspend that was sent to an address held by this wallet
            isminetype mine = wallet->IsMine(txout);
            if (mine) {
                TransactionRecord sub(hash, nTime, wtx.GetTotalSize());
                sub.involvesWatchAddress = mine & ISMINE_WATCH_ONLY;
                if (fZSpendFromMe) {
                    sub.type = TransactionRecord::ZerocoinSpend_FromMe;
                } else {
                    sub.type = TransactionRecord::RecvFromZerocoinSpend;
                    sub.credit = txout.nValue;
                }
                sub.address = mapValue["recvzerocoinspend"];
                if (strAddress != "")
                    sub.address = strAddress;
                sub.idx = parts.size();
                parts.append(sub);
                continue;
            }

            // spend is not from us, so do not display the spend side of the record
            if (!fZSpendFromMe)
                continue;

            // zerocoin spend that was sent to someone else
            TransactionRecord sub(hash, nTime, wtx.GetTotalSize());
            sub.involvesWatchAddress = mine & ISMINE_WATCH_ONLY;
            sub.debit = -txout.nValue;
            sub.type = TransactionRecord::ZerocoinSpend;
            sub.address = mapValue["zerocoinspend"];
            if (strAddress != "")
                sub.address = strAddress;
            sub.idx = parts.size();
            parts.append(sub);
        }
    } else if (wtx.HasP2CSOutputs()) {
        // Delegate tx.
        TransactionRecord sub(hash, nTime, wtx.GetTotalSize());
        sub.credit = nCredit;
        sub.debit = -nDebit;
        loadHotOrColdStakeOrContract(wallet, wtx, sub, true);
        parts.append(sub);
        return parts;
    } else if (wtx.HasP2CSInputs()) {
        // Delegation unlocked
        TransactionRecord sub(hash, nTime, wtx.GetTotalSize());
        loadUnlockColdStake(wallet, wtx, sub);
        parts.append(sub);
        return parts;
    } else if (wtx.HasP2LOutputs()) {
        // Leasing tx.
        TransactionRecord sub(hash, nTime, wtx.GetTotalSize());
        sub.credit = nCredit;
        sub.debit = -nDebit;
        loadP2L(wallet, wtx, sub);
        parts.append(sub);
        return parts;
    } else if (wtx.HasP2LInputs()) {
        // Leaasing unlocked
        TransactionRecord sub(hash, nTime, wtx.GetTotalSize());
        sub.credit = nCredit;
        sub.debit = -nDebit;
        loadLeasingSpend(wallet, wtx, sub);
        parts.append(sub);
        return parts;
    } else if (wtx.IsLeasingReward()) {
        // Leasing reward tx.
        TransactionRecord sub(hash, nTime, wtx.GetTotalSize());
        sub.credit = nCredit;
        sub.debit = -nDebit;
        loadLeasingReward(wallet, wtx, sub);
        parts.append(sub);
        return parts;
    } else if (nNet > 0 || wtx.IsCoinBase()) {
        //
        // Credit
        //
        for (const CTxOut& txout : wtx.vout) {
            isminetype mine = wallet->IsMine(txout);
            if (mine) {
                TransactionRecord sub(hash, nTime, wtx.GetTotalSize());
                CTxDestination address;
                sub.idx = parts.size(); // sequence number
                sub.credit = txout.nValue;
                sub.involvesWatchAddress = mine & ISMINE_WATCH_ONLY;
                if (ExtractDestination(txout.scriptPubKey, address) && IsMine(*wallet, address)) {
                    // Received by BTCU Address
                    sub.type = TransactionRecord::RecvWithAddress;
                    sub.address = CBTCUAddress(address).ToString();
                } else {
                    // Received by IP connection (deprecated features), or a multisignature or other non-simple transaction
                    sub.type = TransactionRecord::RecvFromOther;
                    sub.address = mapValue["from"];
                }
                if (wtx.IsCoinBase()) {
                    // Generated
                    sub.type = TransactionRecord::Generated;
                }

                parts.append(sub);
            }
        }
    } else {
        bool fAllFromMeDenom = true;
        int nFromMe = 0;
        bool involvesWatchAddress = false;
        isminetype fAllFromMe = ISMINE_SPENDABLE;
        for (const CTxIn& txin : wtx.vin) {
            if (wallet->IsMine(txin)) {
                fAllFromMeDenom = fAllFromMeDenom && wallet->IsDenominated(txin);
                nFromMe++;
            }
            isminetype mine = wallet->IsMine(txin);
            if (mine & ISMINE_WATCH_ONLY) involvesWatchAddress = true;
            if (fAllFromMe > mine) fAllFromMe = mine;
        }

        isminetype fAllToMe = ISMINE_SPENDABLE;
        bool fAllToMeDenom = true;
        int nToMe = 0;
        for (const CTxOut& txout : wtx.vout) {
            if (wallet->IsMine(txout)) {
                fAllToMeDenom = fAllToMeDenom && wallet->IsDenominatedAmount(txout.nValue);
                nToMe++;
            }
            isminetype mine = wallet->IsMine(txout);
            if (mine & ISMINE_WATCH_ONLY) involvesWatchAddress = true;
            if (fAllToMe > mine) fAllToMe = mine;
        }

        if (fAllFromMeDenom && fAllToMeDenom && ((nFromMe * nToMe) != 0)) {
            parts.append(TransactionRecord(hash, nTime, wtx.GetTotalSize(), TransactionRecord::ObfuscationDenominate, "", -nDebit, nCredit));
            parts.last().involvesWatchAddress = false; // maybe pass to TransactionRecord as constructor argument
        } else if (fAllFromMe && fAllToMe) {
            // Payment to self
            // TODO: this section still not accurate but covers most cases,
            // might need some additional work however

            TransactionRecord sub(hash, nTime, wtx.GetTotalSize());
            // Payment to self by default
            sub.type = TransactionRecord::SendToSelf;
            sub.address = "";

            if (mapValue["DS"] == "1") {
                sub.type = TransactionRecord::Obfuscated;
                CTxDestination address;
                if (ExtractDestination(wtx.vout[0].scriptPubKey, address)) {
                    // Sent to BTCU Address
                    sub.address = CBTCUAddress(address).ToString();
                } else {
                    // Sent to IP, or other non-address transaction like OP_EVAL
                    sub.address = mapValue["to"];
                }
            } else {
                for (unsigned int nOut = 0; nOut < wtx.vout.size(); nOut++) {
                    const CTxOut& txout = wtx.vout[nOut];
                    sub.idx = parts.size();

                    if (wallet->IsCollateralAmount(txout.nValue)) sub.type = TransactionRecord::ObfuscationMakeCollaterals;
                    if (wallet->IsDenominatedAmount(txout.nValue)) sub.type = TransactionRecord::ObfuscationCreateDenominations;
                    if (nDebit - wtx.GetValueOut() == OBFUSCATION_COLLATERAL) sub.type = TransactionRecord::ObfuscationCollateralPayment;
                }

                // Label for payment to self
                CTxDestination address;
                if (ExtractDestination(wtx.vout[0].scriptPubKey, address)) {
                    sub.address = CBTCUAddress(address).ToString();
                }
            }

            CAmount nChange = wtx.GetChange();

            sub.debit = -(nDebit - nChange);
            sub.credit = nCredit - nChange;
            parts.append(sub);
            parts.last().involvesWatchAddress = involvesWatchAddress; // maybe pass to TransactionRecord as constructor argument
        } else if (fAllFromMe || wtx.HasZerocoinMintOutputs()) {
            //
            // Debit
            //
            CAmount nTxFee = nDebit - wtx.GetValueOut();

            for (unsigned int nOut = 0; nOut < wtx.vout.size(); nOut++) {
                const CTxOut& txout = wtx.vout[nOut];
                TransactionRecord sub(hash, nTime, wtx.GetTotalSize());
                sub.idx = parts.size();
                sub.involvesWatchAddress = involvesWatchAddress;

                if (wallet->IsMine(txout)) {
                    // Ignore parts sent to self, as this is usually the change
                    // from a transaction sent back to our own address.
                    continue;
                }

                CTxDestination address;
                if (ExtractDestination(txout.scriptPubKey, address)) {
                    //This is most likely only going to happen when resyncing deterministic wallet without the knowledge of the
                    //private keys that the change was sent to. Do not display a "sent to" here.
                    if (wtx.HasZerocoinMintOutputs())
                        continue;
                    // Sent to BTCU Address
                    sub.type = TransactionRecord::SendToAddress;
                    sub.address = CBTCUAddress(address).ToString();
                } else if (txout.IsZerocoinMint()){
                    sub.type = TransactionRecord::ZerocoinMint;
                    sub.address = mapValue["zerocoinmint"];
                    sub.credit += txout.nValue;
                } else {
                    // Sent to IP, or other non-address transaction like OP_EVAL
                    sub.type = TransactionRecord::SendToOther;
                    sub.address = mapValue["to"];
                }

                if (mapValue["DS"] == "1") {
                    sub.type = TransactionRecord::Obfuscated;
                }

                CAmount nValue = txout.nValue;
                /* Add fee to first output */
                if (nTxFee > 0) {
                    nValue += nTxFee;
                    nTxFee = 0;
                }
                sub.debit = -nValue;

                parts.append(sub);
            }
        } else {
            //
            // Mixed debit transaction, can't break down payees
            //
            parts.append(TransactionRecord(hash, nTime, wtx.GetTotalSize(), TransactionRecord::Other, "", nNet, 0));
            parts.last().involvesWatchAddress = involvesWatchAddress;
        }
    }

    return parts;
}

void TransactionRecord::loadUnlockColdStake(const CWallet* wallet, const CWalletTx& wtx, TransactionRecord& record)
{
    record.involvesWatchAddress = false;

    // Get the p2cs
    const CScript* p2csScript = nullptr;
    bool isSpendable = false;

    for (const auto &input : wtx.vin) {
        const CWalletTx* tx = wallet->GetWalletTx(input.prevout.hash);
        if (tx && tx->vout[input.prevout.n].scriptPubKey.IsPayToColdStaking()) {
            p2csScript = &tx->vout[input.prevout.n].scriptPubKey;
            isSpendable = wallet->IsMine(input) & ISMINE_SPENDABLE_ALL;
            break;
        }
    }

    if (isSpendable) {
        // owner unlocked the cold stake
        record.type = TransactionRecord::P2CSUnlockOwner;
        record.debit = -(wtx.GetStakeDelegationDebit());
        record.credit = wtx.GetCredit(ISMINE_ALL);
    } else {
        // hot node watching the unlock
        record.type = TransactionRecord::P2CSUnlockStaker;
        record.debit = -(wtx.GetColdStakingDebit());
        record.credit = -(wtx.GetColdStakingCredit());
    }

    // Extract and set the owner address
    ExtractAddress(*p2csScript, false, false, record.address);
}

void TransactionRecord::loadHotOrColdStakeOrContract(
        const CWallet* wallet,
        const CWalletTx& wtx,
        TransactionRecord& record,
        bool isContract)
{
    record.involvesWatchAddress = false;

    // Get the p2cs
    CTxOut p2csUtxo;
    for (unsigned int nOut = 0; nOut < wtx.vout.size(); nOut++) {
        const CTxOut &txout = wtx.vout[nOut];
        if (txout.scriptPubKey.IsPayToColdStaking()) {
            p2csUtxo = txout;
            break;
        }
    }

    bool isSpendable = (wallet->IsMine(p2csUtxo) & ISMINE_SPENDABLE_DELEGATED);
    bool isFromMe = wallet->IsFromMe(wtx);

    if (isContract) {
        if (isSpendable && isFromMe) {
            // Wallet delegating balance
            record.type = TransactionRecord::P2CSDelegationSentOwner;
        } else if (isFromMe){
            // Wallet delegating balance and transfering ownership
            record.type = TransactionRecord::P2CSDelegationSent;
        } else {
            // Wallet receiving a delegation
            record.type = TransactionRecord::P2CSDelegation;
        }
    } else {
        // Stake
        if (isSpendable) {
            // Offline wallet receiving an stake due a delegation
            record.type = TransactionRecord::StakeDelegated;
            record.credit = wtx.GetCredit(ISMINE_SPENDABLE_DELEGATED);
            record.debit = -(wtx.GetDebit(ISMINE_SPENDABLE_DELEGATED));
        } else {
            // Online wallet receiving an stake due a received utxo delegation that won a block.
            record.type = TransactionRecord::StakeHot;
        }
    }

    // Extract and set the owner address
    ExtractAddress(p2csUtxo.scriptPubKey, false, false, record.address);
}

void TransactionRecord::loadP2L(
    const CWallet* wallet,
    const CWalletTx& wtx,
    TransactionRecord& record)
{
    record.involvesWatchAddress = false;

    for (unsigned int nOut = 0; nOut < wtx.vout.size(); nOut++) {
        const CTxOut &txout = wtx.vout[nOut];
        if (txout.scriptPubKey.IsPayToLeasing()) {
            const auto isMine = wallet->IsMine(txout);
            if (isMine == ISMINE_SPENDABLE_LEASING) {
                record.type = TransactionRecord::P2LLeasingSentToSelf;
                record.credit -= wtx.GetCredit(ISMINE_LEASED);
            } else if (isMine == ISMINE_LEASED) {
                record.type = TransactionRecord::P2LLeasingSent;
                record.debit -= wtx.GetCredit(ISMINE_LEASED);
            } else if (isMine == ISMINE_LEASING){
                record.type = TransactionRecord::P2LLeasingRecv;
                record.debit = -record.debit + wtx.GetCredit(ISMINE_LEASED);
            } else {
                continue;
            }

            ExtractAddress(txout.scriptPubKey, false, false, record.address);
            return;
        }
    }
}

void TransactionRecord::loadLeasingSpend(
    const CWallet* wallet,
    const CWalletTx& wtx,
    TransactionRecord& record)
{
    record.involvesWatchAddress = false;

    // Get the p2l
    for (const auto &input : wtx.vin) {
        const CWalletTx* tx = wallet->GetWalletTx(input.prevout.hash);
        if (tx && tx->vout[input.prevout.n].scriptPubKey.IsPayToLeasing()) {
            auto& p2lScript = tx->vout[input.prevout.n].scriptPubKey;
            const auto isMine = wallet->IsMine(tx->vout[input.prevout.n]);

            if (isMine == ISMINE_SPENDABLE_LEASING) {
                record.type = TransactionRecord::P2LUnlockOwnLeasing;
            } else if (isMine == ISMINE_LEASED) {
                record.type = TransactionRecord::P2LUnlockLeasing;
            } else if (isMine == ISMINE_LEASING){
                record.type = TransactionRecord::P2LReturnLeasing;
            } else {
                continue;
            }

            ExtractAddress(p2lScript, false, false, record.address);
            return;
        }
    }
}

void TransactionRecord::loadLeasingReward(
    const CWallet* wallet,
    const CWalletTx& wtx,
    TransactionRecord& record)
{
    record.involvesWatchAddress = false;

    // Get the leasing reward
    for (unsigned int nOut = 0; nOut < wtx.vout.size(); nOut++) {
        const CTxOut &txout = wtx.vout[nOut];
        if (txout.scriptPubKey.IsLeasingReward() && wallet->IsMine(txout) == ISMINE_SPENDABLE) {
            record.type = TransactionRecord::LeasingReward;
            ExtractAddress(txout.scriptPubKey, false, false, record.address);
            return;
        }
    }
}

bool TransactionRecord::ExtractAddress(const CScript& scriptPubKey, bool fColdStake, bool fLease, std::string& addressStr) {
    CTxDestination address;
    if (!ExtractDestination(scriptPubKey, address, fColdStake, fLease)) {
        // this shouldn't happen..
        addressStr = "No available address";
        return false;
    } else {
        addressStr = CBTCUAddress(
                address,
                (fColdStake ? CChainParams::STAKING_ADDRESS : CChainParams::PUBKEY_ADDRESS)
        ).ToString();
        return true;
    }
}

bool IsZBTCUType(TransactionRecord::Type type)
{
    switch (type) {
        case TransactionRecord::StakeZBTCU:
        case TransactionRecord::ZerocoinMint:
        case TransactionRecord::ZerocoinSpend:
        case TransactionRecord::RecvFromZerocoinSpend:
        case TransactionRecord::ZerocoinSpend_Change_zPiv:
        case TransactionRecord::ZerocoinSpend_FromMe:
            return true;
        default:
            return false;
    }
}

void TransactionRecord::updateStatus(const CWalletTx& wtx)
{
    AssertLockHeld(cs_main);
    int chainHeight = chainActive.Height();

    CBlockIndex *pindex = nullptr;
    // Find the block the tx is in
    BlockMap::iterator mi = mapBlockIndex.find(wtx.hashBlock);
    if (mi != mapBlockIndex.end())
        pindex = (*mi).second;

    // Determine transaction status

    // Sort order, unrecorded transactions sort to the top
    status.sortKey = strprintf("%010d-%01d-%010u-%03d",
        (pindex ? pindex->nHeight : std::numeric_limits<int>::max()),
        (wtx.IsCoinBase() ? 1 : 0),
        wtx.nTimeReceived,
        idx);

    bool fConflicted = false;
    int depth = 0;
    bool isTrusted = wtx.IsTrusted(depth, fConflicted);
    const bool isOffline = (GetAdjustedTime() - wtx.nTimeReceived > 2 * 60 && wtx.GetRequestCount() == 0);
    int nBlocksToMaturity = (wtx.IsCoinBase() || wtx.IsCoinStake()) ? std::max(0, (Params().COINBASE_MATURITY() + 1) - depth) : 0;

    status.countsForBalance = isTrusted && !(nBlocksToMaturity > 0);
    status.cur_num_blocks = chainHeight;
    status.depth = depth;
    status.cur_num_ix_locks = nCompleteTXLocks;

    if (!IsFinalTx(wtx, chainHeight + 1)) {
        if (wtx.nLockTime < LOCKTIME_THRESHOLD) {
            status.status = TransactionStatus::OpenUntilBlock;
            status.open_for = wtx.nLockTime - chainHeight;
        } else {
            status.status = TransactionStatus::OpenUntilDate;
            status.open_for = wtx.nLockTime;
        }
    }
    // For generated transactions, determine maturity
    else if (type == TransactionRecord::Generated ||
            type == TransactionRecord::StakeMint ||
            type == TransactionRecord::StakeZBTCU ||
            type == TransactionRecord::MNReward ||
            type == TransactionRecord::StakeDelegated ||
            type == TransactionRecord::StakeHot) {

        if (nBlocksToMaturity > 0) {
            status.status = TransactionStatus::Immature;
            status.matures_in = nBlocksToMaturity;

            if (status.depth >= 0 && !fConflicted) {
                // Check if the block was requested by anyone
                if (isOffline)
                    status.status = TransactionStatus::MaturesWarning;
            } else {
                status.status = TransactionStatus::NotAccepted;
            }
        } else {
            status.status = TransactionStatus::Confirmed;
            status.matures_in = 0;
        }
    } else {
        if (status.depth < 0 || fConflicted) {
            status.status = TransactionStatus::Conflicted;
        } else if (isOffline) {
            status.status = TransactionStatus::Offline;
        } else if (status.depth == 0) {
            status.status = TransactionStatus::Unconfirmed;
        } else if (status.depth < RecommendedNumConfirmations) {
            status.status = TransactionStatus::Confirming;
        } else {
            status.status = TransactionStatus::Confirmed;
        }
    }
}

bool TransactionRecord::statusUpdateNeeded()
{
    AssertLockHeld(cs_main);
    return status.cur_num_blocks != chainActive.Height() || status.cur_num_ix_locks != nCompleteTXLocks;
}

QString TransactionRecord::getTxID() const
{
    return QString::fromStdString(hash.ToString());
}

int TransactionRecord::getOutputIndex() const
{
    return idx;
}

bool TransactionRecord::isCoinStake() const
{
    return (type == TransactionRecord::StakeMint || type == TransactionRecord::Generated || type == TransactionRecord::StakeZBTCU);
}

bool TransactionRecord::isAnyColdStakingType() const
{
    return (type == TransactionRecord::P2CSDelegation || type == TransactionRecord::P2CSDelegationSent
            || type == TransactionRecord::P2CSDelegationSentOwner
            || type == TransactionRecord::StakeDelegated || type == TransactionRecord::StakeHot
            || type == TransactionRecord::P2CSUnlockOwner || type == TransactionRecord::P2CSUnlockStaker);
}

bool TransactionRecord::isAnyLeasingType() const
{
    return (type == TransactionRecord::P2LLeasingRecv || type == TransactionRecord::P2LLeasingSent || type == TransactionRecord::P2LLeasingSentToSelf
            || type == TransactionRecord::LeasingReward
            || type == TransactionRecord::P2LUnlockLeasing || type == TransactionRecord::P2LUnlockOwnLeasing || type == TransactionRecord::P2LReturnLeasing);
}

bool TransactionRecord::isNull() const
{
    return hash.IsNull() || size == 0;
}

std::string TransactionRecord::statusToString(){
    switch (status.status){
        case TransactionStatus::MaturesWarning:
            return "Abandoned (not mature because no nodes have confirmed)";
        case TransactionStatus::Confirmed:
            return "Confirmed";
        case TransactionStatus::OpenUntilDate:
            return "OpenUntilDate";
        case TransactionStatus::OpenUntilBlock:
            return "OpenUntilBlock";
        case TransactionStatus::Unconfirmed:
            return "Unconfirmed";
        case TransactionStatus::Confirming:
            return "Confirming";
        case TransactionStatus::Conflicted:
            return "Conflicted";
        case TransactionStatus::Immature:
            return "Immature";
        case TransactionStatus::NotAccepted:
            return "Not Accepted";
        default:
            return "No status";
    }
}
