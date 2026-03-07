#pragma once
#include <string>
#include <pqxx/pqxx>
#include <openssl/evp.h>
#include <iomanip>
#include <sstream>
#include <optional>
#include <memory>

// Simple password hashing using SHA-256 via EVP API (for production, use bcrypt or Argon2)
inline std::string hash_password(const std::string& password) {
    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    if (!ctx) {
        throw std::runtime_error("Failed to create EVP context");
    }
    
    const EVP_MD* md = EVP_sha256();
    unsigned char hash[EVP_MAX_MD_SIZE];
    unsigned int hash_len;
    
    if (EVP_DigestInit_ex(ctx, md, nullptr) != 1 ||
        EVP_DigestUpdate(ctx, password.c_str(), password.length()) != 1 ||
        EVP_DigestFinal_ex(ctx, hash, &hash_len) != 1) {
        EVP_MD_CTX_free(ctx);
        throw std::runtime_error("Failed to compute hash");
    }
    
    EVP_MD_CTX_free(ctx);
    
    std::stringstream ss;
    for (unsigned int i = 0; i < hash_len; i++) {
        ss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(hash[i]);
    }
    return ss.str();
}

// Verify password against hash
inline bool verify_password(const std::string& password, const std::string& hash) {
    return hash_password(password) == hash;
}

