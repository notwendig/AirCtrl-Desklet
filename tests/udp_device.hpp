#pragma once
#include <aioairctrl/encryption.hpp>
#include "coap.hpp"
#include <atomic>
#include <thread>
#include <chrono>
#include <arpa/inet.h>
#include <poll.h>
#include <unistd.h>

// Loopback-only Philips model for the real CLI + GUI controller integration.
class UdpDevice {
public:
    UdpDevice() {
        fd_=::socket(AF_INET,SOCK_DGRAM|SOCK_CLOEXEC,0);
        if(fd_<0) throw std::runtime_error("test socket failed");
        sockaddr_in address{}; address.sin_family=AF_INET; address.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
        socklen_t size=sizeof(address);
        if(::bind(fd_,reinterpret_cast<sockaddr*>(&address),size)<0 ||
           ::getsockname(fd_,reinterpret_cast<sockaddr*>(&address),&size)<0) {
            ::close(fd_); throw std::runtime_error("test bind failed");
        }
        port=ntohs(address.sin_port); cipher_.set_client_key("11223344");
        thread_=std::thread([this] { run(); });
    }
    ~UdpDevice() { stopped_=true; thread_.join(); ::close(fd_); }
    unsigned short port=0;
    std::atomic<int> syncs{0}, subscriptions{0}, controls{0}, cancellations{0};
    std::atomic<bool> failed{false};
private:
    void send(const aioairctrl::detail::Message& message,const sockaddr_in& peer) {
        const auto wire=aioairctrl::detail::encode(message);
        if(::sendto(fd_,wire.data(),wire.size(),0,reinterpret_cast<const sockaddr*>(&peer),sizeof(peer))<0)
            throw std::runtime_error("test send failed");
    }
    void run() {
        using namespace aioairctrl;
        using namespace std::chrono;
        try {
            sockaddr_in subscriber{}; detail::Bytes token; unsigned sequence=0; std::string power="1";
            auto next=steady_clock::now()+milliseconds(150);
            while(!stopped_) {
                pollfd descriptor{fd_,POLLIN,0};
                if(::poll(&descriptor,1,10)>0) {
                    detail::Bytes bytes(65536); sockaddr_in peer{}; socklen_t size=sizeof(peer);
                    const auto count=::recvfrom(fd_,bytes.data(),bytes.size(),0,reinterpret_cast<sockaddr*>(&peer),&size);
                    if(count<0) throw std::runtime_error("test receive failed");
                    bytes.resize(count); const auto request=detail::decode(bytes);
                    if(!request.code) continue;
                    std::string path;
                    for(const auto& option:request.options) if(option.number==11)
                        path+="/"+std::string(option.value.begin(),option.value.end());
                    if(path=="/sys/dev/sync") {
                        ++syncs; send({1,69,request.mid,request.token,{},"11223344"},peer);
                    } else if(path=="/sys/dev/status") {
                        if(request.observe()==0) { ++subscriptions; subscriber=peer; token=request.token; }
                        else if(request.observe()==1) {
                            ++cancellations;
                            if(peer.sin_port==subscriber.sin_port && token==request.token) token.clear();
                        }
                    } else if(path=="/sys/dev/control") {
                        ++controls;
                        const auto desired=Json::parse(cipher_.decrypt(request.payload)).at("state").at("desired");
                        power=desired.at("pwr").get<std::string>();
                        send({1,68,request.mid,request.token,{},R"({"status":"success"})"},peer);
                    } else throw std::runtime_error("unexpected test request");
                }
                if(!token.empty() && steady_clock::now()>=next) {
                    detail::Message response{1,69,77,token,{{6,{static_cast<unsigned char>(sequence++%256)}}},{}};
                    response.payload=cipher_.encrypt(Json{{"state",{{"reported",{{"pwr",power},{"rh",50}}}}}}.dump());
                    send(response,subscriber); next=steady_clock::now()+milliseconds(150);
                }
            }
        } catch(...) { failed=true; }
    }
    int fd_=-1;
    std::atomic<bool> stopped_{false};
    std::thread thread_;
    aioairctrl::EncryptionContext cipher_;
};
