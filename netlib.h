#pragma once
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <print>
#include <thread>
#include <sys/event.h>
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
    }
    int fd;
    int data_size;
    int data_alloc_size;
    std::shared_ptr<char []> data;
    int default_size;
    bool readable;
};

class netlib
{
    public:
        netlib()
        {
            fd = 0;
            epfd = 0;
            threads = true;
            default_packet_size = 10;
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
        int default_packet_size;
    private:
        void accept_th();
        void recv_th();
        void disconnect_user(int current_fd);
        std::mutex mut;
        std::condition_variable cond;
        int fd;
        int epfd;
        bool threads;
        std::thread accept_thread;
        std::thread recv_thread;
};

template <typename... T>
inline std::tuple<T...> netlib::read_pkt(std::tuple<T...> packet, int timeout, int current_fd)
{
    constexpr std::size_t size = std::tuple_size_v<decltype(packet)>;
    int size2 = 0;
    const_for_<size>([&](auto i){size2 += sizeof(std::get<i.value>(packet));});
    auto &current_user = users.find(current_fd)->second;
    while (size2 > current_user.data_size)
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
    if (current_user.data_size < default_packet_size)
        current_user.readable = false;
    return packet;
}
