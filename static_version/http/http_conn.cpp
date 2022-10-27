#include "http_conn.h"

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

const string http_conn::FILE_LOAD_PASSWD = "6666";

mutex m_lock;  // 原版本用于保证mysql操作线程安全(多余)，现用于保证多用户同时上传文件可能产生冲突问题
static map<string, string> users;

void http_conn::change_html() {
    //  修改picture.html 更改标题 路径
    //  新增Pn.html
    string text1("<li><a class='smoothScroll' href='/");
    string html_end(".html");
    string text2("'><font size='6'>");
    string text3("</font><br/></a></li>\n");

    string s_file_root = doc_root;
    s_file_root.push_back('/');
    string write_file1 = s_file_root + "picture.html"; // 更新文件路径
    string file_end = m_file_name.substr(m_file_name.find_last_of('.'));//获取. + 文件后缀
    for (int i = 1; i < file_end.size(); ++i) { //统一小写格式
        file_end[i] = tolower(file_end[i]);
    }
    string new_file_name = "P" + to_string(m_pic_num) + file_end; //新图片名称  Pn.png
    string write_file2 = s_file_root + "P" + to_string(m_pic_num) + html_end; //Pn.html
    string template_file1 = s_file_root + "template2.html";  //图片文件
    string template_file2 = s_file_root + "template3.html";  //视频文件

    //更新picture.html
    ifstream ifs1(write_file1.c_str());
	string content1( (istreambuf_iterator<char>(ifs1) ),
					 (istreambuf_iterator<char>() ) );
	ifs1.close();
    int location = content1.find("mytag1");
    string final;
    string theme_auto_pic = "无标题图片";
    string theme_auto_mv = "无标题视频";
    string theme_auto = file_end == ".mp4" ? theme_auto_mv + to_string(m_pic_num) : theme_auto_pic + to_string(m_pic_num);
    if (m_theme == "") {
        final = text1 + "P" + to_string(m_pic_num) + html_end + text2 + theme_auto + text3;
    }
    else {
        final = text1 + "P" + to_string(m_pic_num) + html_end + text2 + m_theme + text3;
    }
    content1.insert(location - 1, final);
    ofstream out1(write_file1.c_str(), std::ios::out);
    out1.write(content1.c_str(), content1.size()); // 最终更新 picture.html
    out1.close();   

    //新增Pn.html
    ifstream ifs2;
    if (file_end == ".mp4") { //视频情况
        ifs2.open(template_file2.c_str());
    }
    else {
        ifs2.open(template_file1.c_str());  // 图片情况
    }
	string content2( (istreambuf_iterator<char>(ifs2) ),
					 (istreambuf_iterator<char>() ) );
	ifs2.close();
    location = content2.find("theme");
    if (m_theme == "") content2.insert(location + 7, theme_auto);
    else content2.insert(location + 7, m_theme);
    location = content2.find("images/picture/");
    content2.insert(location + 15, new_file_name);  
    ofstream out2(write_file2.c_str(), std::ios::out);
    out2.write(content2.c_str(), content2.size()); // 最终更新 Pn.html
    out2.close();

}

int http_conn::get_pic_num() {
	int filenum = 0;
	DIR* dir;
	dir = opendir(file_root);
	dirent* ptr;
	while ((ptr = readdir(dir)) != NULL){
		if (ptr->d_name[0] == '.') continue;
		filenum++;
	}
	closedir(dir);
    return filenum;
}

bool http_conn::add_file(const string& filename, const string& contents) {
    ++m_pic_num;
    string file_end = m_file_name.substr(m_file_name.find_last_of('.'));//获取. + 文件后缀
    for (int i = 1; i < file_end.size(); ++i) { //统一小写格式
        file_end[i] = tolower(file_end[i]);
    }
    string new_file_name = "P" + to_string(m_pic_num) + file_end; //新图片名称  Pn.png
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
    if (mysql_query(mysql, "SELECT username,password FROM user"))
    {
        LOG_ERROR("SELECT error:%s\n", mysql_error(mysql));
    }

    //从表中检索完整的结果集
    MYSQL_RES *result = mysql_store_result(mysql);

    //返回结果集中的列数
    int num_fields = mysql_num_fields(result);

    //返回所有字段结构的数组
    MYSQL_FIELD *fields = mysql_fetch_fields(result);

    //从结果集中获取下一行，将对应的用户名和密码，存入map中
    while (MYSQL_ROW row = mysql_fetch_row(result))
    {
        string temp1(row[0]);
        string temp2(row[1]);
        users[temp1] = temp2;
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
    m_boundary = "";
    m_string = "";
    m_passwd = "";

    memset(m_read_buf, '\0', READ_BUFFER_SIZE);
    memset(m_write_buf, '\0', WRITE_BUFFER_SIZE);
    memset(m_real_file, '\0', FILENAME_LEN);

}

//继续接收文件剩余内容
void http_conn::init1()
{

    // m_start_line = 0;
    // m_checked_idx = 0;
    m_read_idx = 0;
    // m_write_idx = 0;

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
//非阻塞ET工作模式下，需要一次性将数据读完
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
        m_url_t = strchr(m_url_t, '/');
    }

    if (strncasecmp(m_url_t, "https://", 8) == 0)
    {
        m_url_t += 8;
        m_url_t = strchr(m_url_t, '/');
    }
    if (strncasecmp(m_url_t, "/fileVerify.action", 18) == 0)
    {
        cgi = 1; //上传文件密码验证
    }
    if (strncasecmp(m_url_t, "/fileUpload.action", 18) == 0)
    {
        cgi = 2; //上传文件标记
    }
    // 添加非法url判断
    // 1、长度大于200(根据实际情况)  2、存在连续/  3、长度大于1末尾为/，末尾去除
    string url_judge = m_url_t;
    if (strlen(m_url_t) > 200) return BAD_REQUEST; // 情况1
    if (url_judge.find("//") != -1) return BAD_REQUEST; // 情况2；
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
            if (cgi) {
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
        //POST请求中最后为输入的用户名和密码
        m_string = text;
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
            strcpy(sql_insert, "INSERT INTO user(username, password) VALUES(");
            strcat(sql_insert, "'");
            strcat(sql_insert, name);
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
            if (users.find(name) != users.end() && users[name] == password)
                strcpy(m_url, "/picture.html");
            else
                strcpy(m_url, "/logError.html");
        }
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
        //验证
        if (m_passwd == FILE_LOAD_PASSWD) {
            strcpy(m_url, "/fileload.html");
        }
        else {
            strcpy(m_url, "/fileloadverifyfail.html");
        }
    }

    //处理文件上传结果
    if (cgi == 2)
    {
        // 获取文件名称
        size_t id1 = file_content.find("filename");
        id1 += 10; //文件名第一个字符
        while (file_content[id1] != '"') {
            m_file_name.push_back(file_content[id1++]);
        }
        if (m_file_name.empty()) {  //处理空提交情况
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
            m_pic_num = get_pic_num();
            if (add_file(m_file_name, file_content)) {
                //将标题记录进history.txt
                string add_theme = m_theme;
                add_theme.push_back('\n'); //添加换行符
                ofstream history_out(history_root.c_str(), std::ios::app); //追加写入
                history_out.write(add_theme.c_str(), add_theme.size());
                history_out.close();
    
                change_html();
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
    strncpy(m_real_file + len, m_url, FILENAME_LEN - len - 1);

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

    int fd = open(m_real_file, O_RDONLY);
    m_file_address = (char *)mmap(0, m_file_stat.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    return FILE_REQUEST;
}

void http_conn::unmap()
{
    if (m_file_address)
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
    return add_content_length(content_len) && add_linger() &&
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
