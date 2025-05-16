#pragma once
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <print>
#include <thread>
#if defined(__APPLE__) || defined(__FreeBSD__)
#include <sys/event.h>
#elif defined(__linux__)
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

#define MAX_PACKET_SIZE 8192

template <typename T>
struct packet_raw
{
    T size;
    char *data;
};

template <typename T>
struct user
{
    user(int sockfd)
    :fd(sockfd)
    {}
    int fd;
    std::vector<packet_raw<T>> packets;
};

struct user_raw
{
    user_raw(int sockfd)
    :fd(sockfd)
    {
        data = (char *)calloc(1024, sizeof(char));
        data_size = 0;
        alloc_size = 1024;
        target = false;
        target_permanent = false;
        target_size = 0;
    }
    int fd;
    char *data;
    size_t data_size;
    size_t alloc_size;
    void set_target(size_t target_s, bool permanent = false);
    void add_data(char *new_data, size_t size);
    void remove_data(size_t size);
    char *receive_data(size_t size);
    std::atomic_bool readable;
    std::mutex sync;
    bool target;
    bool target_permanent;
    size_t target_size;
};

namespace netlib
{
    template<typename T>
    class server
    {
        public:
            server()
            {
                fd = 0;
                epfd = 0;
                threads = true;
            }
            ~server()
            {
                threads = false;
                recv_thread.join();
            }
            int fd;
            void open_server(std::string address, short port);
            void disconnect_user(int current_fd);
            std::map<int, std::vector<packet_raw<T>>> check_packets();
            std::vector<int> readable;
            std::map<int, user<T>> users;
            std::mutex sync;
        private:
            void add_to_list(int sockfd);
            void remove_from_list(int fd);
            void recv_th();
            int epfd;
            bool threads;
            std::thread recv_thread;
    };

    class server_raw
    {
        public:
            server_raw()
            {
                fd = 0;
                epfd = 0;
                threads = true;
                server_target_size = 0;
            }
            server_raw(bool server_target, int target_size)
            {
                fd = 0;
                epfd = 0;
                threads = true;
                if (server_target)
                    server_target_size = target_size;
                else
                    server_target_size = 0;
            }
            ~server_raw()
            {
                threads = false;
                recv_thread.join();
            }
            int fd;
            void open_server(std::string address, short port);
            void disconnect_user(int current_fd);
            char *receive_data(int current_fd, size_t size);
            char *receive_data_ensured(int current_fd, size_t size);
            char *get_line(int current_fd);
            std::pair<char *, size_t> receive_everything(int current_fd);
            template<typename ...T>
            std::tuple<T...> read_packet(int current_fd, std::tuple<T...> packet);
            std::vector<int> get_readable();
            std::vector<int> wait_readable();

            void wait_readable_fd(int fd);

            void set_target(int client_fd, size_t target_s, bool permanent = false);
            std::vector<int> readable;
            std::map<int, user_raw> users;
            std::mutex sync;
        private:
            void add_to_list(int sockfd);
            void remove_from_list(int fd);
            void recv_th();
            int epfd;
            bool threads;
            int server_target_size;
            std::condition_variable readable_cv;
            std::thread recv_thread;
    };
    struct cli_raw
    {
        cli_raw()
        {
            fd = 0;
            data = (char *)calloc(1024, sizeof(char));
            data_size = 0;
            alloc_size = 1024;
        }
        cli_raw(int sockfd)
        :fd(sockfd)
        {
            data = (char *)calloc(1024, sizeof(char));
            data_size = 0;
            alloc_size = 1024;
        }
        int fd;
        char *data;
        size_t data_size;
        size_t alloc_size;
        void add_data(char *new_data, size_t size);
        void remove_data(size_t size);
        char *receive_data(size_t size);
        std::atomic_bool readable;
        std::mutex sync;
    };
    
    class client_raw
    {
        public:
            client_raw()
            {
                fd = 0;
                epfd = 0;
                threads = true;
                readable = false;
            }
            ~client_raw()
            {
                threads = false;
                recv_thread.join();
            }
            int fd;
            void connect_to_server(std::string address, short port);
            void disconnect_from_server();
            char *receive_data(int current_fd, size_t size);
            template<typename ...T>
            std::tuple<T...> read_packet(int current_fd, std::tuple<T...> packet);
            std::atomic_bool readable;
            std::mutex sync;
        private:
            cli_raw serv;
            void recv_th();
            int epfd;
            bool threads;
            std::thread recv_thread;
    };
    
    template <typename... T>
    inline std::tuple<T...> client_raw::read_packet(int current_fd, std::tuple<T...> packet)
    {
        constexpr std::size_t size_tuple = std::tuple_size_v<decltype(packet)>;
        int size = 0;
        const_for_<size_tuple>([&](auto i){size += sizeof(std::get<i.value>(packet));});
        
        std::lock_guard<std::mutex> lock(sync);
        
        auto &current_user = serv;
        
        char *data = current_user.receive_data(size);
        struct packet pkt = {size, size, data, data};
        packet = netlib::read_packet(packet, pkt);
        return packet;
    }
    template <typename... T>
    inline std::tuple<T...> server_raw::read_packet(int current_fd, std::tuple<T...> packet)
    {
        constexpr std::size_t size_tuple = std::tuple_size_v<decltype(packet)>;
        int size = 0;
        const_for_<size_tuple>([&](auto i){size += sizeof(std::get<i.value>(packet));});
        
        std::lock_guard<std::mutex> lock(sync);
        
        auto current_user_test = users.find(current_fd);
        if (current_user_test == users.end())
            return packet;
        auto &current_user = current_user_test->second;
        
        if (size == current_user.data_size)
            readable.erase(std::remove(readable.begin(), readable.end(), current_fd), readable.end());
        char *data = current_user.receive_data(size);
        struct packet pkt = {size, size, data, data};
        packet = netlib::read_packet(packet, pkt);
        return packet;
    }
}
template <typename T>
inline void netlib::server<T>::open_server(std::string address, short port)
{
    fd = socket(AF_INET, SOCK_STREAM, 0);
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
    #if defined(__APPLE__) || defined(__FreeBSD__)
    epfd = kqueue();
    #elif defined(__linux__)
    epfd = epoll_create1(0);
    #endif
    add_to_list(fd);
    recv_thread = std::thread([this]() { this->recv_th(); });
}
template<typename T>
void netlib::server<T>::disconnect_user(int current_fd)
{
    remove_from_list(current_fd);
    std::println("Removed fd {} from epoll", current_fd);
    close(current_fd);
    users.erase(current_fd);
    readable.erase(std::remove(readable.begin(), readable.end(), current_fd), readable.end());
}

#if defined(__APPLE__) || defined(__FreeBSD__)
template <typename T>
void netlib::server<T>::add_to_list(int sockfd)
{
    struct kevent ev;
    EV_SET(&ev, sockfd, EVFILT_READ, EV_ADD, 0, 0, 0);
    kevent(epfd, &ev, 1, NULL, 0, NULL);
}

template<typename T>
void netlib::server<T>::remove_from_list(int fd)
{
    struct kevent ev;
    EV_SET(&ev, fd, EVFILT_READ, EV_DELETE, 0, 0, 0);
    kevent(epfd, &ev, 1, NULL, 0, NULL);
}
#elif defined(__linux__)
template<typename T>
void netlib::server<T>::add_to_list(int sockfd)
{
    epoll_event event;
    event.data.fd = fd;
    event.events = EPOLLIN;
    epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &event);
}

template<typename T>
void netlib::server<T>::remove_from_list(int fd)
{
    epoll_ctl(epfd, EPOLL_CTL_DEL, fd, nullptr);
}
#endif

template <typename T>
inline void netlib::server<T>::recv_th()
{
    int events_ready = 0;
    #if defined(__APPLE__) || defined(__FreeBSD__)
    struct kevent events[1024];
    struct timespec timeout;
    timeout.tv_sec = 0;
    timeout.tv_nsec = 500000000; //500ms
    #elif defined(__linux__)
    epoll_event events[1024];
    #endif
    int status = 0;
    while (threads == true)
    {
        #if defined(__APPLE__) || defined(__FreeBSD__)
        events_ready = kevent(epfd, NULL, 0, events, 1024, &timeout);
        #elif defined(__linux__)
        events_ready = epoll_wait(epfd, events, 1024, 500);
        #endif
        if (events_ready == -1)
        {
            std::println("Epoll/kqueue failed {}", strerror(errno));
            break;
        }
        for (int i = 0; i < events_ready; i++)
        {
            #if defined(__APPLE__) || defined(__FreeBSD__)
            int current_fd = events[i].ident;
            #elif defined(__linux__)
            int current_fd = events[i].data.fd;
            #endif
            if (current_fd == fd)
            {
                sockaddr_in addr = {0};
                unsigned int addr_size = sizeof(addr);
                char str[INET_ADDRSTRLEN];
                int new_client = accept(fd, (sockaddr *)&addr, &addr_size);
                std::println("Client accepted");
                add_to_list(new_client);
                struct in_addr ipAddr = addr.sin_addr;
                std::println("{} connected", inet_ntop(AF_INET, &ipAddr, str, INET_ADDRSTRLEN));
                std::println("New fd {}", new_client);
                users.emplace(std::piecewise_construct, std::forward_as_tuple(new_client), std::forward_as_tuple(new_client));
                continue;
            }
            auto current_user_prov = users.find(current_fd);
            if (current_user_prov == users.end())
            {
                remove_from_list(current_fd);
                continue;
            }
            auto &current_user = current_user_prov->second;
            T head = 0;
            status = recv(current_fd, &head, sizeof(T), MSG_PEEK);
            if (status == -1 || status == 0 || head > MAX_PACKET_SIZE)
            {
                std::lock_guard<std::mutex> lock(sync);
                disconnect_user(current_fd);
                continue;
            }
            packet_raw<T> pkt= {0};
            pkt.size = head + sizeof(T);
            pkt.data = (char *)calloc(head + sizeof(T) + 1, sizeof(char));
            status = recv(current_fd, pkt.data, pkt.size, 0);
            if (status == -1 || status == 0)
            {
                std::lock_guard<std::mutex> lock(sync);
                disconnect_user(current_fd);
                continue;
            }
            int data_recv = status;
            bool user_disconnect = false;
            while (data_recv < head + sizeof(T))
            {
                int data_left = (head + sizeof(T)) - data_recv; 
                status = recv(current_fd, &pkt.data[data_recv], data_left, 0);
                if (status == -1 || status == 0)
                {
                    std::lock_guard<std::mutex> lock(sync);
                    disconnect_user(current_fd);
                    user_disconnect = true;
                    break;
                }
                data_recv += status;
            }
            if (user_disconnect == true)
                continue;
            std::lock_guard<std::mutex> lock(sync);
            current_user.packets.push_back(pkt);
            if (std::find(readable.begin(), readable.end(), current_fd) == readable.end())
                readable.push_back(current_fd);
        }
    }
}

template <typename T>
std::map<int, std::vector<packet_raw<T>>> netlib::server<T>::check_packets()
{
    std::unique_lock<std::mutex> lock(sync);
    std::map<int, std::vector<packet_raw<T>>> ret;
    for (auto current_fd: readable)
    {
        user<T> &current_user = users.find(current_fd)->second;
        ret.emplace(std::piecewise_construct, std::forward_as_tuple(current_fd), std::forward_as_tuple(current_user.packets));
        current_user.packets.clear();
        readable.erase(std::remove(readable.begin(), readable.end(), current_fd), readable.end());
    }
    return ret;
}
