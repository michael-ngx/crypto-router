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

// User structure
struct User {
    std::string id;
    std::string first_name;
    std::string last_name;
    std::string email;
    std::string password_hash; // Don't return this in API responses
};

// User store interface
class IUserStore {
public:
    virtual ~IUserStore() = default;
    virtual std::string create_user(const std::string& email, const std::string& password, 
                                    const std::string& first_name, const std::string& last_name) = 0;
    virtual std::optional<User> get_user_by_email(const std::string& email) = 0;
    virtual std::optional<User> get_user_by_id(const std::string& id) = 0;
};

// Supabase-backed user store
class SupabaseUserStore : public IUserStore {
public:
    explicit SupabaseUserStore(const std::string& connection_string)
        : conn_str_(connection_string) {}
    
    std::string create_user(const std::string& email, const std::string& password,
                           const std::string& first_name, const std::string& last_name) override {
        pqxx::connection conn(conn_str_);
        pqxx::work txn(conn);
        
        std::string password_hash = hash_password(password);
        
        std::string query = R"(
            INSERT INTO public.users (email, password, first_name, last_name)
            VALUES ($1, $2, $3, $4)
            RETURNING id
        )";
        
        try {
            pqxx::result result = txn.exec(
                query,
                pqxx::params(email, password_hash, first_name, last_name)
            );
            txn.commit();
            return result[0][0].as<std::string>();
        } catch (const pqxx::sql_error& e) {
            if (std::string(e.what()).find("duplicate key") != std::string::npos ||
                std::string(e.what()).find("unique constraint") != std::string::npos) {
                throw std::runtime_error("Email already exists");
            }
            throw std::runtime_error("Failed to create user: " + std::string(e.what()));
        }
    }
    
    std::optional<User> get_user_by_email(const std::string& email) override {
        pqxx::connection conn(conn_str_);
        pqxx::work txn(conn);
        
        std::string query = R"(
            SELECT id, email, password, first_name, last_name
            FROM public.users
            WHERE email = $1
        )";
        
        try {
            pqxx::result result = txn.exec(query, pqxx::params(email));
            if (result.empty()) {
                return std::nullopt;
            }
            
            auto row = result[0];
            User user;
            user.id = row[0].as<std::string>();
            user.email = row[1].as<std::string>();
            user.password_hash = row[2].as<std::string>();
            user.first_name = row[3].as<std::string>();
            user.last_name = row[4].as<std::string>();
            return user;
        } catch (const std::exception& e) {
            return std::nullopt;
        }
    }
    
    std::optional<User> get_user_by_id(const std::string& id) override {
        pqxx::connection conn(conn_str_);
        pqxx::work txn(conn);
        
        std::string query = R"(
            SELECT id, email, password, first_name, last_name
            FROM public.users
            WHERE id = $1
        )";
        
        try {
            pqxx::result result = txn.exec(query, pqxx::params(id));
            if (result.empty()) {
                return std::nullopt;
            }
            
            auto row = result[0];
            User user;
            user.id = row[0].as<std::string>();
            user.email = row[1].as<std::string>();
            user.password_hash = row[2].as<std::string>();
            user.first_name = row[3].as<std::string>();
            user.last_name = row[4].as<std::string>();
            return user;
        } catch (const std::exception& e) {
            return std::nullopt;
        }
    }
    
private:
    std::string conn_str_;
};

inline std::unique_ptr<IUserStore> make_user_store(const std::string& connection_string) {
    return std::make_unique<SupabaseUserStore>(connection_string);
}

