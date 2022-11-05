#include "http_conn.h"
#include "../UrlCode/url_code.h"
#include <mysql/mysql.h>


//定义http响应的一些状态信息
const char *ok_200_title = "OK";
const char *error_400_title = "Bad Request";
const char *error_400_form = "Your request has bad syntax or is inherently impossible to staisfy.\n";
const char *error_403_title = "Forbidden";
const char *error_403_form = "You do not have permission to get file form this server.\n";
const char *error_404_title = "Not Found";
const char *error_404_form = "The requested file was not found on this server.\n";
const char *error_500_title = "Internal Error";
const char *error_500_form = "There was an unusual problem serving the request file.\n";

const string http_conn::FILE_LOAD_PASSWD1 = "6666"; //vip
const string http_conn::FILE_LOAD_PASSWD2 = "0478"; //SIVP

const int onepage_num = 5; //一页存放资源数量

mutex m_lock;  // 原版本用于保证mysql操作线程安全(多余)，现用于保证多用户同时上传,修改，删除文件可能产生冲突问题
static unordered_map<string, string> users;  //用户名密码本地缓存
unordered_set<string> level1_users; //vip用户本地缓存
unordered_set<string> level2_users; //svip用户本地缓存
// key(服务端称为session，客户端用cookie存储，通常使用算法随机生成，这里使用用户名url编码简单模拟) --- value(用户名)
unordered_map<string, string> session_users; //session本地缓存  加入url-utf8转存机制  cookie存url编码， 用户名用utf-8编码支持中文, 数据库中也存url编码

//总页数   size / onepage_num   if size % onepage_num   +1
//判断当前资源所在页    id / onepage_num  + 1 (id从0开始)    第i页第一个元素id, (i - 1) * onepage_num      
vector<pair<string, string>> web_resources; //按顺序存储静态资源标题名称及文件名称，增删改查，同步更新磁盘文件（服务器初始化）

string HISTORY_CONTENT; //标题记录回写 history.txt

void http_conn::clear_session() {
    if (session_users.size() > 2000) session_users.clear();
}

bool http_conn::change_file(int id) {
    //  更改标题接口,  对应修改之后标题索引及文件名   Pi.jpg  Pi.mp4
    string old_theme = web_resources[id].first;
    web_resources[id].first = c_theme;
    m_lock.lock();
    //更新磁盘文件
    string change_line = old_theme + " " + web_resources[id].second;  //整行查找避免重复标题更新错误（标题+名称确定一行）
    auto idx = HISTORY_CONTENT.find(change_line);
    if (idx != string::npos) {
        HISTORY_CONTENT.replace(idx, old_theme.size(), c_theme);
    }
    else return false;
    ofstream history_out(history_root.c_str(), std::ios::out); //覆盖写
    if (!history_out) {
        m_lock.unlock();
        return false;
    }
    history_out.write(HISTORY_CONTENT.c_str(), HISTORY_CONTENT.size());
    history_out.close();
    m_lock.unlock();
    return true;
}

bool http_conn::delete_file(int id) {
    //删除文件接口
    if (!(id >= 0 && id < web_resources.size()) || web_resources.size() == 0) return false;
    string del_theme = web_resources[id].first;
    string file_name = web_resources[id].second;
    string del_line = del_theme + ' ' + file_name + '\n';
    auto idx = HISTORY_CONTENT.find(del_line);
    if (idx != string::npos) {
        HISTORY_CONTENT.erase(idx, HISTORY_CONTENT.size() - idx); //本地缓存
    }
    else return false;
    m_lock.lock();
    //删除文件
    string del_route = file_root + file_name;
    if (remove(del_route.c_str()) == 0) {
        // cout << "删除成功" << endl;
    }
    else {
        // cout << "删除失败" << endl;
        m_lock.unlock();
        return false;
    }
    //更新索引，及文件名
    for (int i = id; i < web_resources.size() - 1; ++i) {
        web_resources[i] = web_resources[i + 1];
        string old_name = web_resources[i].second;
        string new_name = "P" + to_string(i) + old_name.substr(old_name.find('.'));
        web_resources[i].second = new_name;
        old_name = file_root + old_name;
        new_name = file_root + new_name;
        rename(old_name.c_str(), new_name.c_str());
        string insert_line = web_resources[i].first + ' ' + web_resources[i].second + '\n';
        HISTORY_CONTENT.insert(HISTORY_CONTENT.size(), insert_line);
    }
    web_resources.pop_back();
    ofstream history_out(history_root.c_str(), std::ios::out);  //磁盘文件
    if (!history_out) {
        m_lock.unlock();
        return false;
    }
    history_out.write(HISTORY_CONTENT.c_str(), HISTORY_CONTENT.size());
    history_out.close();
    m_lock.unlock();
    return true;
}

bool http_conn::add_file(const string& filename, const string& contents) {
    int now_file_id = web_resources.size();
    string file_end = m_file_name.substr(m_file_name.find_last_of('.'));//获取. + 文件后缀
    for (int i = 1; i < file_end.size(); ++i) { //统一小写格式
        file_end[i] = tolower(file_end[i]);
    }
    string new_file_name = "P" + to_string(now_file_id) + file_end; //新图片名称  Pn.png
    m_file_name = new_file_name;
    string temp_route = file_root + new_file_name;
    std::ofstream out(temp_route.c_str(), std::ios::binary | std::ios::out);
    if (!out) {
        // printf("open fail\n");
        return false;
    }
    int total_write = contents.size();
    out.write(file_content.c_str(), total_write);
    out.close();
    return true;
}

void http_conn::initmysql_result(connection_pool *connPool)
{
    //先从连接池中取一个连接
    MYSQL *mysql = NULL;
    connectionRAII mysqlcon(&mysql, connPool);

    //在user表中检索username，passwd数据，浏览器端输入
    if (mysql_query(mysql, "SELECT username,passwd,level FROM user"))
    {
        LOG_ERROR("SELECT error:%s\n", mysql_error(mysql));
    }

    //从表中检索完整的结果集
    MYSQL_RES *result = mysql_store_result(mysql);

    //返回结果集中的列数
    int num_fields = mysql_num_fields(result);

    //返回所有字段结构的数组
    MYSQL_FIELD *fields = mysql_fetch_fields(result);

    //从结果集中获取下一行，将对应的用户名和密码，存入map中， vip用户存入本地缓存
    while (MYSQL_ROW row = mysql_fetch_row(result))
    {
        string temp1(row[0]);
        string temp2(row[1]);
        string temp3(row[2]);
        users[temp1] = temp2;
        if (temp3 == "1") level1_users.insert(temp1);
        else if (temp3 == "2") level2_users.insert(temp1);
    }
}

//对文件描述符设置非阻塞
int setnonblocking(int fd)
{
    int old_option = fcntl(fd, F_GETFL);
    int new_option = old_option | O_NONBLOCK;
    fcntl(fd, F_SETFL, new_option);
    return old_option;
}

//将内核事件表注册读事件，ET模式，选择开启EPOLLONESHOT
void addfd(int epollfd, int fd, bool one_shot, int TRIGMode)
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

//从内核时间表删除描述符
void removefd(int epollfd, int fd)
{
    epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, 0);
    close(fd);
}

//将事件重置为EPOLLONESHOT
void modfd(int epollfd, int fd, int ev, int TRIGMode)
{
    epoll_event event;
    event.data.fd = fd;

    if (1 == TRIGMode)
        event.events = ev | EPOLLET | EPOLLONESHOT | EPOLLRDHUP;
    else
        event.events = ev | EPOLLONESHOT | EPOLLRDHUP;
        // event.events = ev | EPOLLRDHUP;

    epoll_ctl(epollfd, EPOLL_CTL_MOD, fd, &event);
}

int http_conn::m_user_count = 0;
int http_conn::m_epollfd = -1;

//关闭连接，关闭一个连接，客户总量减一
void http_conn::close_conn(bool real_close)
{
    if (real_close && (m_sockfd != -1))
    {
        // printf("close %d\n", m_sockfd);
        removefd(m_epollfd, m_sockfd);
        m_sockfd = -1;
        m_user_count--;
    }
}

//初始化连接,外部调用初始化套接字地址
void http_conn::init(int sockfd, const sockaddr_in &addr, char *root, char *froot, int TRIGMode,
                     int close_log, string user, string passwd, string sqlname)
{
    m_sockfd = sockfd;
    m_address = addr;

    addfd(m_epollfd, sockfd, true, m_TRIGMode);//默认开启EPOLLONESHOT
    m_user_count++;

    //当浏览器出现连接重置时，可能是网站根目录出错或http响应格式出错或者访问的文件中内容完全为空
    doc_root = root;
    history_root = root;
    history_root += "/history.txt";
    file_root = froot;
    m_TRIGMode = TRIGMode;
    m_close_log = close_log;

    strcpy(sql_user, user.c_str());
    strcpy(sql_passwd, passwd.c_str());
    strcpy(sql_name, sqlname.c_str());


    init();
}

//初始化新接受的连接
//check_state默认为分析请求行状态
void http_conn::init()
{

    m_total_byte = 0;
    mysql = NULL;
    bytes_to_send = 0;
    bytes_have_send = 0;
    m_check_state = CHECK_STATE_REQUESTLINE;
    m_linger = false;
    m_method = GET;
    if (m_url) delete [] m_url;
    m_url = 0;
    m_version = 0;
    m_content_length = 0;
    if (m_host) delete [] m_host;
    m_host = 0;
    m_start_line = 0;
    m_checked_idx = 0;
    m_read_idx = 0;
    m_write_idx = 0;
    cgi = 0;
    m_state = 0;
    timer_flag = 0;
    improv = 0;
    // string().swap(file_content); //减少内存占用   时间换空间
    file_content.clear(); //空间换时间，空间不释放
    m_file_name = "";
    m_theme = "";
    c_theme = "";
    m_boundary = "";
    m_string = "";
    m_passwd = "";
    exist_judge = 1;

    now_page = -1;
    now_pic = -1;
    c_id = -1;

    now_session.clear();
    now_cookie.clear();
    final_url.clear();
    create_html.clear();
    m_level = LOGOUT;

    memset(m_read_buf, '\0', READ_BUFFER_SIZE);
    memset(m_write_buf, '\0', WRITE_BUFFER_SIZE);
    memset(m_real_file, '\0', FILENAME_LEN);

}

//继续接收文件剩余内容
void http_conn::init1()
{
    m_read_idx = 0;
    memset(m_read_buf, '\0', READ_BUFFER_SIZE);
}

//从状态机，用于分析出一行内容
//返回值为行的读取状态，有LINE_OK,LINE_BAD,LINE_OPEN
http_conn::LINE_STATUS http_conn::parse_line()
{
    char temp;
    for (; m_checked_idx < file_content.size(); ++m_checked_idx)
    {
        temp = m_read_buf[m_checked_idx];
        if (temp == '\r')
        {
            if ((m_checked_idx + 1) == file_content.size())
                return LINE_OPEN;
            else if (m_read_buf[m_checked_idx + 1] == '\n')
            {
                m_read_buf[m_checked_idx++] = '\0';
                m_read_buf[m_checked_idx++] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        }
        else if (temp == '\n')
        {
            if (m_checked_idx > 1 && m_read_buf[m_checked_idx - 1] == '\r')
            {
                m_read_buf[m_checked_idx - 1] = '\0';
                m_read_buf[m_checked_idx++] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        }
    }
    return LINE_OPEN;
}

//循环读取客户数据，直到无数据可读或对方关闭连接
//非阻塞ET工作模式下，需要一次性将数据读完se
bool http_conn::read_once()
{

    int bytes_read = 0;

    //LT读取数据
    if (0 == m_TRIGMode)
    {
        init1();
        bytes_read = recv(m_sockfd, m_read_buf + m_read_idx, READ_BUFFER_SIZE - m_read_idx, 0);
        if (bytes_read > 0) m_total_byte += bytes_read;
        if (bytes_read > 0) m_read_idx += bytes_read;
        for (int i = 0; i < bytes_read; ++i) {
            file_content.push_back(m_read_buf[i]);
        }

        if (bytes_read == 0)  //对方断开连接
        {
            return false;
        }

        return true;
    }
    //ET读数据   
    else
    {
        while (true)
        {
            init1();
            bytes_read = recv(m_sockfd, m_read_buf + m_read_idx, READ_BUFFER_SIZE - m_read_idx, 0);
            if (bytes_read > 0) m_total_byte += bytes_read;
            if (bytes_read > 0) m_read_idx += bytes_read;
            for (int i = 0; i < bytes_read; ++i) {
                file_content.push_back(m_read_buf[i]);
            }
            if (bytes_read == -1)
            {
                if (errno == EAGAIN || errno == EWOULDBLOCK)  //读完再次尝试，EPOLLONESHOT由process函数判断是否重新绑定
                    break;
                return false;
            }
            else if (bytes_read == 0)
            {
                return false;
            }
        }
        return true;
    }
}

//解析http请求行，获得请求方法，目标url及http版本号
http_conn::HTTP_CODE http_conn::parse_request_line(char *text)
{
    //深拷贝请求目标url
    m_url_t = strpbrk(text, " \t");
    if (!m_url_t)
    {
        return BAD_REQUEST;
    }
    *m_url_t++ = '\0';
    char *method = text;
    if (strcasecmp(method, "GET") == 0)
        m_method = GET;
    else if (strcasecmp(method, "POST") == 0)
    {
        m_method = POST;
    }
    else
        return BAD_REQUEST;
    m_url_t += strspn(m_url_t, " \t");
    m_version = strpbrk(m_url_t, " \t");
    if (!m_version)
        return BAD_REQUEST;
    *m_version++ = '\0';
    m_version += strspn(m_version, " \t");
    if (strcasecmp(m_version, "HTTP/1.1") != 0)
        return BAD_REQUEST;
    if (strncasecmp(m_url_t, "http://", 7) == 0)
    {
        m_url_t += 7;
        m_url_t = strchr(m_url_t, '/'); //strchr未查询到符合条件地址返回空指针，造成之后段错误
        if (!m_url_t) return BAD_REQUEST;  //不符合即为非法请求 例:http://eth0.me?Z74225174970Q1  没有/
    }

    if (strncasecmp(m_url_t, "https://", 8) == 0)
    {
        m_url_t += 8;
        m_url_t = strchr(m_url_t, '/');
        if (!m_url_t) return BAD_REQUEST;
    }
    if (strncasecmp(m_url_t, "/fileVerify.action", 18) == 0)
    {
        cgi = 1; //上传文件密码验证
    }
    else if (strncasecmp(m_url_t, "/fileUpload.action", 18) == 0)
    {
        cgi = 2; //上传文件标记
    }
    else if (strncasecmp(m_url_t, "/DELETE.cgi", 11) == 0)
    {
        cgi = 3; //删除文件标记
    }
    else if (strncasecmp(m_url_t, "/UPDATE.cgi", 11) == 0)
    {
        cgi = 4; //标题修改标记
    }
    else if (strncasecmp(m_url_t, "/PAGEJUMP.cgi", 13) == 0)
    {
        cgi = 5; //页码跳转标记
    }
    
    // 添加非法url判断
    // 1、长度大于200(根据实际情况)  2、存在连续/  3、长度大于1末尾为/，末尾去除
    string url_judge = m_url_t;
    if (strlen(m_url_t) > 200) return BAD_REQUEST; // 情况1
    if (url_judge.find("//") != string::npos) return BAD_REQUEST; // 情况2；
    if (url_judge.find("/") == string::npos) return BAD_REQUEST; //针对非法请求 例：GET eth0.me?Z74225174970Q1 HTTP/1.1
    if (url_judge.size() > 1 && url_judge.back() == '/') { // 情况3；
        *(m_url_t + strlen(m_url_t) - 1) = '\0';
    }

    m_url = new char[strlen(m_url_t) + 40];  // 多开辟40长度，避免之后url跳转越界拷贝(do_request函数内)
    strcpy(m_url, m_url_t);
    // printf("m_url:%s\n", m_url);
    if (!m_url || m_url[0] != '/')
        return BAD_REQUEST;
    //当url为/时，显示首页
    if (strlen(m_url) == 1)
        strcat(m_url, "index.html");
    m_check_state = CHECK_STATE_HEADER;
    return NO_REQUEST;
}

//解析http请求的一个头部信息
http_conn::HTTP_CODE http_conn::parse_headers(char *text)
{
    if (text[0] == '\0')
    {
        if (m_content_length != 0)
        {
            if (cgi == 2) {
                m_check_state = CHECK_STATE_FORMDATA;
                return NO_REQUEST;
            }
            else 
                m_check_state = CHECK_STATE_CONTENT;
            return NO_REQUEST;
        }
        return GET_REQUEST;
    }
    else if (strncasecmp(text, "Connection:", 11) == 0)
    {
        text += 11;
        text += strspn(text, " \t");
        if (strcasecmp(text, "keep-alive") == 0)
        {
            m_linger = true;
        }
    }
    else if (strncasecmp(text, "Content-Type: multipart/form-data;", 34) == 0) {
        text += 48;
        m_boundary = text;
    }
    else if (strncasecmp(text, "Content-length:", 15) == 0)
    {
        text += 15;
        text += strspn(text, " \t");
        m_content_length = atol(text);
    }
    else if (strncasecmp(text, "Host:", 5) == 0)
    {
        text += 5;
        text += strspn(text, " \t");
        m_host = new char[strlen(text) + 1];
        strcpy(m_host, text);
    }
    else if (strncasecmp(text, "Cookie:", 7) == 0)
    {
        text += 7;
        text += strspn(text, " \t");
        now_cookie = text;
        now_cookie = now_cookie.substr(now_cookie.find('=') + 1);
        if (session_users.count(now_cookie)) {
            now_session = now_cookie;   //session到期时间内再次访问，延长其到期时间
            if (level1_users.count(session_users[now_cookie])) m_level = VIP;
            else if (level2_users.count(session_users[now_cookie])) m_level = SVIP;
            else m_level = COMMON;
        }
    }
    else  //其它头部数据按需处理
    {
        LOG_INFO("oop!unknow header: %s", text);
    }
    return NO_REQUEST;
}

//判断http请求是否被完整读入
http_conn::HTTP_CODE http_conn::parse_content(char *text)
{
    if (file_content.size() >= (m_content_length + m_checked_idx))
    {
        text[m_content_length] = '\0';
        //POST请求中的最后输入
        // 1、user=cc&password=cc 2、DELETE=delID  3、UPDATE=ID&newtheme=xxx  4、pageID=ID
        m_string = text;
        m_string = UrlDecode(m_string);  //post数据也为url格式
        return GET_REQUEST;
    }
    return NO_REQUEST;
}

//处理formdata 仅仅根据特征判断整个数据包是否接收完毕 
http_conn::HTTP_CODE http_conn::parse_formdata()
{
    if (file_content.substr(file_content.size() - 4, 4) != "--\r\n") {
        return NO_REQUEST;
    }
    return GET_REQUEST;
}

// 状态机主要调用函数，包括内部状态转移
http_conn::HTTP_CODE http_conn::process_read()
{
    LINE_STATUS line_status = LINE_OK;
    HTTP_CODE ret = NO_REQUEST;
    char *text = 0;
    if (m_TRIGMode == 1) {
        //ET模式特殊处理，ET模式先读完数据后处理，需要将请求行，请求头部重新输入处理buffer中
        // printf("ET mode\n");
        memset(m_read_buf, '\0', READ_BUFFER_SIZE);
        string line_header = file_content.substr(0, READ_BUFFER_SIZE);
        strcpy(m_read_buf, line_header.c_str());
    }
    // else printf("LT mode\n");
    while (m_check_state == CHECK_STATE_FORMDATA || (m_check_state == CHECK_STATE_CONTENT && line_status == LINE_OK) || ((line_status = parse_line()) == LINE_OK))
    {
        if (m_check_state != CHECK_STATE_FORMDATA) {
            text = get_line();
            m_start_line = m_checked_idx;
            LOG_INFO("%s", text);
        }
        switch (m_check_state)
        {
        case CHECK_STATE_REQUESTLINE:
        {
            ret = parse_request_line(text);
            if (ret == BAD_REQUEST)
                return BAD_REQUEST;
            break;
        }
        case CHECK_STATE_HEADER:
        {
            ret = parse_headers(text);
            if (ret == BAD_REQUEST)
                return BAD_REQUEST;
            else if (ret == GET_REQUEST)
            {
                return do_request();
            }
            break;
        }
        case CHECK_STATE_CONTENT:
        {
            ret = parse_content(text);
            if (ret == GET_REQUEST)
                return do_request();
            line_status = LINE_OPEN;
            break;
        }
        case CHECK_STATE_FORMDATA:
        {
            ret = parse_formdata();
            if (ret == GET_REQUEST)
                return do_request();
            return NO_REQUEST;
        }
        default:
            return INTERNAL_ERROR;
        }
    }
    return NO_REQUEST;
}

http_conn::HTTP_CODE http_conn::do_request()
{
    strcpy(m_real_file, doc_root);
    int len = strlen(doc_root);
    string url = m_url;
    url = url.substr(url.find_last_of('/') + 1);

    //处理注册登陆
    if ((url == "2CGISQL.cgi" || url == "3CGISQL.cgi") && m_method == POST)
    {

        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/");
        strcat(m_url_real, m_url + 2);
        strncpy(m_real_file + len, m_url_real, FILENAME_LEN - len - 1);
        free(m_url_real);

        //将用户名和密码提取出来
        //user=123&passwd=123
        char name[100], password[100];
        int i;
        for (i = 5; i < m_string.size() && m_string[i] != '&'; ++i)
            name[i - 5] = m_string[i];
        name[i - 5] = '\0';

        int j = 0;
        for (i = i + 10; i < m_string.size(); ++i, ++j)
            password[j] = m_string[i];
        password[j] = '\0';

        if (url == "3CGISQL.cgi")
        {
            //如果是注册，先检测数据库中是否有重名的
            //没有重名的，进行增加数据
            char *sql_insert = (char *)malloc(sizeof(char) * 200);
            strcpy(sql_insert, "INSERT INTO user(username, passwd) VALUES(");
            strcat(sql_insert, "'");
            strcat(sql_insert, UrlDecode(name).c_str());
            strcat(sql_insert, "', '");
            strcat(sql_insert, password);
            strcat(sql_insert, "')");
            LOG_INFO("Register name: %s", name);
            if (users.find(name) == users.end())
            {
                //每条线程对于一条数据库连接应该不存在线程安全问题
                // m_lock.lock();
                int res = mysql_query(mysql, sql_insert);
                // m_lock.unlock();

                if (!res) {
                    strcpy(m_url, "/registersuccess.html");
                    users.insert(pair<string, string>(name, password));
                }
                else
                    strcpy(m_url, "/registerError.html");
            }
            else
                strcpy(m_url, "/registerError.html");
        }
        //如果是登录，直接判断
        //若浏览器端输入的用户名和密码在表中可以查找到，返回1，否则返回0
        else if (url == "2CGISQL.cgi")
        {
            if (users.find(name) != users.end() && users[name] == password) {
                strcpy(m_url, "/page1.html");
                now_session = name;
                now_cookie = name; // 登陆操作客户端未存cookie，提前更新用于之后用户状态判断
                session_users[name] = UrlDecode(name); //存储cookie
                //登陆成功后 需要更新用户权限
                if (level1_users.count(session_users[now_cookie])) m_level = VIP;
                else if (level2_users.count(session_users[now_cookie])) m_level = SVIP;
                else m_level = COMMON;
            }
            else
                strcpy(m_url, "/logError.html");
        }
    }

    if (url == "logout.html") { //注销处理
        session_users.erase(now_cookie);
        m_level = LOGOUT;
    }

    // 验证文件上传密码
    if (cgi == 1) {
        //获取上传密码
        // printf("%s\n\n", file_content.c_str());
        size_t ind = file_content.find("rootpasswd");
        ind += 15;
        while (file_content[ind] != '\r') {
            m_passwd.push_back(file_content[ind++]);
            // printf("%s\n", m_passwd.c_str());
        }
        //验证  //提升用户权限 同步更新数据库  及本地缓存
        if (m_passwd == FILE_LOAD_PASSWD1) {
            char *sql_insert = (char *)malloc(sizeof(char) * 200);
            strcpy(sql_insert, "update user set level=1 where username='");
            strcat(sql_insert, now_cookie.c_str());
            strcat(sql_insert, "'");
            int ret = mysql_query(mysql, sql_insert);
            if (!ret) {
                level1_users.insert(session_users[now_cookie]);
                level2_users.erase(session_users[now_cookie]);
                m_level = VIP;
                strcpy(m_url, "/fileload.html");
            }
            else {
                strcpy(m_url, "/fileloadverifyfail.html");
            }
        }
        else if (m_passwd == FILE_LOAD_PASSWD2) {
            char *sql_insert = (char *)malloc(sizeof(char) * 200);
            strcpy(sql_insert, "update user set level=2 where username='");
            strcat(sql_insert, now_cookie.c_str());
            strcat(sql_insert, "'");
            int ret = mysql_query(mysql, sql_insert);
            if (!ret) {
                level2_users.insert(session_users[now_cookie]);
                level1_users.erase(session_users[now_cookie]);
                m_level = SVIP;
                strcpy(m_url, "/fileload.html");
            }
            else {
                strcpy(m_url, "/fileloadverifyfail.html");
            }            
        }
        else {
            strcpy(m_url, "/fileloadverifyfail.html");
        }
    }

    //处理文件上传结果
    else if (cgi == 2)
    {
        // 获取文件名称
        size_t id1 = file_content.find("filename");
        id1 += 10; //文件名第一个字符
        while (file_content[id1] != '"') {
            m_file_name.push_back(file_content[id1++]);
        }
        if (m_file_name.empty()) {  //处理空提交情况   前端已处理
            strcpy(m_url, "/loadempty.html");
        }
        else {
            // 获取文件主题
            size_t id2 = file_content.find("theme");
            if (id2 != string().npos) {
                id2 += 10; //文件主题第一个字符
                while (file_content[id2] != '\r') {
                    m_theme.push_back(file_content[id2++]);
                }
            }
            // if (m_theme.size() == 0) printf("Empty theme.\n");
            // else printf("The theme is : %s\n", m_theme.c_str());
            // 截取文件主体内容
            id1 = file_content.find("\r\n\r\n", id1);
            id1 += 4;
            // diff = id1; // 剔除了前diff个字符
            id2 = file_content.find(m_boundary, id1);
            // printf("oldcontent:\n%s\n", file_content.c_str());
            file_content = file_content.substr(id1, id2 - id1 - 8); //剔除前后多余内容
            // printf("newcontent:\n%s\n", file_content.c_str());

            //执行指定业务功能
            //  多用户同时提交文件可能出现线程不安全情况 下面一段，直接加互斥锁，消耗大。
            m_lock.lock();
            if (add_file(m_file_name, file_content)) {
                //将标题记录进history.txt
                string add_theme = m_theme;
                web_resources.push_back({m_theme, m_file_name});
                string add_content = m_theme + ' ' + m_file_name + '\n';
                ofstream history_out(history_root.c_str(), std::ios::app); //追加写入 磁盘文件
                history_out.write(add_content.c_str(), add_content.size());
                history_out.close();

                HISTORY_CONTENT += add_content;  //更新本地缓存

                strcpy(m_url, "/loadsuccess.html");
            }
            // 多用户同时提交文件可能出现线程不安全情况
            else {
                //文件名冲突等情况
                strcpy(m_url, "/loadfail.html");
            }
            m_lock.unlock();
        }
    }
    else if (cgi == 3) {  //删除  2、DELETE=delID   4、pageID=ID
        c_id = stoi(m_string.substr(m_string.find("DELETE=") + 7));
        if (delete_file(c_id)) {
            strcpy(m_url, "/deletesuccess.html");
        }
        else {
            strcpy(m_url, "/deletefail.html");
        }
    }
    else if (cgi == 4) {  //更新标题  UPDATE=ID&newtheme=xxx
        int idx = 7;
        string idtemp("");
        while (m_string[idx] != '&') {
            idtemp.push_back(m_string[idx++]);
        }
        c_id = stoi(idtemp);
        idx += 10; //新标题索引
        c_theme = m_string.substr(idx);
        if (change_file(c_id)) {
            strcpy(m_url, "/updatesuccess.html");
        }
        else {
            strcpy(m_url, "/updatefail.html");
        }
    }
    else if (cgi == 5) { //目录页跳转  pageID=ID
        now_page = stoi(m_string.substr(m_string.find("pageID=") + 7));
        string n_url = "/page" + to_string(now_page) + ".html";
        strcpy(m_url, n_url.c_str());
    }

    //404判断   pageID.html  和  PID.html 404提前判断
    url = m_url;
    url = url.substr(url.find_last_of('/') + 1);
    if (now_page != -1) {  //跳转 页面html生成 不用判断html文件是否存在
        exist_judge = 0;
    } 
    else if (url.find("page") != string::npos && url.find_last_of(".html") != string::npos) {
        int flag = 0;
        int begin = 4, end = url.size() - 6;
        string temp_page("");
        for (int i = begin; i <= end; ++i) {
            if (url[i] >= '0' && url[i] <= '9') temp_page.push_back(url[i]);
            else {
                flag = 1;
                break;
            }
        }
        if (flag == 0) {
            now_page = stoi(temp_page);
            int totalpage = web_resources.size() / onepage_num;
            if (web_resources.size() % onepage_num) ++totalpage;
            if (now_page <= totalpage || (now_page == 1 && totalpage == 0)) exist_judge = 0; //0页默认为第一页
            else now_page = -1;
        }
    }
    else if (url.find('P') != string::npos && url.find(".html") != string::npos) {
        int begin = 1, end = url.size() - 6;
        int flag = 0;
        string temp_pic("");
        for (int i = begin; i <= end; ++i) {
            if (url[i] >= '0' && url[i] <= '9') temp_pic.push_back(url[i]);
            else {
                flag = 1;
                break;
            }
        }
        if (flag == 0) {
            now_pic = stoi(temp_pic);
            if (now_pic < web_resources.size()) exist_judge = 0;
            else now_pic = -1;
        }        
    }
    strncpy(m_real_file + len, m_url, FILENAME_LEN - len - 1);

    //404判断   pageID.html  和  PID.html 404提前判断

    if (exist_judge) {
        string temp404 = m_url;
        while (stat(m_real_file, &m_file_stat) < 0) {
            // 访问不存在资源 进入404界面  处理多个/ 404界面css等资源访问不到情况
            auto nextid = temp404.find('/', 1);
            if (nextid == -1) {
                string f404 = doc_root;
                f404 += "/404.html";
                strcpy(m_real_file, f404.c_str());
            }
            else {
                temp404 = temp404.substr(nextid);
                string f404 = doc_root;
                f404 += temp404;
                strcpy(m_real_file, f404.c_str());
            }
        }

        if (!(m_file_stat.st_mode & S_IROTH))
            return FORBIDDEN_REQUEST;

        if (S_ISDIR(m_file_stat.st_mode))
            return BAD_REQUEST;
    }

    //针对html文件先读取文件根据情况生成完整html, 针对其它文件通过mmap映射传输
    final_url = m_real_file;
    if (final_url.find(".html") != -1) {
        if (now_pic != -1) {   //图片视频资源页
            string temp = doc_root;
            final_url = temp + "/template2.html";
            ifstream ifs1(final_url.c_str());
            string content1( (istreambuf_iterator<char>(ifs1) ),
                            (istreambuf_iterator<char>() ) );
            ifs1.close();
            string final1; //用户状态
            string final2; //字画集跳转  跳转至当前资源所在页
            int now_page = now_pic / onepage_num + 1;
            if (m_level == LOGOUT) { //未登陆状态
                final1 += "<li><a class='smoothScroll' href='/register.html'><font size='6'>注册</font><br/></a></li>\n";
                final1 += "<li><a class='smoothScroll' href='/login.html'><font size='6'>登录</font><br/></a></li>\n";

                final2 = "<li><a class='smoothScroll' href='/login.html'><font size='6'>字画集</font><br/></a></li>\n";
            } 
            else if (m_level == VIP) {   
                final1 = "<li><a class='smoothScroll' id='VIP' style='color:#ff0000'><font size='3' >VIP用户: "
                + session_users[now_cookie] + "</font><br/></a></li>\n" + 
                "<li><a class='smoothScroll' href='/logout.html'; style='color:#000000'><font size='3'>用户注销</font><br/></a></li>";

                final2 = "<li><a class='smoothScroll' href='/page" + to_string(now_page) +".html'><font size='6'>字画集</font><br/></a></li>\n";
            }
            else if (m_level == SVIP) { //SVIP
                final1 = "<li><a class='smoothScroll' id='SVIP' style='color:#ff0000'><font size='3' >SVIP用户: "
                + session_users[now_cookie] + "</font><br/></a></li>\n" + 
                "<li><a class='smoothScroll' href='/logout.html'; style='color:#000000'><font size='3'>用户注销</font><br/></a></li>";
                
                final2 = "<li><a class='smoothScroll' href='/page" + to_string(now_page) +".html'><font size='6'>字画集</font><br/></a></li>\n";
            }
            else {  //普通用户
                final1 = "<li><a class='smoothScroll' style='color:#000000'><font size='3' >普通用户: "
                + session_users[now_cookie] + "</font><br/></a></li>\n" + 
                "<li><a class='smoothScroll' href='/logout.html'; style='color:#000000'><font size='3'>用户注销</font><br/></a></li>";
                
                final2 = "<li><a class='smoothScroll' href='/page" + to_string(now_page) +".html'><font size='6'>字画集</font><br/></a></li>\n";
            }
            int location1 = content1.find("mytag2"); //用户状态
            content1.insert(location1 - 1, final1); 
            int location2 = content1.find("mytag3"); //字画集跳转链接
            content1.insert(location2 - 1, final2); 
            content1.erase(content1.find("mytag2"), 6);
            content1.erase(content1.find("mytag3"), 6);
            // 标题显示
            content1.replace(content1.find("yourtheme"), 9, web_resources[now_pic].first);
            // 资源地址更新
            string now_address;
            string filename = web_resources[now_pic].second;
            if (filename.find(".mp4") != -1) { //视频
                now_address = "<video width='906' height='506' controls><source src='images/picture/" + filename + 
                "' type='video/mp4'></video>";
            }
            else { //图片
                now_address = "<img style='width: 40vw;' src='images/picture/" + filename +  "' alt='pic'/>";
            }
            content1.replace(content1.find("youraddress"), 11, now_address);
            //  删除，修改功能指定ID修改
            string ID_temp = "nowID";
            content1.replace(content1.find(ID_temp), ID_temp.size(), to_string(now_pic));
            content1.replace(content1.find(ID_temp), ID_temp.size(), to_string(now_pic));
            create_html = content1;
            m_file_address = const_cast<char*>(create_html.c_str());
            m_file_stat.st_size = create_html.size();            
        }
        else if (now_page != -1) { //目录页
            string temp = doc_root;
            final_url = temp + "/template1.html";
            ifstream ifs1(final_url.c_str());
            string content1( (istreambuf_iterator<char>(ifs1) ),
                            (istreambuf_iterator<char>() ) );
            ifs1.close();
            string final1;
            string final2;
            if (m_level == LOGOUT) {
                final1 += "<li><a class='smoothScroll' href='/register.html'><font size='6'>注册</font><br/></a></li>\n";
                final1 += "<li><a class='smoothScroll' href='/login.html'><font size='6'>登录</font><br/></a></li>\n";
                
                final2 = "<li><a class='smoothScroll'><font size='4'>作品上传请先登录</font><br/></a></li>\n";
            }//未登陆状态， 
            else if (m_level == VIP) {   //VIP
                final1 = "<li><a class='smoothScroll' id='VIP' style='color:#ff0000'><font size='3' >VIP用户: "
                + session_users[now_cookie] + "</font><br/></a></li>\n" + 
                "<li><a class='smoothScroll' href='/logout.html'; style='color:#000000'><font size='3'>用户注销</font><br/></a></li>";

                final2 = "<li><a class='smoothScroll' href='/fileload.html'><font size='6'>作品上传</font><br/></a></li>\n";
            }
            else if (m_level == SVIP) { //SVIP
                final1 = "<li><a class='smoothScroll' id='SVIP' style='color:#ff0000'><font size='3' >SVIP用户: "
                + session_users[now_cookie] + "</font><br/></a></li>\n" + 
                "<li><a class='smoothScroll' href='/logout.html'; style='color:#000000'><font size='3'>用户注销</font><br/></a></li>";
                
                final2 = "<li><a class='smoothScroll' href='/fileload.html'><font size='6'>作品上传</font><br/></a></li>\n";
            }
            else {  //普通用户
                final1 = "<li><a class='smoothScroll' style='color:#000000'><font size='3' >普通用户: "
                + session_users[now_cookie] + "</font><br/></a></li>\n" + 
                "<li><a class='smoothScroll' href='/logout.html'; style='color:#000000'><font size='3'>用户注销</font><br/></a></li>";
                
                final2 = "<li><a class='smoothScroll' href='/fileloadverify.html'><font size='6'>作品上传</font><br/></a></li>\n";
            }
            int location1 = content1.find("mytag2"); //用户状态
            content1.insert(location1 - 1, final1); 
            int location2 = content1.find("mytag4"); //作品上传链接
            content1.insert(location2 - 1, final2); 
            content1.erase(content1.find("mytag2"), 6);
            content1.erase(content1.find("mytag4"), 6);
            //根据页号添加目录内容
            int location3 = content1.find("mytag1");
            string page_content;
            int begin_pic_num = (now_page - 1) * onepage_num;
            int end_pic_num = begin_pic_num + onepage_num - 1;
            for (int i = begin_pic_num; i < web_resources.size() && i <= end_pic_num; ++i) {
                string pic = "P" + to_string(i);
                string now_theme = web_resources[i].first;
                if (now_theme.size() < 15) {
                    int cnt = 15 - now_theme.size();
                    while (cnt--) now_theme += "&nbsp;&nbsp;"; //展示页标题默认最短15长度,&nbsp;为html空格
                } 
                page_content += "<li><a class='smoothScroll' href='/" + pic + ".html" +
                "'><font size='7'>" + now_theme + "</font><br/></a></li>\n";
            }
            content1.insert(location3 - 1, page_content);
            //添加页码跳转部分内容
            int totalpage = web_resources.size() / onepage_num;
            if (web_resources.size() % onepage_num) ++totalpage;
            if (totalpage == 0) ++totalpage; //无资源默认有一页
            page_content = "第 " + to_string(now_page) + " / " + to_string(totalpage) + " 页"; 
            content1.replace(content1.find("pageshow"), 8, page_content); //当前页码显示
            content1.replace(content1.find("maxpage"), 7, to_string(totalpage));
            create_html = content1;
            m_file_address = const_cast<char*>(create_html.c_str());
            m_file_stat.st_size = create_html.size();      
        }
        else if (cgi == 3 || cgi == 4) { //删除文件 及 更新标题 跳转
            ifstream ifs1(final_url.c_str());
            string content1( (istreambuf_iterator<char>(ifs1) ),
                            (istreambuf_iterator<char>() ) );
            ifs1.close();
            string final1; //用户状态
            string final2; //字画集跳转
            if (m_level == LOGOUT) { //未登陆状态
                final1 += "<li><a class='smoothScroll' href='/register.html'><font size='6'>注册</font><br/></a></li>\n";
                final1 += "<li><a class='smoothScroll' href='/login.html'><font size='6'>登录</font><br/></a></li>\n";

                final2 = "<li><a class='smoothScroll' href='/login.html'><font size='6'>字画集</font><br/></a></li>\n";
            } 
            else if (m_level == VIP) {   //VIP
                final1 = "<li><a class='smoothScroll' id='VIP' style='color:#ff0000'><font size='3' >VIP用户: "
                + session_users[now_cookie] + "</font><br/></a></li>\n" + 
                "<li><a class='smoothScroll' href='/logout.html'; style='color:#000000'><font size='3'>用户注销</font><br/></a></li>";

                final2 = "<li><a class='smoothScroll' href='/page1.html'><font size='6'>字画集</font><br/></a></li>\n";
            }
            else if (m_level == SVIP) { //SVIP
                final1 = "<li><a class='smoothScroll' id='SVIP' style='color:#ff0000'><font size='3' >SVIP用户: "
                + session_users[now_cookie] + "</font><br/></a></li>\n" + 
                "<li><a class='smoothScroll' href='/logout.html'; style='color:#000000'><font size='3'>用户注销</font><br/></a></li>";
                
                final2 = "<li><a class='smoothScroll' href='/page1.html'><font size='6'>字画集</font><br/></a></li>\n";
            }
            else {  //普通用户
                final1 = "<li><a class='smoothScroll' style='color:#000000'><font size='3' >普通用户: "
                + session_users[now_cookie] + "</font><br/></a></li>\n" + 
                "<li><a class='smoothScroll' href='/logout.html'; style='color:#000000'><font size='3'>用户注销</font><br/></a></li>";
                
                final2 = "<li><a class='smoothScroll' href='/page1.html'><font size='6'>字画集</font><br/></a></li>\n";
            }
            int location1 = content1.find("mytag2"); //用户状态
            content1.insert(location1 - 1, final1); 
            int location2 = content1.find("mytag3"); //字画集跳转链接
            content1.insert(location2 - 1, final2); 
            content1.erase(content1.find("mytag2"), 6);
            content1.erase(content1.find("mytag3"), 6);
            now_page = c_id / onepage_num + 1;
            if (final_url.find("deletesuccess.html") != -1) {  //删除资源成功可能发生页码变化，其它情况删除更新操作相同
                //跳转删除资源原先所在目录， 若该资源目录只有该资源则返回上一页，若只有一个资源则返回第一页
                if (c_id % onepage_num == 0 && c_id == web_resources.size()) {
                    now_page--;
                }
                if (now_page == 0) ++now_page;               
            }
            string page_t = "page" + to_string(now_page) + ".html";
            content1.replace(content1.find("backid"), 6, page_t);
            create_html = content1; 
            m_file_address = const_cast<char*>(create_html.c_str());
            m_file_stat.st_size = create_html.size();            
        }
        else {  //其它情况 首页，404等
            //插入用户显示区
            ifstream ifs1(final_url.c_str());
            string content1( (istreambuf_iterator<char>(ifs1) ),
                            (istreambuf_iterator<char>() ) );
            ifs1.close();
            string final1; //用户状态
            string final2; //字画集跳转
            if (m_level == LOGOUT) { //未登陆状态
                final1 += "<li><a class='smoothScroll' href='/register.html'><font size='6'>注册</font><br/></a></li>\n";
                final1 += "<li><a class='smoothScroll' href='/login.html'><font size='6'>登录</font><br/></a></li>\n";

                final2 = "<li><a class='smoothScroll' href='/login.html'><font size='6'>字画集</font><br/></a></li>\n";
            } 
            else if (m_level == VIP) {   //VIP
                final1 = "<li><a class='smoothScroll' id='VIP' style='color:#ff0000'><font size='3' >VIP用户: "
                + session_users[now_cookie] + "</font><br/></a></li>\n" + 
                "<li><a class='smoothScroll' href='/logout.html'; style='color:#000000'><font size='3'>用户注销</font><br/></a></li>";

                final2 = "<li><a class='smoothScroll' href='/page1.html'><font size='6'>字画集</font><br/></a></li>\n";
            }
            else if (m_level == SVIP) { //SVIP
                final1 = "<li><a class='smoothScroll' id='SVIP' style='color:#ff0000'><font size='3' >SVIP用户: "
                + session_users[now_cookie] + "</font><br/></a></li>\n" + 
                "<li><a class='smoothScroll' href='/logout.html'; style='color:#000000'><font size='3'>用户注销</font><br/></a></li>";
                
                final2 = "<li><a class='smoothScroll' href='/page1.html'><font size='6'>字画集</font><br/></a></li>\n";
            }
            else {  //普通用户
                final1 = "<li><a class='smoothScroll' style='color:#000000'><font size='3' >普通用户: "
                + session_users[now_cookie] + "</font><br/></a></li>\n" + 
                "<li><a class='smoothScroll' href='/logout.html'; style='color:#000000'><font size='3'>用户注销</font><br/></a></li>";
                
                final2 = "<li><a class='smoothScroll' href='/page1.html'><font size='6'>字画集</font><br/></a></li>\n";
            }
            //上传文件成功跳转对应页码(末尾添加，即最后一页)
            if (cgi == 2 && final_url.find("loadsuccess.html") != -1 ) {
                int totalpage = web_resources.size() / onepage_num;
                if (web_resources.size() % onepage_num) ++totalpage;
                final2 = "<li><a class='smoothScroll' href='/page" + to_string(totalpage) + ".html'><font size='6'>返回</font><br/></a></li>\n";                
            }

            int location1 = content1.find("mytag2"); //用户状态
            content1.insert(location1 - 1, final1); 
            int location2 = content1.find("mytag3"); //字画集跳转链接
            content1.insert(location2 - 1, final2); 
            content1.erase(content1.find("mytag2"), 6);
            content1.erase(content1.find("mytag3"), 6);
            create_html = content1;
            m_file_address = const_cast<char*>(create_html.c_str());
            m_file_stat.st_size = create_html.size();           
        }
    }
    else {
        int fd = open(m_real_file, O_RDONLY);
        m_file_address = (char *)mmap(0, m_file_stat.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
        close(fd);
    }
    return FILE_REQUEST;
}

void http_conn::unmap()
{
    if (m_file_address && final_url.find(".html") == -1)
    {
        munmap(m_file_address, m_file_stat.st_size);
        m_file_address = 0;
    }
}
bool http_conn::write()
{
    int temp = 0;
    // ET写
    //若要发送的数据长度为0
    //表示响应报文为空，一般不会出现这种情况
    if (bytes_to_send == 0)
    {
        modfd(m_epollfd, m_sockfd, EPOLLIN, m_TRIGMode);
        init();
        return true;
    }

    while (1)
    {
        //将响应报文的状态行、消息头、空行和响应正文发送给浏览器端
        temp = writev(m_sockfd, m_iv, m_iv_count);
        // printf("sent %d datas.\n", temp);
        if (temp < 0)
        {
            //socket是非阻塞时,如返回此错误,表示写缓冲队列已满
            if (errno == EAGAIN)
            {
  	            // 方案1、在这里做延时后再重试，保证一次性写完，避免重复调用epoll_wait
                // usleep(1000);
                // continue;
                // 方案2、重新注册EPPOLLONESHOT
                modfd(m_epollfd, m_sockfd, EPOLLOUT, m_TRIGMode);//重新注册EPOLLONESHOT！！
                return true;
            }
            //否则关闭连接
            unmap();
            return false;
        }

        bytes_have_send += temp;
        bytes_to_send -= temp;

        //第一个iov头部信息发送完毕
        if (bytes_have_send >= m_iv[0].iov_len)
        {
            //不再发送第一个iov
            m_iv[0].iov_len = 0;
            //更新第二个iov起始地址，及剩余长度
            m_iv[1].iov_base = m_file_address + (bytes_have_send - m_write_idx);
            m_iv[1].iov_len = bytes_to_send;
        }
        else
        {
            //继续发送第一个iov,更新相关信息
            m_iv[0].iov_base = m_write_buf + bytes_have_send;
            m_iv[0].iov_len = m_iv[0].iov_len - bytes_have_send;
        }

        if (bytes_to_send <= 0)
        {
            //发送完毕
            unmap();
            //重置EPOLLONESHOT
            modfd(m_epollfd, m_sockfd, EPOLLIN, m_TRIGMode);

            if (m_linger)
            {
                //长连接
                init();
                return true;
            }
            else
            {
                return false;
            }
        }
    }
}
bool http_conn::add_response(const char *format, ...)
{
    //如果写入内容超出m_write_buf大小则报错
    if (m_write_idx >= WRITE_BUFFER_SIZE)
        return false;
    
    //定义可变参数列表
    va_list arg_list;
    //将变量arg_list初始化为传入参数
    va_start(arg_list, format);
    //将数据format从可变参数列表写入缓冲区写，返回写入数据的长度
    int len = vsnprintf(m_write_buf + m_write_idx, WRITE_BUFFER_SIZE - 1 - m_write_idx, format, arg_list);
    //如果写入的数据长度超过缓冲区剩余空间，则报错
    if (len >= (WRITE_BUFFER_SIZE - 1 - m_write_idx))
    {
        va_end(arg_list);
        return false;
    }
    //更新m_write_idx位置
    m_write_idx += len;
    //清空可变参列表
    va_end(arg_list);

    LOG_INFO("request:%s", m_write_buf);

    return true;
}
bool http_conn::add_status_line(int status, const char *title)
{
    return add_response("%s %d %s\r\n", "HTTP/1.1", status, title);
}
bool http_conn::add_headers(int content_len)
{
    return add_content_length(content_len) && add_linger() && (now_session.empty() || add_cookie(now_session.c_str())) &&
           add_blank_line();
}
bool http_conn::add_content_length(int content_len)
{
    return add_response("Content-Length:%d\r\n", content_len);
}
bool http_conn::add_content_type()
{
    return add_response("Content-Type:%s\r\n", "text/html");
}
bool http_conn::add_linger()
{
    return add_response("Connection:%s\r\n", (m_linger == true) ? "keep-alive" : "close");
}
bool http_conn::add_blank_line()
{
    return add_response("%s", "\r\n");
}
bool http_conn::add_content(const char *content)
{
    return add_response("%s", content);
}
bool http_conn::add_cookie(const char *username)
{
    return add_response("Set-Cookie:userid=%s; max-age=172800; path=/\r\n", username);
}
bool http_conn::process_write(HTTP_CODE ret)
{
    switch (ret)
    {
    case INTERNAL_ERROR:
    {
        add_status_line(500, error_500_title);
        add_headers(strlen(error_500_form));
        if (!add_content(error_500_form))
            return false;
        break;
    }
    case BAD_REQUEST:
    {
        add_status_line(404, error_404_title);
        add_headers(strlen(error_404_form));
        if (!add_content(error_404_form))
            return false;
        break;
    }
    case FORBIDDEN_REQUEST:
    {
        add_status_line(403, error_403_title);
        add_headers(strlen(error_403_form));
        if (!add_content(error_403_form))
            return false;
        break;
    }
    case FILE_REQUEST:
    {
        add_status_line(200, ok_200_title);
        if (m_file_stat.st_size != 0)
        {
            add_headers(m_file_stat.st_size);
            m_iv[0].iov_base = m_write_buf;
            m_iv[0].iov_len = m_write_idx;
            m_iv[1].iov_base = m_file_address;
            m_iv[1].iov_len = m_file_stat.st_size;
            m_iv_count = 2;
            bytes_to_send = m_write_idx + m_file_stat.st_size;
            return true;
        }
        else
        {
            const char *ok_string = "<html><body></body></html>";
            add_headers(strlen(ok_string));
            if (!add_content(ok_string))
                return false;
        }
    }
    default:
        return false;
    }
    m_iv[0].iov_base = m_write_buf;
    m_iv[0].iov_len = m_write_idx;
    m_iv_count = 1;
    bytes_to_send = m_write_idx;
    return true;
}
void http_conn::process()
{
    HTTP_CODE read_ret = process_read();
    if (read_ret == NO_REQUEST)  //继续接收客户数据
    {
        modfd(m_epollfd, m_sockfd, EPOLLIN, m_TRIGMode); //重置EPOLLONESHOT
        return;
    }
    bool write_ret = process_write(read_ret);
    if (!write_ret)
    {
        close_conn();
    }
    modfd(m_epollfd, m_sockfd, EPOLLOUT, m_TRIGMode);
}
