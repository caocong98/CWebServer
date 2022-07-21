#include "heap_timer.h"
#include "../http/http_conn.h"

M_heap::~M_heap() {
    for (int i = 0; i < size; ++i) {
        delete Heap[i];
    }
}

void M_heap::adjust_down(int ind) {
    int parent = ind, child = parent * 2;
    while (child <= size) {
        if (child + 1 <= size && Heap[child - 1]->expire > Heap[child]->expire) ++child;
        if (Heap[parent - 1]->expire > Heap[child - 1]->expire) {
            swap(Heap[parent - 1], Heap[child - 1]);
            swap(Heap[parent - 1]->ind, Heap[child - 1]->ind);
            parent = child;
            child = parent * 2;
        }
        else break;
    }
}

void M_heap::adjust_up(int ind) {
    int child = ind, parent = child / 2;
    while (parent >= 1) {
        if (Heap[parent - 1]->expire > Heap[child - 1]->expire) {
            swap(Heap[parent - 1], Heap[child - 1]);
            swap(Heap[parent - 1]->ind, Heap[child - 1]->ind);
            child = parent;
            parent = child / 2;
        }
        else break;
    }
}

void M_heap::add_timer(util_timer* timer) {
    if (!timer) return;
    ++size;
    timer->ind = size;
    Heap.push_back(timer);
    adjust_up(size);
}

void M_heap::del_timer(util_timer* timer) {
    // 这里不使用延迟删除策略避免堆数组膨胀，占用内存过大
    // 延迟删除具体操作：只需要将回调函数设置为null即可，不真正进行删除动作，最后这些未真正删除的元素会堆积在堆数组前半部分。
    // 若使用延迟删除则可以使删除操作达到O1复杂度，否则为Ologn
    //交换至堆尾，删除，调整被交换定时器位置
    if (!timer) return;
    int origin_ind = timer->ind;
    Heap[size - 1]->ind = origin_ind;  //更新交换后元素新位置
    swap(Heap[timer->ind - 1], Heap[size - 1]);
    Heap.pop_back();
    --size;
    adjust_down(origin_ind);
    delete timer;
}

void M_heap::adjust_timer(util_timer* timer) {
    if (!timer) return;
    adjust_down(timer->ind);
}


void M_heap::tick() {
    time_t cur = time(NULL);
    while (size > 0 && Heap[0]->expire < cur) {
        Heap[0]->cb_func(Heap[0]->user_data);
        del_timer(Heap[0]);
    }
}


void Utils::init(int timeslot)
{
    m_TIMESLOT = timeslot;
}

//对文件描述符设置非阻塞
int Utils::setnonblocking(int fd)
{
    int old_option = fcntl(fd, F_GETFL);
    int new_option = old_option | O_NONBLOCK;
    fcntl(fd, F_SETFL, new_option);
    return old_option;
}

//将内核事件表注册读事件，ET模式，选择开启EPOLLONESHOT
void Utils::addfd(int epollfd, int fd, bool one_shot, int TRIGMode)
{
    epoll_event event;
    event.data.fd = fd;

    if (1 == TRIGMode)
        event.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
    else
        event.events = EPOLLIN | EPOLLRDHUP;

    if (one_shot)
        event.events |= EPOLLONESHOT;
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
    setnonblocking(fd);
}

//信号处理函数
void Utils::sig_handler(int sig)
{
    //为保证函数的可重入性，保留原来的errno
    //可重入性表示中断后再次进入该函数，环境变量与之前相同，不会丢失数据
    int save_errno = errno;
    int msg = sig;
    //将信号值从管道写端写入，传输字符类型，而非整型
    send(u_pipefd[1], (char *)&msg, 1, 0);
    errno = save_errno;
}

//设置信号函数
void Utils::addsig(int sig, void(handler)(int), bool restart)
{
    //创建sigaction结构体变量
    struct sigaction sa;
    memset(&sa, '\0', sizeof(sa));
    //信号处理函数中仅仅发送信号值，不做对应逻辑处理
    sa.sa_handler = handler;
    if (restart)
        sa.sa_flags |= SA_RESTART; //设置程序收到信号时的行为，该行为添加重新调用被该信号终止的系统调用
    //将所有信号添加到信号集中
    sigfillset(&sa.sa_mask);
    //执行sigaction函数
    assert(sigaction(sig, &sa, NULL) != -1);
}

//定时处理任务，重新定时以不断触发SIGALRM信号
void Utils::timer_handler()
{
    m_timer_lst.tick();
    alarm(m_TIMESLOT);
}

void Utils::show_error(int connfd, const char *info)
{
    send(connfd, info, strlen(info), 0);
    close(connfd);
}

int *Utils::u_pipefd = 0;
int Utils::u_epollfd = 0;

class Utils;
void cb_func(client_data *user_data)
{
    epoll_ctl(Utils::u_epollfd, EPOLL_CTL_DEL, user_data->sockfd, 0);
    assert(user_data);
    close(user_data->sockfd);
    http_conn::m_user_count--;
}