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
#include <condition_variable>
#include "comp_time_read.h"
#include "comp_time_write.h"

class netlib
{
    public:
        netlib()
        {
            fd = 0;
            epfd = 0;
            threads = true;
            data_size = 0;
            data_alloc_size = 1024;
            data = std::make_shared<char []>(1024);
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
        std::tuple<T...> read_pkt(std::tuple<T...> packet, int timeout);
        std::vector<int> users;
    private:
        void accept_th();
        void recv_th();
        void disconnect_user(int current_fd);
        std::shared_ptr<char []> data;
        std::mutex mut;
        std::condition_variable cond;
        int data_size;
        int data_alloc_size;
        int fd;
        int epfd;
        bool threads;
        std::thread accept_thread;
        std::thread recv_thread;
};

template <typename... T>
inline std::tuple<T...> netlib::read_pkt(std::tuple<T...> packet, int timeout)
{
    constexpr std::size_t size = std::tuple_size_v<decltype(packet)>;
    int size2 = 0;
    const_for_<size>([&](auto i){size2 += sizeof(std::get<i.value>(packet));});
    while (size2 > data_size)
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
    pkt.data = data.get();
    pkt.start_data = data.get();
    pkt.sock = 0;
    pkt.id = 0;
    char_size buff = {.data = pkt.data, .consumed_size = 0, .max_size = (int)pkt.size, .start_data = pkt.data};
    read_comp_pkt(size, buff, packet);
    std::shared_ptr<char[]> newBuffer(new char[data_alloc_size]);
    std::memcpy(newBuffer.get(), &data.get()[size2], data_size - size2);
    data.reset();
    data = newBuffer;
    data_size = data_size - size2;
    return packet;
}
