#include "NullCryptoProvider.h"

CryptoAlgorithm NullCryptoProvider::getAlgorithm() const {
    return CryptoAlgorithm::NONE;
}

QString NullCryptoProvider::getName() const {
    return "NullCrypto";
}

EncryptionResult NullCryptoProvider::encrypt(const QByteArray& data, const QByteArray& publicKey) {
    Q_UNUSED(publicKey)
    
    EncryptionResult result;
    result.data = data;  // No encryption - pass through
    result.success = true;
    return result;
}

DecryptionResult NullCryptoProvider::decrypt(const QByteArray& encryptedData, const QByteArray& privateKey) {
    Q_UNUSED(privateKey)
    
    DecryptionResult result;
    result.data = encryptedData;  // No decryption - pass through
    result.success = true;
    return result;
}

bool NullCryptoProvider::isReady() const {
    return true;
}

QString NullCryptoProvider::getLastError() const {
    return m_lastError;
}