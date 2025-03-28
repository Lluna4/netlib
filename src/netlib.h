#pragma once
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <print>
#include <thread>
#ifdef __APPLE__
#include <sys/event.h>
#endif
#ifdef __linux__
#include <sys/epoll.h>
#endif
#include <vector>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <cstring>
#include <sys/ioctl.h>
#include <tuple>
#include <mutex>
#include <map>
#include <condition_variable>
#include "comp_time_read.h"
#include "comp_time_write.h"

struct user
{
    user(int sockfd, int size)
    :fd(sockfd), default_size(size)
    {
        readable = false;
        data_size = 0;
        data_alloc_size = 1024;
        data = std::make_shared<char []>(1024);
        erase = false;
    }
    int fd;
    int data_size;
    int data_alloc_size;
    std::shared_ptr<char []> data;
    int default_size;
    bool readable;
    bool erase;
};

template<typename B>
class netlib
{
    public:
        netlib()
        {
            fd = 0;
            epfd = 0;
            threads = true;
            default_packet_size = sizeof(B);
        }
        ~netlib()
        {
            threads = false;
            accept_thread.join();
            recv_thread.join();
        }
        void add_to_list(int sockfd);
        void remove_from_list(int fd, int epfd);
        void open_server(std::string address, short port);
        template<typename ...T>
        std::tuple<T...> read_pkt(std::tuple<T...> packet, int timeout, int current_fd);
        std::map<int, user> users;
        std::vector<int> readable;
        int default_packet_size;
        std::mutex sync;
    private:
        void accept_th();
        void recv_th();
        void disconnect_user(int current_fd);
        std::mutex mut;
        std::mutex internal;
        std::condition_variable cond;
        int fd;
        int epfd;
        bool threads;
        std::thread accept_thread;
        std::thread recv_thread;
};
#ifdef __APPLE__
template<typename B>
void netlib<B>::add_to_list(int sockfd)
{
    struct kevent ev;
    EV_SET(&ev, sockfd, EVFILT_READ, EV_ADD, 0, 0, 0);
    kevent(epfd, &ev, 1, NULL, 0, NULL);
}

template<typename B>
void netlib<B>::remove_from_list(int fd, int epfd)
{
    struct kevent ev;
    EV_SET(&ev, fd, EVFILT_READ, EV_DELETE, 0, 0, 0);
    kevent(epfd, &ev, 1, NULL, 0, NULL);
}
#endif

#ifdef __linux__
template<typename B>
void netlib<B>::add_to_list(int sockfd)
{
    epoll_event event;
    event.data.fd = fd;
    event.events = EPOLLIN;
    epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &event);
}

template<typename B>
void netlib<B>::remove_from_list(int fd, int epfd)
{
	epoll_ctl(epfd, EPOLL_CTL_DEL, fd, nullptr);
}
#endif

template<typename B>
void netlib<B>::recv_th()
{
    int events_ready = 0;
    struct kevent events[1024];
    int status = 0;
    struct timespec timeout;
    timeout.tv_sec = 0;
    timeout.tv_nsec = 500000000; //500ms
    while (threads == true)
    {
        #ifdef __APPLE__
        events_ready = kevent(epfd, NULL, 0, events, 1024, &timeout);
        #endif
        #ifdef __linux__
        events_ready = epoll_wait(epfd, events, 1024, -1);
        #endif
        if (events_ready == -1)
            std::println("Epoll error! {}", strerror(errno));
        for (int i = 0; i < events_ready; i++)
        {
            std::unique_lock<std::mutex> lk(mut);
            #ifdef __APPLE__
            int current_fd = events[i].ident;
            #endif
            #ifdef __linux__
            int current_fd = events[i].data.fd;
            #endif
            auto current_user_test = users.find(current_fd);
            if (current_user_test == users.end())
                continue;
            auto &current_user = current_user_test->second;
            size_t available;
            ioctl(current_fd, FIONREAD, &available);
            if (available > current_user.data_alloc_size - current_user.data_size)
            {
                std::shared_ptr<char[]> newBuffer(new char[current_user.data_size + available + 1]);
                std::memcpy(newBuffer.get(), current_user.data.get(), current_user.data_size);
                current_user.data.reset();
                current_user.data = newBuffer;
                current_user.data_alloc_size = current_user.data_size + available + 1;
            }
            status = recv(current_fd, &(current_user.data.get())[current_user.data_size], available, 0);
            if (status == -1 || status == 0)
            {
                disconnect_user(current_fd);
                std::println("Disconnected with status {}", status);
                if (status == -1)
                    std::println("{}", strerror(errno));
                lk.unlock();
                cond.notify_all();
                continue;
            }
            std::tuple<B> pkt_size;
            char_size buff = {.data = current_user.data.get(), .consumed_size = 0, .max_size = (int)current_user.data_alloc_size, .start_data = current_user.data.get()};
            constexpr std::size_t size = std::tuple_size_v<decltype(pkt_size)>;
            read_comp_pkt(size, buff, pkt_size);
            current_user.data_size += available;
            std::println("New packet size {}", current_user.default_size);
            if (current_user.data_size >= std::get<0>(pkt_size))
            {
                current_user.readable = true;
                std::lock_guard<std::mutex> lock(sync);
                std::lock_guard<std::mutex> lock2(internal);
                readable.push_back(current_fd);
            }
            else
            {
                int data_recv = 0;
                bool user_disconnect = false;
                while (data_recv < std::get<0>(pkt_size) + default_packet_size)
                {
                    int data_left = (std::get<0>(pkt_size) + default_packet_size) - data_recv; 
                    status = recv(current_fd, &(current_user.data.get())[data_recv], data_left, 0);
                    if (status == -1 || status == 0)
                    {
                        disconnect_user(current_fd);
                        std::println("Disconnected with status {}", status);
                        if (status == -1)
                            std::println("{}", strerror(errno));
                        lk.unlock();
                        cond.notify_all();
                        user_disconnect = true;
                        break;
                    }
                    data_recv += status;
                }
                if (user_disconnect == true)
                    continue;
            }
            lk.unlock();
            cond.notify_all();
            std::println("Got {}B of data", available);
        }
    }
}

template<typename B>
void netlib<B>::accept_th()
{
    sockaddr_in addr = {0};
    unsigned int addr_size = sizeof(addr);
    char str[INET_ADDRSTRLEN];
    std::println("Listening for clients");
    while (threads == true)
    {
        int new_client = accept(fd, (sockaddr *)&addr, &addr_size);
        if (new_client < 0)
        {
            //std::println("Returned {}", strerror(errno));
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }
        std::println("Client accepted");
        netlib::add_to_list(new_client);
        struct in_addr ipAddr = addr.sin_addr;
        std::println("{} connected", inet_ntop(AF_INET, &ipAddr, str, INET_ADDRSTRLEN));
        std::println("New fd {}", new_client);
        users.emplace(std::piecewise_construct, std::forward_as_tuple(new_client), std::forward_as_tuple(new_client, default_packet_size));
    }
}

template<typename B>
void netlib<B>::disconnect_user(int current_fd)
{
    remove_from_list(current_fd, epfd);
    std::println("Removed fd {} from epoll", current_fd);
    close(current_fd);
    users.erase(current_fd);
}

template<typename B>
void netlib<B>::open_server(std::string address, short port)
{
    fd = socket(AF_INET, SOCK_STREAM, 0);
    fcntl(fd, F_SETFL, O_NONBLOCK);
    sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    
    if (inet_pton(AF_INET, address.c_str(), &(addr.sin_addr)) == -1)
    {
        std::println("Inet pton failed! {}", strerror(errno));
        close(fd);
        return ;
    }
    if (bind(fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) == -1)
    {
        std::println("Bind failed! {}", strerror(errno));
        close(fd);
        return ;
    }
    if (listen(fd, 10) == -1)
    {
        std::println("Listen failed!");
        close(fd);
        return ;
    }
    epfd = kqueue();
    accept_thread = std::thread([this]() { this->accept_th(); });
    recv_thread = std::thread([this]() { this->recv_th(); });
}

template<typename B>
template <typename... T>
inline std::tuple<T...> netlib<B>::read_pkt(std::tuple<T...> packet, int timeout, int current_fd)
{
    constexpr std::size_t size = std::tuple_size_v<decltype(packet)>;
    int size2 = 0;
    const_for_<size>([&](auto i){size2 += sizeof(std::get<i.value>(packet));});
    auto current_user_test = users.find(current_fd);
    if (current_user_test == users.end() || current_user_test->second.fd < 2)
        return packet;
    auto &current_user = current_user_test->second;
    if (size2 > current_user.data_size)
    {
        std::unique_lock<std::mutex> lk(mut);
        if (timeout >= 0)
        {
            if (cond.wait_for(lk, std::chrono::seconds(timeout)) == std::cv_status::timeout)
                return packet;
        }
        else
        {
            cond.wait(lk);
            if (size2 > current_user.data_size)
                return packet;
        }
    }
    struct packet pkt;
    pkt.buf_size = size2;
    pkt.size = size2;
    pkt.data = current_user.data.get();
    pkt.start_data = current_user.data.get();
    pkt.sock = 0;
    pkt.id = 0;
    char_size buff = {.data = pkt.data, .consumed_size = 0, .max_size = (int)pkt.size, .start_data = pkt.data};
    read_comp_pkt(size, buff, packet);
    std::shared_ptr<char[]> newBuffer(new char[current_user.data_alloc_size]);
    std::memcpy(newBuffer.get(), &(current_user.data.get())[size2], current_user.data_size - size2);
    current_user.data.reset();
    current_user.data = newBuffer;
    current_user.data_size = current_user.data_size - size2;
    std::lock_guard<std::mutex> lock(internal);
    if (current_user.data_size <= 0)
    {
        current_user.readable = false;
        readable.erase(std::remove(readable.begin(), readable.end(), current_fd), readable.end());
    }
    std::println("data size is {}", current_user.data_size);
    return packet;
}
