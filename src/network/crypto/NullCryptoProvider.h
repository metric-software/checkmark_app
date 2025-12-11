#ifndef NULLCRYPTOPROVIDER_H
#define NULLCRYPTOPROVIDER_H

// NullCryptoProvider - Pass-through no-encryption implementation
// Used by: BaseApiClient (default crypto provider for unencrypted communication)
// Purpose: No-op encryption - data passes through unchanged for current local server setup
// When to use: Current default - replace with real crypto provider when server supports encryption
// Operations: Identity transformation only - no actual cryptographic operations

#include "ICryptoProvider.h"

class NullCryptoProvider : public ICryptoProvider {
public:
    CryptoAlgorithm getAlgorithm() const override;
    QString getName() const override;
    
    EncryptionResult encrypt(const QByteArray& data, const QByteArray& publicKey) override;
    DecryptionResult decrypt(const QByteArray& encryptedData, const QByteArray& privateKey) override;
    
    bool isReady() const override;
    QString getLastError() const override;

private:
    QString m_lastError;
};

#endif // NULLCRYPTOPROVIDER_H