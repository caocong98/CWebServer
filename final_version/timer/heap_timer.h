/*
 * @Author       : CC
 * @Date         : 2022-07-21
 */ 
#ifndef __HEAPTIMER__H
#define __HEAPTIMER__H

#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <assert.h>
#include <sys/stat.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <stdarg.h>
#include <errno.h>
#include <sys/wait.h>
#include <sys/uio.h>
#include <time.h>
#include <vector>
using namespace std;

// 使用小根堆作为定时器底层结构  插入，修改，删除复杂度均为OlogN, 若使用延迟删除，删除可达到O1复杂度，但是会使堆数组膨胀
// 双链表  复杂度：插入，修改 On, 删除O1

class util_timer;

struct client_data
{
    sockaddr_in address;
    int sockfd;
    util_timer *timer;
};

class util_timer
{   //定时器
public:
    util_timer(){}

public:
    time_t expire;  //超时时间
    void (* cb_func)(client_data *);  //回调处理函数
    client_data *user_data;
    int ind;  //堆中对应索引
};

// 小顶堆

class M_heap {
public:
    M_heap() : size(0) {}
    ~M_heap();

    void add_timer(util_timer* timer);
    void del_timer(util_timer* timer);
    void adjust_timer(util_timer* timer);
    void tick();

private:
    void adjust_down(int ind);
    void adjust_up(int ind);

private:
    vector<util_timer*> Heap;
    int size;
};

//定时器主类
class Utils
{
public:
    Utils() {}
    ~Utils() {}

    void init(int timeslot);

    //对文件描述符设置非阻塞
    int setnonblocking(int fd);

    //将内核事件表注册读事件，ET模式，选择开启EPOLLONESHOT
    void addfd(int epollfd, int fd, bool one_shot, int TRIGMode);

    //信号处理函数
    static void sig_handler(int sig);

    //设置信号函数
    void addsig(int sig, void(handler)(int), bool restart = true);

    //定时处理任务，重新定时以不断触发SIGALRM信号
    void timer_handler();

    void show_error(int connfd, const char *info);

public:
    static int *u_pipefd;
    M_heap m_timer_lst;
    static int u_epollfd;
    int m_TIMESLOT;
};

void cb_func(client_data *user_data);


#endif