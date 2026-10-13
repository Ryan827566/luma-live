#include "IAccountService.hpp"
#include "contracts/auth/AuthWire.hpp"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <chrono>
#include <condition_variable>
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

Socket Listen(std::uint16_t& port){
    const Socket listener=::socket(AF_INET,SOCK_STREAM,0);
    assert(listener!=kInvalidSocket);
    sockaddr_in address{};
    address.sin_family=AF_INET;
    address.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
    address.sin_port=0;
    assert(::bind(listener,reinterpret_cast<sockaddr*>(&address),sizeof(address))==0);
    assert(::listen(listener,1)==0);
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
void Send(Socket peer,const Packet& packet){
    const auto bytes=luma::contracts::auth::wire::encode(packet);
    std::size_t offset=0;
    while(offset<bytes.size()){
        const auto sent=::send(peer,bytes.data()+offset,static_cast<int>(bytes.size()-offset),0);
        assert(sent>0);offset+=static_cast<std::size_t>(sent);
    }
}
Packet Challenge(Type type){
    if(type==Type::RegisterChallenge)return{type,{std::string(32,'a'),std::string(64,'b'),"600000"}};
    return{type,{std::string(32,'a'),std::string(64,'b'),type==Type::LoginChallenge?"0":std::string(32,'c'),"600000"}};
}

void RejectChallenge(Type operation,const Packet& malformed){
    std::uint16_t port=0;const auto listener=Listen(port);
    std::thread server([&]{
        const auto peer=::accept(listener,nullptr,nullptr);assert(peer!=kInvalidSocket);
        // Seed a session for every case, so rejection must clear a previously logged-in identity.
        assert(Receive(peer).type==Type::LoginBegin);
        auto login=Challenge(Type::LoginChallenge);login.fields.back()="1";Send(peer,login);
        assert(Receive(peer).type==Type::LoginProof);
        const auto future=std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count()+3600;
        Send(peer,{Type::LoginOk,{"fixture-access","fixture-refresh","fixture-session","fixture-device","Fixture",
            "fixture-user","fixture.user","fixture@example.com","Fixture",std::to_string(future),std::to_string(future)}});
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
    // Run all malformed cases in one killable process, keeping the full suite below 20 seconds.
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
}

int main(int argc,char** argv) {
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
