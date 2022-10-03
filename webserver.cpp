#include "webserver.h"
#include <algorithm>

WebServer::WebServer()
{
    //http_conn类对象
    users = new http_conn[MAX_FD];

    //root文件夹路径
    char server_path[200];
    getcwd(server_path, 200);
    char root[6] = "/root";
    char file_root[] = "/root/images/picture/";
    m_root = (char *)malloc(strlen(server_path) + strlen(root) + 1);
    m_file_root = (char *)malloc(strlen(server_path) + strlen(file_root) + 1);
    strcpy(m_root, server_path);
    strcat(m_root, root);

    strcpy(m_file_root, server_path);
    strcat(m_file_root, file_root);
    //定时器
    users_timer = new client_data[MAX_FD];

}

WebServer::~WebServer()
{
    close(m_epollfd);
    close(m_listenfd);
    close(m_pipefd[1]);
    close(m_pipefd[0]);
    delete[] users;
    delete[] users_timer;
    // delete m_pool;
}

void WebServer::init(int port, string user, string passWord, string databaseName, int log_write, 
                     int opt_linger, int trigmode, int sql_num, int thread_num, int close_log, int actor_model)
{
    m_port = port;
    m_user = user;
    m_passWord = passWord;
    m_databaseName = databaseName;
    m_sql_num = sql_num;
    m_thread_num = thread_num;
    m_log_write = log_write;
    m_OPT_LINGER = opt_linger;
    m_TRIGMode = trigmode;
    m_close_log = close_log;
    m_actormodel = actor_model;

    m_file_num = standard_filename();
    resource_init();
}

int WebServer::standard_filename() {
	int filenum = 0;
	DIR* dir;
	dir = opendir(m_file_root);
	dirent* ptr;
	while ((ptr = readdir(dir)) != NULL){
		if (ptr->d_name[0] == '.') continue;
		filenum++;
        string filename = ptr->d_name;
        string file_end = filename.substr(filename.find_last_of('.'));//获取. + 文件后缀
        for (int i = 1; i < file_end.size(); ++i) { //统一小写格式
            file_end[i] = tolower(file_end[i]);
        }
        string fn = to_string(filenum);
        string newname = "P" + fn + file_end; 
        // printf("newname:%s\n", newname.c_str());
        if (ptr->d_name[0] == 'P' && ptr->d_name[1] >= '0' && ptr->d_name[1] <= '9') {
            //处理重启服务器重复修改文件名情况
            m_file_name.push_back(filename);
            continue;
        }
        m_file_name.push_back(newname);
        // printf("newname:%s\n", newname.c_str());
        // printf("oldname:%s\n", filename.c_str());
        filename = m_file_root + filename;
        newname = m_file_root + newname;
        rename(filename.c_str(), newname.c_str());
	}
	closedir(dir);
    sort(m_file_name.begin(), m_file_name.end());
    return filenum;
}

void WebServer::resource_init() {
    // 默认标题 字画图片n   图片资源名 Pn
    // 根据history.txt文件是否为空判断是否初始化默认资源
    string history_root = m_root;
    history_root += "/history.txt";
    ifstream ifs(history_root.c_str());
	string history_content( (istreambuf_iterator<char>(ifs) ),
					 (istreambuf_iterator<char>() ) );
    ifs.close();
    if (history_content.empty()) { //无历史记录，初始化默认标题 字画图片n   图片资源名 Pn

        string text1("<li><a class='smoothScroll' href='/");
        string filename(".html");
        string text2("'><font size='6'>");
        string theme_pic("字画图片");
        string theme_mv("视频文件");
        string text3("</font><br/></a></li>\n");

        string s_file_root = m_root;
        s_file_root.push_back('/');
        string tmp1 = s_file_root + "template1.html";  //用于更新目录页
        string tmp2 = s_file_root + "template2.html"; //用于更新目录页 图片类
        string tmp3 = s_file_root + "template3.html"; //用于更新目录页 视频类
        string update_file1 = s_file_root + "picture.html"; // 更新文件路径
        // printf("route1:%s\n", tmp1.c_str());
        // printf("route2:%s\n", update_file1.c_str());

        ifstream ifs1(tmp1.c_str());
        string content1( (istreambuf_iterator<char>(ifs1) ),
                        (istreambuf_iterator<char>() ) );
        ifs1.close();
        for (int i = 1; i <= m_file_num; ++i) {
            int location = content1.find("mytag1");
            string filename_now = "P" + to_string(i) + filename;  //新建文件名以及目录页更新内容
            string file_end = m_file_name[i - 1].substr(m_file_name[i - 1].find_last_of('.'));//获取. + 文件后缀
            string theme_now = file_end == ".mp4" ? theme_mv + to_string(i) : theme_pic + to_string(i);
            string final = text1 + filename_now + text2 + theme_now + text3;
            content1.insert(location - 1, final); //循环插入 m_file_num行

            //主题名 插入history.
            history_content += theme_now + "\n"; 
            // 新建 Pn.html
            ifstream ifs2;
            if (file_end == ".mp4") { //视频情况
                ifs2.open(tmp3.c_str());
            }
            else {
                ifs2.open(tmp2.c_str());  // 图片情况
            }
            string content2( (istreambuf_iterator<char>(ifs2) ),
                            (istreambuf_iterator<char>() ) );  
            ifs2.close();      
            location = content2.find("theme");
            content2.insert(location + 7, theme_now);
            location = content2.find("images/picture/");
            // printf("file_name:%s\n", m_file_name[i].c_str());
            content2.insert(location + 15, m_file_name[i - 1]);
            string Pn = s_file_root + filename_now;
            ofstream out(Pn.c_str(), std::ios::out);
            out.write(content2.c_str(), content2.size()); // 最终更新 picture.html
            out.close();        
        }
        ofstream history_out(history_root.c_str(), std::ios::out);
        history_out.write(history_content.c_str(), history_content.size());
        history_out.close();
        ofstream out(update_file1.c_str(), std::ios::out);
        out.write(content1.c_str(), content1.size()); // 最终更新 picture.html
        out.close();
    }
    else {
        //存在历史记录则无需重新初始化
    }
}

void WebServer::trig_mode()
{
    //LT + LT
    if (0 == m_TRIGMode)
    {
        m_LISTENTrigmode = 0;
        m_CONNTrigmode = 0;
    }
    //LT + ET
    else if (1 == m_TRIGMode)
    {
        m_LISTENTrigmode = 0;
        m_CONNTrigmode = 1;
    }
    //ET + LT
    else if (2 == m_TRIGMode)
    {
        m_LISTENTrigmode = 1;
        m_CONNTrigmode = 0;
    }
    //ET + ET
    else if (3 == m_TRIGMode)
    {
        m_LISTENTrigmode = 1;
        m_CONNTrigmode = 1;
    }
}

void WebServer::log_write()
{
    if (0 == m_close_log)
    {
        //初始化日志
        if (1 == m_log_write)
            Log::get_instance()->init("./ServerLog", m_close_log, 2000, 800000, 800);
        else
            Log::get_instance()->init("./ServerLog", m_close_log, 2000, 800000, 0);
    }
}

void WebServer::sql_pool()
{
    //初始化数据库连接池
    m_connPool = connection_pool::GetInstance();
    m_connPool->init("localhost", m_user, m_passWord, m_databaseName, 3306, m_sql_num, m_close_log);

    //初始化数据库读取表
    users->initmysql_result(m_connPool);
}

void WebServer::thread_pool()
{
    //线程池
    // m_pool = new threadpool<http_conn>(m_actormodel, m_connPool, m_thread_num);
    m_pool = std::make_unique<ThreadPool>(m_thread_num);
}

void WebServer::eventListen()
{
    //网络编程基础步骤
    m_listenfd = socket(PF_INET, SOCK_STREAM, 0);
    assert(m_listenfd >= 0);

    //优雅关闭连接
    if (0 == m_OPT_LINGER)
    {
        struct linger tmp = {0, 1};
        setsockopt(m_listenfd, SOL_SOCKET, SO_LINGER, &tmp, sizeof(tmp));
    }
    else if (1 == m_OPT_LINGER)
    {
        struct linger tmp = {1, 1};
        setsockopt(m_listenfd, SOL_SOCKET, SO_LINGER, &tmp, sizeof(tmp));
    }

    int ret = 0;
    struct sockaddr_in address;
    bzero(&address, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(m_port);

    int flag = 1;
    setsockopt(m_listenfd, SOL_SOCKET, SO_REUSEADDR, &flag, sizeof(flag));
    ret = bind(m_listenfd, (struct sockaddr *)&address, sizeof(address));
    assert(ret >= 0);
    ret = listen(m_listenfd, 5);
    assert(ret >= 0);

    utils.init(TIMESLOT);

    //epoll创建内核事件表
    // epoll_event events[MAX_EVENT_NUMBER];
    m_epollfd = epoll_create(5);
    assert(m_epollfd != -1);

    utils.addfd(m_epollfd, m_listenfd, false, m_LISTENTrigmode); //listen不开启EPOLLONESHOT
    http_conn::m_epollfd = m_epollfd;

    ret = socketpair(PF_UNIX, SOCK_STREAM, 0, m_pipefd);
    assert(ret != -1);
    //设置管道写端为非阻塞，为什么写端要非阻塞？
    //send是将信息发送给套接字缓冲区，如果缓冲区满了，则会阻塞，这时候会进一步增加信号处理函数的执行时间，
    //为此，将其修改为非阻塞。 未对非阻塞异常情况处理，因为，定时事件可以不是必须处理，忽略后之后还会触发
    utils.setnonblocking(m_pipefd[1]);
    //设置管道读端为ET非阻塞
    utils.addfd(m_epollfd, m_pipefd[0], false, 0);

    utils.addsig(SIGPIPE, SIG_IGN);
    utils.addsig(SIGALRM, utils.sig_handler, false);
    utils.addsig(SIGTERM, utils.sig_handler, false);

    alarm(TIMESLOT);

    //工具类,信号和描述符基础操作
    Utils::u_pipefd = m_pipefd;
    Utils::u_epollfd = m_epollfd;
}

void WebServer::Run(http_conn* request) {  //线程池调用函数，逻辑处理及IO
    if (1 == m_actormodel)  //Reactor 
    {
        if (0 == request->m_state)
        {
            if (request->read_once())
            {
                request->improv = 1;
                connectionRAII mysqlcon(&request->mysql, m_connPool);
                request->process();
            }
            else
            {
                request->improv = 1;
                request->timer_flag = 1;
            }
        }
        else
        {
            if (request->write())
            {
                request->improv = 1;
            }
            else
            {
                request->improv = 1;
                request->timer_flag = 1;
            }
        }
    }
    else  //Proactor
    {
        connectionRAII mysqlcon(&request->mysql, m_connPool);
        request->process();
    }
}


void WebServer::timer(int connfd, struct sockaddr_in client_address)
{
    users[connfd].init(connfd, client_address, m_root, m_file_root, m_CONNTrigmode, m_close_log, m_user, m_passWord, m_databaseName);

    //初始化client_data数据
    //创建定时器，设置回调函数和超时时间，绑定用户数据，将定时器添加到链表中
    users_timer[connfd].address = client_address;
    users_timer[connfd].sockfd = connfd;
    util_timer *timer = new util_timer;
    timer->user_data = &users_timer[connfd];
    timer->cb_func = cb_func;
    time_t cur = time(NULL);
    timer->expire = cur + 3 * TIMESLOT;
    users_timer[connfd].timer = timer;
    utils.m_timer_lst.add_timer(timer);
}

//若有数据传输，则将定时器往后延迟3个单位
//并对新的定时器在链表上的位置进行调整
void WebServer::adjust_timer(util_timer *timer)
{
    time_t cur = time(NULL);
    timer->expire = cur + 3 * TIMESLOT;
    utils.m_timer_lst.adjust_timer(timer);

    LOG_INFO("%s", "adjust timer once");
    // printf("Adjust timer once %s %d\n", inet_ntoa(timer->user_data->address.sin_addr), timer->user_data->sockfd);
}

void WebServer::deal_timer(util_timer *timer, int sockfd)
{
    timer->cb_func(&users_timer[sockfd]);
    if (timer)
    {
        utils.m_timer_lst.del_timer(timer);
    }

    LOG_INFO("close fd %d", users_timer[sockfd].sockfd);
    // printf("close fd %d", users_timer[sockfd].sockfd);
}

bool WebServer::dealclinetdata()
{
    struct sockaddr_in client_address;
    socklen_t client_addrlength = sizeof(client_address);
    if (0 == m_LISTENTrigmode)
    {
        int connfd = accept(m_listenfd, (struct sockaddr *)&client_address, &client_addrlength);
        if (connfd < 0)
        {
            LOG_ERROR("%s:errno is:%d", "accept error", errno);
            return false;
        }
        if (http_conn::m_user_count >= MAX_FD)
        {
            utils.show_error(connfd, "Internal server busy");
            LOG_ERROR("%s", "Internal server busy");
            return false;
        }
        timer(connfd, client_address);
    }

    else
    {
        while (1)
        {
            int connfd = accept(m_listenfd, (struct sockaddr *)&client_address, &client_addrlength);
            if (connfd <= 0)
            {
                LOG_ERROR("%s:errno is:%d", "accept error", errno);
                break;
            }
            if (http_conn::m_user_count >= MAX_FD)
            {
                utils.show_error(connfd, "Internal server busy");
                LOG_ERROR("%s", "Internal server busy");
                break;
            }
            timer(connfd, client_address);
        }
        return false;
    }
    return true;
}

bool WebServer::    dealwithsignal(bool &timeout, bool &stop_server)
{
    int ret = 0;
    int sig;
    char signals[1024];
    ret = recv(m_pipefd[0], signals, sizeof(signals), 0);
    if (ret == -1)
    {
        return false;
    }
    else if (ret == 0)
    {
        return false;
    }
    else
    {
        for (int i = 0; i < ret; ++i)
        {
            switch (signals[i])
            {
            case SIGALRM:
            {
                timeout = true;
                break;
            }
            case SIGTERM:
            {
                stop_server = true;
                break;
            }
            }
        }
    }
    return true;
}

void WebServer::dealwithread(int sockfd)
{
    util_timer *timer = users_timer[sockfd].timer;

    //reactor
    if (1 == m_actormodel)
    {
        if (timer)
        {
            adjust_timer(timer);
        }

        //若监测到读事件，将该事件放入请求队列
        users[sockfd].m_state = 0;
        m_pool->AddTask(std::bind(&WebServer::Run, this, users + sockfd));

        while (true)
        {
            if (1 == users[sockfd].improv)
            {
                if (1 == users[sockfd].timer_flag)
                {
                    deal_timer(timer, sockfd);
                    users[sockfd].timer_flag = 0;
                }
                users[sockfd].improv = 0;
                break;
            }
        }
    }
    else
    {
        //proactor
        if (users[sockfd].read_once())
        {
            LOG_INFO("deal with the client(%s)", inet_ntoa(users[sockfd].get_address()->sin_addr));

            //若监测到读事件，将该事件放入请求队列
            m_pool->AddTask(std::bind(&WebServer::Run, this, users + sockfd));
            // m_pool->append_p(users + sockfd);

            if (timer)
            {
                adjust_timer(timer);
            }
        }
        else
        {
            deal_timer(timer, sockfd);
        }
    }
}

void WebServer::dealwithwrite(int sockfd)
{
    util_timer *timer = users_timer[sockfd].timer;
    //reactor
    if (1 == m_actormodel)
    {
        if (timer)
        {
            adjust_timer(timer);
        }

        users[sockfd].m_state = 1;
        m_pool->AddTask(std::bind(&WebServer::Run, this, users + sockfd));

        while (true)
        {
            if (1 == users[sockfd].improv)
            {
                if (1 == users[sockfd].timer_flag)
                {
                    deal_timer(timer, sockfd);
                    users[sockfd].timer_flag = 0;
                }
                users[sockfd].improv = 0;
                break;
            }
        }
    }
    else
    {
        //proactor
        if (users[sockfd].write())
        {
            LOG_INFO("send data to the client(%s)", inet_ntoa(users[sockfd].get_address()->sin_addr));

            if (timer)
            {
                adjust_timer(timer);
            }
        }
        else //服务器端关闭连接，移除对应的定时器
        {
            deal_timer(timer, sockfd);
        }
    }
}

void WebServer::eventLoop()
{
    bool timeout = false;
    bool stop_server = false;
    // printf("m_actormodel:%d\n", m_actormodel);

    while (!stop_server)
    {
        // printf("wait.\n");
        int number = epoll_wait(m_epollfd, events, MAX_EVENT_NUMBER, -1);
        if (number < 0 && errno != EINTR)
        {
            LOG_ERROR("%s", "epoll failure");
            break;
        }

        for (int i = 0; i < number; i++)
        {
            int sockfd = events[i].data.fd;

            //处理新到的客户连接
            if (sockfd == m_listenfd)
            {
                bool flag = dealclinetdata();
                if (false == flag)
                    continue;
            }
            else if (events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR))
            {
                //服务器端关闭连接，移除对应的定时器
                util_timer *timer = users_timer[sockfd].timer;
                deal_timer(timer, sockfd);
            }
            //处理信号
            else if ((sockfd == m_pipefd[0]) && (events[i].events & EPOLLIN))
            {
                //接收到SIGALRM信号，timeout设置为True，具体逻辑处理放主线程最后执行
                bool flag = dealwithsignal(timeout, stop_server);
                if (false == flag)
                    LOG_ERROR("%s", "dealclientdata failure");
            }
            //处理客户连接上接收到的数据
            else if (events[i].events & EPOLLIN)
            {
                dealwithread(sockfd);
            }
            else if (events[i].events & EPOLLOUT)
            {
                dealwithwrite(sockfd);
            }
        }
        //处理定时器为非必须事件，收到信号并不是立马处理
        //完成读写事件后，再进行处理
        if (timeout)
        {
            utils.timer_handler();

            LOG_INFO("%s", "timer tick");
            // printf("connect num:%d\n", http_conn::m_user_count);

            timeout = false;
        }
    }
}