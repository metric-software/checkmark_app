#ifndef RSACRYPTOPROVIDER_H
#define RSACRYPTOPROVIDER_H

// RSACryptoProvider - Future RSA encryption implementation (stub)
// Used by: BaseApiClient when server provides RSA public keys for encryption
// Purpose: RSA-OAEP encryption for sensitive data transmission (not implemented yet)
// When to use: Future replacement for NullCryptoProvider when server supports RSA
// Operations: Public key encryption, private key decryption, key validation (TO BE IMPLEMENTED)

#include "ICryptoProvider.h"

class RSACryptoProvider : public ICryptoProvider {
public:
    RSACryptoProvider();
    
    CryptoAlgorithm getAlgorithm() const override;
    QString getName() const override;
    
    EncryptionResult encrypt(const QByteArray& data, const QByteArray& publicKey) override;
    DecryptionResult decrypt(const QByteArray& encryptedData, const QByteArray& privateKey) override;
    
    bool isReady() const override;
    QString getLastError() const override;

private:
    QString m_lastError;
    bool m_initialized;
    
    void initialize();
};

#endif // RSACRYPTOPROVIDER_H