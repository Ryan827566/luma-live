#include "IAuthStore.hpp"
#include <cstdlib>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>

#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace luma::server::auth {

namespace {

std::string EnvironmentValue(const char* name) {
#ifdef _WIN32
    char* value=nullptr;
    std::size_t length=0;
    if(_dupenv_s(&value,&length,name)!=0||!value)return {};
    std::string result(value);
    std::free(value);
    return result;
#else
    const char* value=std::getenv(name);
    return value?std::string(value):std::string{};
#endif
}

class InMemoryAuthStore final : public IAuthStore {
public:
    shared::contracts::Result Open() override {
        std::lock_guard lock(mutex_);
        opened_=true;
        return shared::contracts::Result::Ok();
    }

    void Close() noexcept override {
        std::lock_guard lock(mutex_);
        opened_=false;
    }

    shared::contracts::Result LoadUsers(
        std::vector<AuthUserRecord>& users) override {
        std::lock_guard lock(mutex_);
        if(!opened_) return shared::contracts::Result::Failure(
            shared::contracts::ErrorCode::InvalidState,"auth store is not open");
        users=users_;
        return shared::contracts::Result::Ok();
    }

    shared::contracts::Result ReplaceUsers(
        const std::vector<AuthUserRecord>& users) override {
        std::lock_guard lock(mutex_);
        if(!opened_) return shared::contracts::Result::Failure(
            shared::contracts::ErrorCode::InvalidState,"auth store is not open");
        users_=users;
        return shared::contracts::Result::Ok();
    }

    shared::contracts::Result LoadSecurityEvents(
        std::vector<AuthSecurityEventRecord>& events) override {
        std::lock_guard lock(mutex_);
        if(!opened_) return shared::contracts::Result::Failure(
            shared::contracts::ErrorCode::InvalidState,"auth store is not open");
        events=events_;
        return shared::contracts::Result::Ok();
    }

    shared::contracts::Result AppendSecurityEvent(
        const AuthSecurityEventRecord& event) override {
        std::lock_guard lock(mutex_);
        if(!opened_) return shared::contracts::Result::Failure(
            shared::contracts::ErrorCode::InvalidState,"auth store is not open");
        events_.push_back(event);
        if(events_.size()>512)events_.erase(events_.begin());
        return shared::contracts::Result::Ok();
    }

private:
    bool opened_{false};
    std::mutex mutex_;
    std::vector<AuthUserRecord> users_;
    std::vector<AuthSecurityEventRecord> events_;
};

struct pg_conn;
struct pg_result;
using PGconn=pg_conn;
using PGresult=pg_result;
using Oid=unsigned int;

constexpr int CONNECTION_OK=0;
constexpr int PGRES_COMMAND_OK=1;
constexpr int PGRES_TUPLES_OK=2;

struct PostgresApi {
#ifdef _WIN32
    HMODULE library{nullptr};
#else
    void* library{nullptr};
#endif

    using PQconnectdbFn=PGconn*(*)(const char*);
    using PQstatusFn=int(*)(const PGconn*);
    using PQerrorMessageFn=const char*(*)(const PGconn*);
    using PQfinishFn=void(*)(PGconn*);
    using PQexecFn=PGresult*(*)(PGconn*,const char*);
    using PQexecParamsFn=PGresult*(*)(PGconn*,const char*,int,const Oid*,const char*const*,const int*,const int*,int);
    using PQresultStatusFn=int(*)(const PGresult*);
    using PQresultErrorMessageFn=const char*(*)(const PGresult*);
    using PQntuplesFn=int(*)(const PGresult*);
    using PQnfieldsFn=int(*)(const PGresult*);
    using PQgetvalueFn=char*(*)(const PGresult*,int,int);
    using PQclearFn=void(*)(PGresult*);

    PQconnectdbFn PQconnectdb{nullptr};
    PQstatusFn PQstatus{nullptr};
    PQerrorMessageFn PQerrorMessage{nullptr};
    PQfinishFn PQfinish{nullptr};
    PQexecFn PQexec{nullptr};
    PQexecParamsFn PQexecParams{nullptr};
    PQresultStatusFn PQresultStatus{nullptr};
    PQresultErrorMessageFn PQresultErrorMessage{nullptr};
    PQntuplesFn PQntuples{nullptr};
    PQnfieldsFn PQnfields{nullptr};
    PQgetvalueFn PQgetvalue{nullptr};
    PQclearFn PQclear{nullptr};

    ~PostgresApi() {
#ifdef _WIN32
        if(library) FreeLibrary(library);
#else
        if(library) dlclose(library);
#endif
    }

    template<typename T>
    bool LoadSymbol(T& target,const char* name) {
#ifdef _WIN32
        target=reinterpret_cast<T>(GetProcAddress(library,name));
#else
        target=reinterpret_cast<T>(dlsym(library,name));
#endif
        return target!=nullptr;
    }

    shared::contracts::Result LoadClientLibrary() {
#ifdef _WIN32
        library=LoadLibraryA("libpq.dll");
        if(!library) library=LoadLibraryA("libpq5.dll");
#else
        library=dlopen("libpq.so.5",RTLD_NOW|RTLD_LOCAL);
        if(!library) library=dlopen("libpq.so",RTLD_NOW|RTLD_LOCAL);
#endif
        if(!library)return shared::contracts::Result::Failure(
            shared::contracts::ErrorCode::Internal,"PostgreSQL client library (libpq) was not found");

        if(!LoadSymbol(PQconnectdb,"PQconnectdb")||
           !LoadSymbol(PQstatus,"PQstatus")||
           !LoadSymbol(PQerrorMessage,"PQerrorMessage")||
           !LoadSymbol(PQfinish,"PQfinish")||
           !LoadSymbol(PQexec,"PQexec")||
           !LoadSymbol(PQexecParams,"PQexecParams")||
           !LoadSymbol(PQresultStatus,"PQresultStatus")||
           !LoadSymbol(PQresultErrorMessage,"PQresultErrorMessage")||
           !LoadSymbol(PQntuples,"PQntuples")||
           !LoadSymbol(PQnfields,"PQnfields")||
           !LoadSymbol(PQgetvalue,"PQgetvalue")||
           !LoadSymbol(PQclear,"PQclear")) {
            return shared::contracts::Result::Failure(
                shared::contracts::ErrorCode::Internal,"PostgreSQL client library is missing required symbols");
        }
        return shared::contracts::Result::Ok();
    }
};

class PostgresAuthStore final : public IAuthStore {
public:
    explicit PostgresAuthStore(std::string connection_string)
        : connection_string_(std::move(connection_string)) {}

    ~PostgresAuthStore() override { Close(); }

    shared::contracts::Result Open() override {
        std::lock_guard lock(mutex_);
        if(opened_) return shared::contracts::Result::Ok();
        if(connection_string_.empty()) return shared::contracts::Result::Failure(
            shared::contracts::ErrorCode::InvalidArgument,
            "PostgreSQL connection string is empty");

        auto library=api_.LoadClientLibrary();
        if(!library.IsOk()) return library;

        connection_=api_.PQconnectdb(connection_string_.c_str());
        if(!connection_) return shared::contracts::Result::Failure(
            shared::contracts::ErrorCode::Internal,"PostgreSQL connection creation failed");

        if(api_.PQstatus(connection_)!=CONNECTION_OK) {
            const std::string message=api_.PQerrorMessage(connection_);
            api_.PQfinish(connection_);
            connection_=nullptr;
            return shared::contracts::Result::Failure(
                shared::contracts::ErrorCode::Internal,"PostgreSQL connection failed: "+message);
        }

        auto schema=EnsureSchemaLocked();
        if(!schema.IsOk()) {
            api_.PQfinish(connection_);
            connection_=nullptr;
            return schema;
        }

        opened_=true;
        return shared::contracts::Result::Ok();
    }

    void Close() noexcept override {
        std::lock_guard lock(mutex_);
        if(connection_ && api_.PQfinish)api_.PQfinish(connection_);
        connection_=nullptr;
        opened_=false;
    }

    shared::contracts::Result LoadUsers(
        std::vector<AuthUserRecord>& users) override {
        std::lock_guard lock(mutex_);
        if(auto r=CheckOpenLocked();!r.IsOk())return r;

        static constexpr const char* sql=
            "SELECT user_id,username,email,display_name,avatar_url,phone,"
            "password_salt,password_verifier,password_kdf_iterations,email_verified,phone_verified,mfa_enabled,"
            "mfa_recovery_hash,mfa_totp_secret_hex,email_verify_hash,email_verify_expires,"
            "reset_token_hash,reset_token_expires "
            "FROM luma_auth_users ORDER BY user_id";

        PGresult* result=api_.PQexec(connection_,sql);
        if(!result)return FailureLocked("PostgreSQL query failed");
        ResultGuard guard{api_,result};
        if(api_.PQresultStatus(result)!=PGRES_TUPLES_OK)return ResultErrorLocked(result);

        if(api_.PQnfields(result)!=18)return shared::contracts::Result::Failure(
            shared::contracts::ErrorCode::Internal,"unexpected auth user schema");

        users.clear();
        const int rows=api_.PQntuples(result);
        users.reserve(static_cast<std::size_t>(rows));
        for(int row=0;row<rows;++row) {
            AuthUserRecord u{};
            u.id=Value(result,row,0);
            u.username=Value(result,row,1);
            u.email=Value(result,row,2);
            u.display_name=Value(result,row,3);
            u.avatar_url=Value(result,row,4);
            u.phone=Value(result,row,5);
            u.salt_hex=Value(result,row,6);
            u.verifier_hex=Value(result,row,7);
            u.password_kdf_iterations=static_cast<std::uint32_t>(Int64Value(result,row,8));
            u.email_verified=BoolValue(result,row,9);
            u.phone_verified=BoolValue(result,row,10);
            u.mfa_enabled=BoolValue(result,row,11);
            u.mfa_recovery_hash=Value(result,row,12);
            u.mfa_totp_secret_hex=Value(result,row,13);
            u.email_verify_hash=Value(result,row,14);
            u.email_verify_expires=Int64Value(result,row,15);
            u.reset_token_hash=Value(result,row,16);
            u.reset_token_expires=Int64Value(result,row,17);
            users.push_back(std::move(u));
        }
        return shared::contracts::Result::Ok();
    }

    shared::contracts::Result ReplaceUsers(
        const std::vector<AuthUserRecord>& users) override {
        std::lock_guard lock(mutex_);
        if(auto r=CheckOpenLocked();!r.IsOk())return r;

        if(auto r=ExecSimpleLocked("BEGIN");!r.IsOk())return r;

        if(auto r=ExecSimpleLocked("DELETE FROM luma_auth_users");!r.IsOk()) {
            ExecSimpleLocked("ROLLBACK");
            return r;
        }

        static constexpr const char* sql=
            "INSERT INTO luma_auth_users("
            "user_id,username,email,display_name,avatar_url,phone,password_salt,password_verifier,password_kdf_iterations,"
            "email_verified,phone_verified,mfa_enabled,mfa_recovery_hash,mfa_totp_secret_hex,email_verify_hash,"
            "email_verify_expires,reset_token_hash,reset_token_expires)"
            " VALUES($1,$2,$3,$4,$5,$6,$7,$8,$9,$10,$11,$12,$13,$14,$15,$16,$17,$18)";

        for(const auto& u:users) {
            const std::string password_kdf_iterations=std::to_string(u.password_kdf_iterations);
            const std::string email_verified=u.email_verified?"true":"false";
            const std::string phone_verified=u.phone_verified?"true":"false";
            const std::string mfa_enabled=u.mfa_enabled?"true":"false";
            const std::string email_expiry=std::to_string(u.email_verify_expires);
            const std::string reset_expiry=std::to_string(u.reset_token_expires);
            const std::string* params[]={
                &u.id,&u.username,&u.email,&u.display_name,&u.avatar_url,&u.phone,
                &u.salt_hex,&u.verifier_hex,&password_kdf_iterations,&email_verified,&phone_verified,&mfa_enabled,
                &u.mfa_recovery_hash,&u.mfa_totp_secret_hex,&u.email_verify_hash,&email_expiry,
                &u.reset_token_hash,&reset_expiry};

            if(auto r=ExecParamsLocked(sql,params,18);!r.IsOk()) {
                ExecSimpleLocked("ROLLBACK");
                return r;
            }
        }

        if(auto r=ExecSimpleLocked("COMMIT");!r.IsOk()) {
            ExecSimpleLocked("ROLLBACK");
            return r;
        }
        return shared::contracts::Result::Ok();
    }

    shared::contracts::Result LoadSecurityEvents(
        std::vector<AuthSecurityEventRecord>& events) override {
        std::lock_guard lock(mutex_);
        if(auto r=CheckOpenLocked();!r.IsOk())return r;

        static constexpr const char* sql=
            "SELECT event_id,user_id,type,detail,created_at "
            "FROM luma_auth_security_events ORDER BY created_at DESC LIMIT 512";

        PGresult* result=api_.PQexec(connection_,sql);
        if(!result)return FailureLocked("PostgreSQL query failed");
        ResultGuard guard{api_,result};
        if(api_.PQresultStatus(result)!=PGRES_TUPLES_OK)return ResultErrorLocked(result);
        if(api_.PQnfields(result)!=5)return shared::contracts::Result::Failure(
            shared::contracts::ErrorCode::Internal,"unexpected security event schema");

        events.clear();
        const int rows=api_.PQntuples(result);
        events.reserve(static_cast<std::size_t>(rows));
        for(int row=0;row<rows;++row) {
            AuthSecurityEventRecord e{};
            e.event_id=Value(result,row,0);
            e.user_id=Value(result,row,1);
            e.type=Value(result,row,2);
            e.detail=Value(result,row,3);
            e.created_at_epoch_seconds=Int64Value(result,row,4);
            events.push_back(std::move(e));
        }
        return shared::contracts::Result::Ok();
    }

    shared::contracts::Result AppendSecurityEvent(
        const AuthSecurityEventRecord& event) override {
        std::lock_guard lock(mutex_);
        if(auto r=CheckOpenLocked();!r.IsOk())return r;

        static constexpr const char* sql=
            "INSERT INTO luma_auth_security_events(event_id,user_id,type,detail,created_at)"
            " VALUES($1,$2,$3,$4,$5)";

        const std::string timestamp=std::to_string(event.created_at_epoch_seconds);
        const std::string* params[]={
            &event.event_id,&event.user_id,&event.type,&event.detail,&timestamp};
        return ExecParamsLocked(sql,params,5);
    }

private:
    struct ResultGuard {
        PostgresApi& api;
        PGresult* result;
        ~ResultGuard(){ if(result)api.PQclear(result); }
    };

    std::string Value(PGresult* result,int row,int column) const {
        char* value=api_.PQgetvalue(result,row,column);
        return value?std::string(value):std::string{};
    }

    bool BoolValue(PGresult* result,int row,int column) const {
        const auto value=Value(result,row,column);
        return value=="t"||value=="true"||value=="1";
    }

    std::int64_t Int64Value(PGresult* result,int row,int column) const {
        const auto value=Value(result,row,column);
        try{return std::stoll(value);}catch(...){return 0;}
    }

    shared::contracts::Result CheckOpenLocked() const {
        if(!opened_||!connection_)return shared::contracts::Result::Failure(
            shared::contracts::ErrorCode::InvalidState,"auth database is not open");
        return shared::contracts::Result::Ok();
    }

    shared::contracts::Result FailureLocked(const std::string& fallback) const {
        std::string message=fallback;
        if(connection_&&api_.PQerrorMessage) {
            message=api_.PQerrorMessage(connection_);
        }
        return shared::contracts::Result::Failure(shared::contracts::ErrorCode::Internal,message);
    }

    shared::contracts::Result ResultErrorLocked(PGresult* result) const {
        std::string message="PostgreSQL query failed";
        if(result&&api_.PQresultErrorMessage) {
            const char* raw=api_.PQresultErrorMessage(result);
            if(raw&&*raw)message=raw;
        }
        return shared::contracts::Result::Failure(shared::contracts::ErrorCode::Internal,message);
    }

    shared::contracts::Result ExecSimpleLocked(const char* sql) {
        PGresult* result=api_.PQexec(connection_,sql);
        if(!result)return FailureLocked("PostgreSQL command failed");
        ResultGuard guard{api_,result};
        const int status=api_.PQresultStatus(result);
        if(status!=PGRES_COMMAND_OK)return ResultErrorLocked(result);
        return shared::contracts::Result::Ok();
    }

    shared::contracts::Result ExecParamsLocked(
        const char* sql,const std::string* const* params,int count) {
        std::vector<const char*> values;
        values.reserve(static_cast<std::size_t>(count));
        for(int i=0;i<count;++i)values.push_back(params[i]->c_str());

        PGresult* result=api_.PQexecParams(
            connection_,sql,count,nullptr,values.data(),nullptr,nullptr,0);
        if(!result)return FailureLocked("PostgreSQL parameterized command failed");
        ResultGuard guard{api_,result};
        if(api_.PQresultStatus(result)!=PGRES_COMMAND_OK)return ResultErrorLocked(result);
        return shared::contracts::Result::Ok();
    }

    shared::contracts::Result EnsureSchemaLocked() {
        static constexpr const char* schema=
            "CREATE TABLE IF NOT EXISTS luma_auth_users("
            "user_id TEXT PRIMARY KEY,"
            "username TEXT NOT NULL,"
            "email TEXT NOT NULL,"
            "display_name TEXT NOT NULL,"
            "avatar_url TEXT NOT NULL DEFAULT '',"
            "phone TEXT NOT NULL DEFAULT '',"
            "password_salt TEXT NOT NULL,"
            "password_verifier TEXT NOT NULL,"
            "password_kdf_iterations INTEGER NOT NULL DEFAULT 120000,"
            "email_verified BOOLEAN NOT NULL DEFAULT FALSE,"
            "phone_verified BOOLEAN NOT NULL DEFAULT FALSE,"
            "mfa_enabled BOOLEAN NOT NULL DEFAULT FALSE,"
            "mfa_recovery_hash TEXT NOT NULL DEFAULT '',"
            "mfa_totp_secret_hex TEXT NOT NULL DEFAULT '',"
            "email_verify_hash TEXT NOT NULL DEFAULT '',"
            "email_verify_expires BIGINT NOT NULL DEFAULT 0,"
            "reset_token_hash TEXT NOT NULL DEFAULT '',"
            "reset_token_expires BIGINT NOT NULL DEFAULT 0"
            ");"
            "ALTER TABLE luma_auth_users ADD COLUMN IF NOT EXISTS password_kdf_iterations INTEGER NOT NULL DEFAULT 120000;"
            "ALTER TABLE luma_auth_users ADD COLUMN IF NOT EXISTS mfa_totp_secret_hex TEXT NOT NULL DEFAULT '';"
            "CREATE UNIQUE INDEX IF NOT EXISTS luma_auth_users_username_idx "
            "ON luma_auth_users(lower(username));"
            "CREATE UNIQUE INDEX IF NOT EXISTS luma_auth_users_email_idx "
            "ON luma_auth_users(lower(email));"
            "CREATE UNIQUE INDEX IF NOT EXISTS luma_auth_users_phone_idx "
            "ON luma_auth_users(phone) WHERE phone <> '';"
            "CREATE TABLE IF NOT EXISTS luma_auth_security_events("
            "event_id TEXT PRIMARY KEY,"
            "user_id TEXT NOT NULL,"
            "type TEXT NOT NULL,"
            "detail TEXT NOT NULL,"
            "created_at BIGINT NOT NULL"
            ");"
            "CREATE INDEX IF NOT EXISTS luma_auth_security_events_user_idx "
            "ON luma_auth_security_events(user_id,created_at DESC);";

        PGresult* result=api_.PQexec(connection_,schema);
        if(!result)return FailureLocked("PostgreSQL schema initialization failed");
        ResultGuard guard{api_,result};
        if(api_.PQresultStatus(result)!=PGRES_COMMAND_OK)
            return ResultErrorLocked(result);
        return shared::contracts::Result::Ok();
    }

    std::string connection_string_;
    PostgresApi api_;
    PGconn* connection_{nullptr};
    bool opened_{false};
    std::mutex mutex_;
};

}

std::unique_ptr<IAuthStore> CreateInMemoryAuthStore() {
    return std::make_unique<InMemoryAuthStore>();
}

std::unique_ptr<IAuthStore> CreatePostgresAuthStore(std::string connection_string) {
    return std::make_unique<PostgresAuthStore>(std::move(connection_string));
}

std::unique_ptr<IAuthStore> CreateAuthStoreFromEnvironment() {
    const auto connection_string=EnvironmentValue("LUMALIVE_AUTH_DATABASE_URL");
    if(!connection_string.empty())return CreatePostgresAuthStore(connection_string);
    return CreateInMemoryAuthStore();
}

}
