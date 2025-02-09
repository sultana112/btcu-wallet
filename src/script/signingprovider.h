// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2019 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_SCRIPT_SIGNINGPROVIDER_H
#define BITCOIN_SCRIPT_SIGNINGPROVIDER_H

#include <key.h>
#include <pubkey.h>
#include <script/script.h>
#include <script/standard.h>
#include <sync.h>
#include "crypter.h"

//struct KeyOriginInfo;
//
///** An interface to be implemented by keystores that support signing. */
//class SigningProvider
//{
//public:
//    virtual ~SigningProvider() {}
//    virtual bool GetCScript(const CScriptID &scriptid, CScript& script) const { return false; }
//    virtual bool HaveCScript(const CScriptID &scriptid) const { return false; }
//    virtual bool GetPubKey(const CKeyID &address, CPubKey& pubkey) const { return false; }
//    virtual bool GetKey(const CKeyID &address, CKey& key) const { return false; }
//    virtual bool HaveKey(const CKeyID &address) const { return false; }
//    virtual bool GetKeyOrigin(const CKeyID& keyid, KeyOriginInfo& info) const { return false; }
//};
//
//extern const SigningProvider& DUMMY_SIGNING_PROVIDER;
//
//class HidingSigningProvider : public SigningProvider
//{
//private:
//    const bool m_hide_secret;
//    const bool m_hide_origin;
//    const SigningProvider* m_provider;
//
//public:
//    HidingSigningProvider(const SigningProvider* provider, bool hide_secret, bool hide_origin) : m_hide_secret(hide_secret), m_hide_origin(hide_origin), m_provider(provider) {}
//    bool GetCScript(const CScriptID& scriptid, CScript& script) const override;
//    bool GetPubKey(const CKeyID& keyid, CPubKey& pubkey) const override;
//    bool GetKey(const CKeyID& keyid, CKey& key) const override;
//    bool GetKeyOrigin(const CKeyID& keyid, KeyOriginInfo& info) const override;
//};
//
//struct FlatSigningProvider final : public SigningProvider
//{
//    std::map<CScriptID, CScript> scripts;
//    std::map<CKeyID, CPubKey> pubkeys;
//    std::map<CKeyID, std::pair<CPubKey, KeyOriginInfo>> origins;
//    std::map<CKeyID, CKey> keys;
//
//    bool GetCScript(const CScriptID& scriptid, CScript& script) const override;
//    bool GetPubKey(const CKeyID& keyid, CPubKey& pubkey) const override;
//    bool GetKeyOrigin(const CKeyID& keyid, KeyOriginInfo& info) const override;
//    bool GetKey(const CKeyID& keyid, CKey& key) const override;
//};
//
//FlatSigningProvider Merge(const FlatSigningProvider& a, const FlatSigningProvider& b);

/** Fillable signing provider that keeps keys in an address->secret map */


/** Return the CKeyID of the key involved in a script (if there is a unique one). */
CKeyID GetKeyForDestination(const CCryptoKeyStore& store, const CTxDestination& dest);

#endif // BITCOIN_SCRIPT_SIGNINGPROVIDER_H
