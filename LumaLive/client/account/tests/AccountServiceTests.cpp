#include "IAccountService.hpp"
#include "contracts/auth/AuthWire.hpp"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <atomic>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <future>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <winsock2.h>
using Socket=SOCKET;
constexpr Socket kInvalidSocket=INVALID_SOCKET;
void CloseSocket(Socket socket){::closesocket(socket);}
#else
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <signal.h>
#include <unistd.h>
using Socket=int;
constexpr Socket kInvalidSocket=-1;
void CloseSocket(Socket socket){::close(socket);}
#endif

namespace {
using luma::contracts::auth::wire::Packet;
using luma::contracts::auth::wire::Type;

Socket Listen(std::uint16_t& port,bool accepting=true){
    const Socket listener=::socket(AF_INET,SOCK_STREAM,0);
    assert(listener!=kInvalidSocket);
    sockaddr_in address{};
    address.sin_family=AF_INET;
    address.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
    address.sin_port=0;
    assert(::bind(listener,reinterpret_cast<sockaddr*>(&address),sizeof(address))==0);
    if(accepting)assert(::listen(listener,1)==0);
#ifdef _WIN32
    int address_length=sizeof(address);
#else
    socklen_t address_length=sizeof(address);
#endif
    assert(::getsockname(listener,reinterpret_cast<sockaddr*>(&address),&address_length)==0);
    port=ntohs(address.sin_port);
    return listener;
}

Packet Receive(Socket peer){
    std::string line;char c=0;
    while(true){assert(::recv(peer,&c,1,0)==1);if(c=='\n')break;line.push_back(c);assert(line.size()<4096);}
    return luma::contracts::auth::wire::decode_line(line);
}
int SendBytes(Socket peer,const char* bytes,int length){
#ifdef MSG_NOSIGNAL
    return static_cast<int>(::send(peer,bytes,length,MSG_NOSIGNAL));
#else
    return static_cast<int>(::send(peer,bytes,length,0));
#endif
}
void SendRaw(Socket peer,std::string_view bytes){
    std::size_t offset=0;
    while(offset<bytes.size()){
        const auto sent=SendBytes(peer,bytes.data()+offset,static_cast<int>(bytes.size()-offset));
        assert(sent>0);offset+=static_cast<std::size_t>(sent);
    }
}
void Send(Socket peer,const Packet& packet){SendRaw(peer,luma::contracts::auth::wire::encode(packet));}
Packet Challenge(Type type){
    if(type==Type::RegisterChallenge)return{type,{std::string(32,'a'),std::string(64,'b'),"600000"}};
    return{type,{std::string(32,'a'),std::string(64,'b'),type==Type::LoginChallenge?"0":std::string(32,'c'),"600000"}};
}
Packet LoggedIn(){
    const auto future=std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count()+3600;
    return{Type::LoginOk,{"fixture-access","fixture-refresh","fixture-session","fixture-device","Fixture",
        "fixture-user","fixture.user","fixture@example.com","Fixture",std::to_string(future),std::to_string(future)}};
}

void RejectChallenge(Type operation,const Packet& malformed){
    std::uint16_t port=0;const auto listener=Listen(port);
    std::thread server([&]{
        const auto peer=::accept(listener,nullptr,nullptr);assert(peer!=kInvalidSocket);
        // Seed a session for every case, so rejection must clear a previously logged-in identity.
        assert(Receive(peer).type==Type::LoginBegin);
        auto login=Challenge(Type::LoginChallenge);login.fields.back()="1";Send(peer,login);
        assert(Receive(peer).type==Type::LoginProof);
        Send(peer,LoggedIn());
        const auto begin=Receive(peer).type;
        assert(begin==(operation==Type::RegisterChallenge?Type::RegisterBegin:
            operation==Type::LoginChallenge?Type::LoginBegin:Type::ChangePasswordBegin));
        Send(peer,malformed);
        char next=0;
        // Invalid parameters must close the connection without sending a proof or verifier.
        assert(::recv(peer,&next,1,0)==0);
        CloseSocket(peer);
    });
    auto service=luma::client::account::CreateAccountService();
    assert(service->Connect("127.0.0.1",port).success);
    assert(service->Login("fixture.user","fixture password").success);
    assert(service->IsAuthenticated());
    luma::client::account::OperationResult result;
    try{
        if(operation==Type::RegisterChallenge)result=service->Register("new.user","new@example.com","New User","fixture password");
        else if(operation==Type::LoginChallenge)result=service->Login("fixture.user","fixture password");
        else result=service->ChangePassword("fixture password","changed password");
    }catch(...){assert(false&&"malformed challenge escaped the account service");}
    assert(!result.success&&result.code=="invalid_challenge");
    assert(!service->IsRunning()&&!service->IsAuthenticated());
    assert(service->Session().token.empty()&&service->Session().refresh_token.empty()&&service->Session().user.user_id.empty());
    assert(service->Sessions().empty()&&service->SecurityEvents().empty());
    assert(service->Stop().success);
    server.join();CloseSocket(listener);
}

void ChallengeCases(){
    for(const auto type:{Type::RegisterChallenge,Type::LoginChallenge,Type::ChangePasswordChallenge}){
        for(const auto* iterations:{"","abc","0","-1","+1"," 1","1x","600001","4294967295","4294967296","999999999999999999999"}){
            auto packet=Challenge(type);packet.fields.back()=iterations;RejectChallenge(type,packet);
        }
        for(std::size_t field=0;field<(type==Type::ChangePasswordChallenge?3u:2u);++field){
            const auto length=field==1?64u:32u;
            for(const auto& value:std::vector<std::string>{"",std::string(length-2,'a'),std::string(length+2,'a'),
                std::string(length-1,'a'),std::string(length-1,'a')+"z"}){
                auto packet=Challenge(type);packet.fields[field]=value;RejectChallenge(type,packet);
            }
        }
        auto short_packet=Challenge(type);short_packet.fields.pop_back();RejectChallenge(type,short_packet);
        auto wrong_type=Challenge(type);wrong_type.type=Type::Pong;RejectChallenge(type,wrong_type);
    }
    for(const auto* flag:{"","2","false"}){auto packet=Challenge(Type::LoginChallenge);packet.fields[2]=flag;RejectChallenge(Type::LoginChallenge,packet);}
}

void ChallengeWatchdog(){
    // A regressed huge iteration count cannot be interrupted by closing its socket.
    // Bound malformed cases independently of the transport timeout regressions below.
#ifdef _WIN32
    wchar_t executable[32768]{};const auto length=GetModuleFileNameW(nullptr,executable,32768);
    assert(length>0&&length<32768);
    std::wstring command=L"\""+std::wstring(executable)+L"\" --challenge-worker";
    STARTUPINFOW startup{};startup.cb=sizeof(startup);PROCESS_INFORMATION child{};
    assert(CreateProcessW(executable,command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&child));
    CloseHandle(child.hThread);
    const auto waited=WaitForSingleObject(child.hProcess,10000);
    if(waited!=WAIT_OBJECT_0){TerminateProcess(child.hProcess,1);WaitForSingleObject(child.hProcess,1000);}
    DWORD code=1;GetExitCodeProcess(child.hProcess,&code);CloseHandle(child.hProcess);
    assert(waited==WAIT_OBJECT_0&&code==0);
#else
    const auto child=fork();assert(child>=0);
    if(child==0){ChallengeCases();_exit(0);}
    int status=0;bool finished=false;
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);
    while(std::chrono::steady_clock::now()<deadline){
        if(waitpid(child,&status,WNOHANG)==child){finished=true;break;}
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    if(!finished){kill(child,SIGKILL);waitpid(child,&status,0);}
    assert(finished&&WIFEXITED(status)&&WEXITSTATUS(status)==0);
#endif
}

void FragmentedProtocolAndRefusedConnection(){
    std::uint16_t port=0,refused_port=0;
    const auto listener=Listen(port),reserved=Listen(refused_port,false);
    std::thread server([&]{
        const auto peer=::accept(listener,nullptr,nullptr);assert(peer!=kInvalidSocket);
        assert(Receive(peer).type==Type::LoginBegin);
        auto challenge=Challenge(Type::LoginChallenge);challenge.fields.back()="1";
        const auto bytes=luma::contracts::auth::wire::encode(challenge);
        for(std::size_t offset=0;offset<bytes.size();offset+=11){
            SendRaw(peer,std::string_view(bytes).substr(offset,11));
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        assert(Receive(peer).type==Type::LoginProof);
        const Packet profile{Type::ProfileOk,{"fixture-user","fixture.user","fixture@example.com","Loaded profile","",""}};
        // Two frames in one write must remain separate across successive Recv calls.
        SendRaw(peer,luma::contracts::auth::wire::encode(LoggedIn())+luma::contracts::auth::wire::encode(profile));
        assert(Receive(peer).type==Type::GetProfile);
        char c=0;assert(::recv(peer,&c,1,0)==0);CloseSocket(peer);
    });
    auto service=luma::client::account::CreateAccountService();
    assert(service->Connect("127.0.0.1",port).success);
    assert(service->Login("fixture.user","fixture password").success);
    assert(service->IsAuthenticated()&&service->GetProfile().success);
    assert(service->Session().user.display_name=="Loaded profile");
    const auto started=std::chrono::steady_clock::now();
    assert(!service->Connect("127.0.0.1",refused_port).success);
    assert(std::chrono::steady_clock::now()-started<std::chrono::seconds(7));
    assert(!service->IsRunning()&&!service->IsAuthenticated());
    assert(service->Session().token.empty()&&service->Session().user.user_id.empty());
    server.join();CloseSocket(listener);CloseSocket(reserved);
}

void SlowResponseDeadline(){
    std::uint16_t port=0;const auto listener=Listen(port);
    std::mutex mutex;std::condition_variable completed;bool release_peer=false;
    std::atomic<int> sent{0};
    std::thread server([&]{
        const auto peer=::accept(listener,nullptr,nullptr);assert(peer!=kInvalidSocket);
        assert(Receive(peer).type==Type::LoginBegin);
        auto challenge=Challenge(Type::LoginChallenge);challenge.fields.back()="1";Send(peer,challenge);
        assert(Receive(peer).type==Type::LoginProof);Send(peer,LoggedIn());
        assert(Receive(peer).type==Type::GetProfile);
        const auto bytes=luma::contracts::auth::wire::encode({Type::ProfileOk,
            {std::string(80,'a'),"fixture.user","fixture@example.com","Fixture","",""}});
        // Every byte arrives well within the old inactivity timeout. End at 8 seconds
        // even on the old implementation, so the regression fails without hanging.
        for(std::size_t i=0;i<40;++i){
            if(SendBytes(peer,bytes.data()+i,1)!=1)break;
            ++sent;
            std::unique_lock lock(mutex);
            if(completed.wait_for(lock,std::chrono::milliseconds(200),[&]{return release_peer;}))break;
        }
        CloseSocket(peer);
    });
    auto service=luma::client::account::CreateAccountService();
    assert(service->Connect("127.0.0.1",port).success);
    assert(service->Login("fixture.user","fixture password").success);
    const auto started=std::chrono::steady_clock::now();
    const auto result=service->GetProfile();
    const auto elapsed=std::chrono::steady_clock::now()-started;
    {std::lock_guard lock(mutex);release_peer=true;}
    completed.notify_one();server.join();CloseSocket(listener);
    assert(!result.success&&sent>=10);
    assert(elapsed>=std::chrono::milliseconds(4500)&&elapsed<std::chrono::seconds(7));
    assert(!service->IsRunning()&&!service->IsAuthenticated());
    assert(service->Session().token.empty()&&service->Session().refresh_token.empty()&&service->Session().user.user_id.empty());
    const auto stopped=std::chrono::steady_clock::now();
    assert(service->Stop().success);
    assert(std::chrono::steady_clock::now()-stopped<std::chrono::seconds(1));
}
}

int main(int argc,char** argv) {
#ifdef _WIN32
    _putenv_s("LUMALIVE_AUTH_ENV","test");
    _putenv_s("LUMALIVE_AUTH_TLS_CA_FILE","");
#else
    setenv("LUMALIVE_AUTH_ENV","test",1);
    unsetenv("LUMALIVE_AUTH_TLS_CA_FILE");
#endif
    using namespace luma::client::account;
    OperationResult result{false, ""};
    assert(!result.success);
#ifdef _WIN32
    WSADATA sockets{};
    assert(::WSAStartup(MAKEWORD(2,2),&sockets)==0);
#endif
    if(argc==2&&std::string(argv[1])=="--challenge-worker"){
        ChallengeCases();
#ifdef _WIN32
        ::WSACleanup();
#endif
        return 0;
    }
    assert(argc==1);
    ChallengeWatchdog();
    FragmentedProtocolAndRefusedConnection();
    SlowResponseDeadline();
    std::uint16_t port=0;const auto listener=Listen(port);

    std::mutex mutex;
    std::condition_variable completed;
    bool release_peer=false;
    std::promise<void> accepted;
    auto accepted_future=accepted.get_future();
    std::thread stalled_peer([&]{
        const Socket peer=::accept(listener,nullptr,nullptr);
        assert(peer!=kInvalidSocket);
        accepted.set_value();
        // This peer accepts the connection but never replies to the login request.
        // The fallback deadline also lets a regressed client fail without hanging the suite.
        std::unique_lock lock(mutex);
        completed.wait_for(lock,std::chrono::seconds(8),[&]{return release_peer;});
        CloseSocket(peer);
    });

    auto service=CreateAccountService();
    assert(service->Connect("127.0.0.1",port).success);
    accepted_future.wait();
    const auto started=std::chrono::steady_clock::now();
    const auto login=service->Login("timeout.user","correct horse");
    const auto elapsed=std::chrono::steady_clock::now()-started;
    {
        std::lock_guard lock(mutex);
        release_peer=true;
    }
    completed.notify_one();
    stalled_peer.join();
    CloseSocket(listener);

    assert(!login.success);
    assert(elapsed<std::chrono::seconds(7));
    assert(!service->IsRunning());
    assert(!service->IsAuthenticated());
    assert(service->Session().token.empty());
    const auto stop_started=std::chrono::steady_clock::now();
    assert(service->Stop().success);
    assert(std::chrono::steady_clock::now()-stop_started<std::chrono::seconds(1));
#ifdef _WIN32
    ::WSACleanup();
#endif
    return 0;
}
