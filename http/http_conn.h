#ifndef HTTPCONNECTION_H
#define HTTPCONNECTION_H
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
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <stdarg.h>
#include <errno.h>
#include <sys/wait.h>
#include <sys/uio.h>
#include <map>
#include <vector>
#include <dirent.h>
#include <iomanip>

#include <thread>
#include <mutex>
#include "../lock/locker.h"
#include "../CGImysql/sql_connection_pool.h"
#include "../timer/heap_timer.h"
// #include "../timer/lst_timer.h"
#include "../log/log.h"

#include <fstream>

class http_conn
{
public:
    static const int FILENAME_LEN = 200;
    static const int READ_BUFFER_SIZE = 4096;  //GET 2048足够一次读完
    static const int WRITE_BUFFER_SIZE = 1024;
    static const string FILE_LOAD_PASSWD;  //上传文件验证码
    enum METHOD
    {
        GET = 0,
        POST,
        HEAD,
        PUT,
        DELETE,
        TRACE,
        OPTIONS,
        CONNECT,
        PATH
    };
    // 主状态
    enum CHECK_STATE
    {
        CHECK_STATE_REQUESTLINE = 0,   //当前正在分析请求行
        CHECK_STATE_HEADER,     // 头部字段
        CHECK_STATE_CONTENT,     // 内容实体
        CHECK_STATE_FORMDATA     //  formdata内容实体
    };
    enum HTTP_CODE
    {
        NO_REQUEST,   //请求不完整需要继续读取客户数据
        GET_REQUEST,  //获得请求
        BAD_REQUEST,  // 请求语法错误
        NO_RESOURCE,
        FORBIDDEN_REQUEST,
        FILE_REQUEST, // do_request()函数后状态，利用向量写入请求文件内容
        INTERNAL_ERROR,
        CLOSED_CONNECTION
    };
    //从状态
    enum LINE_STATUS
    {
        LINE_OK = 0,   // 读取到一个完整的行
        LINE_BAD,       //  行出错
        LINE_OPEN       // 行数据不完整
    };

public:
    http_conn() {}
    ~http_conn() {}

public:
    void init(int sockfd, const sockaddr_in &addr, char *, char *, int, int, string user, string passwd, string sqlname);
    void close_conn(bool real_close = true);
    void process();
    bool read_once();
    bool write();
    //将上传文件写入硬盘
    bool add_file(const string &filename, const string &contents);
    // 文件上传验证
    // bool upload_check();

    sockaddr_in *get_address()  //日志模块调用
    {
        return &m_address;
    }
    void initmysql_result(connection_pool *connPool);
    int timer_flag;
    int improv; // reactor 标记


private:
    void init();
    void init1(); //处理formdata，清空当前读缓存区
    HTTP_CODE process_read();
    bool process_write(HTTP_CODE ret);
    HTTP_CODE parse_request_line(char *text);
    HTTP_CODE parse_headers(char *text);
    HTTP_CODE parse_content(char *text);
    HTTP_CODE parse_formdata();  //当前在接受formdata内容
    HTTP_CODE do_request();
    //m_start_line是已经解析的字符
    //get_line用于将指针向后偏移，指向未处理的字符
    char *get_line() { return m_read_buf + m_start_line; };
    LINE_STATUS parse_line();
    void unmap();
    bool add_response(const char *format, ...);
    bool add_content(const char *content);
    bool add_status_line(int status, const char *title);
    bool add_headers(int content_length);
    bool add_content_type();
    bool add_content_length(int content_length);
    bool add_linger();
    bool add_blank_line();

public:
    static int m_epollfd;
    static int m_user_count;
    MYSQL *mysql;
    int m_state;  //读为0, 写为1  Reactor工作模式

private:
    // 接受文件内容用string接收，存在被\0截断情况，用+替换\0    多余操作已去除
    // std::vector<int> m_changeids;  // \0替换为+的索引
    // int diff; //替换索引偏移量 用于还原原数据
    int m_sockfd;
    sockaddr_in m_address;
    int m_total_byte;  //接收内容总字节数
    //存储读取的请求报文数据
    char m_read_buf[READ_BUFFER_SIZE];
    //缓冲区获取到数据中最后一个字节的下一个位置
    int m_read_idx;
    //读缓冲区当前读取位置
    int m_checked_idx;
    //已经解析的字符个数
    int m_start_line;
    char m_write_buf[WRITE_BUFFER_SIZE];
    //写缓冲区长度
    int m_write_idx;
    //当前主状态机状态
    CHECK_STATE m_check_state;
    METHOD m_method;
    //存储读取文件的名称
    char m_real_file[FILENAME_LEN];


    //请求目标文件url 深拷贝
    char *m_url_t;
    char *m_url;  //深拷贝
    char *m_version;  // 深拷贝
    char *m_host;
    //内容实体部分长度， 包括上传文件部分 空行\r\n\r\n后的所有数据长度
    int m_content_length;
    //是否长连接
    bool m_linger;
    // 读取文件的地址
    char *m_file_address;
    struct stat m_file_stat;
    // io向量机制iovec 0为http响应报文， 1为请求文件内容使用内存映射
    struct iovec m_iv[2];  
    // io向量块数，应用于聚集写 writev
    int m_iv_count;
    int cgi;        //是否为上传文件请求标记
    string m_string; //存储post表单数据  这里为登陆或注册的账号和密码
    int bytes_to_send;  // 剩余发送字节数
    int bytes_have_send;  // 已发送字节数
    char *doc_root;  //root资源绝对路径
    char *file_root; //存放上传文件绝对路径

    string m_file_name; //上传文件名称
    string file_content;
    string m_theme;     //对应网站主题
    string m_passwd;   // 文件上传密码
    string m_boundary;  // formdata 边界

    // map<string, string> m_users;  //没用到
    int m_TRIGMode;  // LT ET选择
    int m_close_log;

    char sql_user[100];
    char sql_passwd[100];
    char sql_name[100];

// 上传文件后自动创建修改相应前端界面
public:
    int get_pic_num();   //获取当前服务器中存储图片数量，用于更新标题
    void change_html();  //修改html文件  1、更新目录页  2、添加新图片对应界面

private: 
    int m_pic_num;  //图片总数  统一图片名称为 Pn.png ...
};

#endif
