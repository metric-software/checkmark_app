#ifndef ICRYPTOPROVIDER_H
#define ICRYPTOPROVIDER_H

// ICryptoProvider - Abstract encryption/decryption interface
// Used by: BaseApiClient for request/response body encryption (when enabled)
// Purpose: Encrypt data before transmission, decrypt received data using server public keys
// When to use: Implement for different crypto algorithms - injected when encryption needed
// Operations: Asymmetric encryption/decryption, key management, algorithm identification

#include <QByteArray>
#include <QString>

enum class CryptoAlgorithm {
    NONE,
    RSA_OAEP,
    AES_256_GCM,
    LIBSODIUM_SEALEDBOX
};

struct EncryptionResult {
    bool success = false;
    QByteArray data;
    QString error;
};

struct DecryptionResult {
    bool success = false;
    QByteArray data;
    QString error;
};

class ICryptoProvider {
public:
    virtual ~ICryptoProvider() = default;
    
    virtual CryptoAlgorithm getAlgorithm() const = 0;
    virtual QString getName() const = 0;
    
    virtual EncryptionResult encrypt(const QByteArray& data, const QByteArray& publicKey) = 0;
    virtual DecryptionResult decrypt(const QByteArray& encryptedData, const QByteArray& privateKey) = 0;
    
    virtual bool isReady() const = 0;
    virtual QString getLastError() const = 0;
};

#endif // ICRYPTOPROVIDER_H