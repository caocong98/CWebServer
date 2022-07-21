

基于C++轻量级Web服务器搭建自己的字画分享网站

参考项目：TinyWebServer(https://github.com/qinguoyi/TinyWebServer)
参考书籍：Linux高性能服务器编程，游双著.

修改实现：
1. 根据字画网站需求修改http相关解析逻辑
2. 根据需求添加相应前端界面
3. 添加了基于http formdata文件上传(字画图片及MP4视频文件)的后端解析功能，后端解析后自动生成相关前端界面供用户访问查看，达到网站的动态交互效果。
4. 将基于升序链表的定时器更新为基于小根堆的定时器，优化了定时器效率；

参考项目要点：
* 使用 **线程池 + 非阻塞socket + epoll(针对上传文件需求使用Proactor/Reactor + LT方案) + 事件处理(Reactor和Proactor均实现)** 的并发模型
* 使用**有限状态机**解析HTTP请求报文，支持解析**GET和POST**请求
* 访问服务器数据库实现web端用户**注册、登录**功能，可以请求服务器**图片和视频文件**
* 实现**同步/异步日志系统**，记录服务器运行状态
* 经Webbench压力测试可以实现**上万的并发连接**数据交换
===============

* [参考项目解读](https://mp.weixin.qq.com/mp/appmsgalbum?__biz=MzAxNzU2MzcwMw==&action=getalbum&album_id=1339230165934882817&scene=173&from_msgid=2649274431&from_itemidx=1&count=3&nolastread=1#wechat_redirect)


目录
-----

| [更新日志](#更新日志) | [快速运行](#快速运行) | [个性化运行](#个性化运行) |




更新日志
-------
- [x] 添加守护进程后台运行方式
- [x] 修改前端界面添加文件上传的前端界面
- [x] 针对字画网站需求实现后端parse_formdata解析文件上传http请求报文
- [x] 添加根据上传文件自动生成前端相应界面功能
- [x] 添加文件上传密码验证功能
- [x] 添加基于小根堆实现的定时器。
- 待添加   1、存储已上传文件标题名称，用于服务器重启恢复原有内容
- 待添加   2、将以pthread库实现的多线程更新为以C++11 thread库实现


快速运行
------------
* 服务器测试环境
	* Ubuntu版本16.04
	* MySQL版本5.7.29
* 浏览器测试环境
	* Windows、Linux均可
	* Chrome
	* 360极速浏览器
	* 其他浏览器暂无测试

* 测试前确认已安装MySQL数据库

    ```C++
    // 建立yourdb库
    create database yourdb;

    // 创建user表
    USE yourdb;
    CREATE TABLE user(
        username char(50) NULL,
        passwd char(50) NULL
    )ENGINE=InnoDB;

    // 添加数据
    INSERT INTO user(username, passwd) VALUES('name', 'passwd');
    ```

* 修改main.cpp中的数据库初始化信息

    ```C++
    //数据库登录名,密码,库名
    string user = "root";
    string passwd = "root";
    string databasename = "yourdb";
    ```

* build

    ```C++
    sh ./build.sh
    ```

* 启动server

    ```C++
    ./server
    ```

* 浏览器端(默认端口9006)

    ```C++
    ip:9006
    ```

个性化运行
------

```C++
./server [-p port] [-l LOGWrite] [-m TRIGMode] [-o OPT_LINGER] [-s sql_num] [-t thread_num] [-c close_log] [-a actor_model] [-d start_ground] 
```

温馨提示:以上参数不是非必须，不用全部使用，根据个人情况搭配选用即可.

* -p，自定义端口号
	* 默认9006
* -d，选择运行方式，默认前台运行
	* 0，前台运行
	* 1，后台运行
* -l，选择日志写入方式，默认同步写入
	* 0，同步写入
	* 1，异步写入
* -m，listenfd和connfd的模式组合，默认使用LT + LT(大文件上传情况下读缓存区一次性无法读完全部数据，因此connfd使用LT模式处理)
	* 0，表示使用LT + LT
	* 1，表示使用LT + ET
    * 2，表示使用ET + LT
    * 3，表示使用ET + ET
* -o，优雅关闭连接，默认不使用
	* 0，不使用
	* 1，使用
* -s，数据库连接数量
	* 默认为8
* -t，线程数量
	* 默认为8
* -c，关闭日志，默认打开
	* 0，打开日志
	* 1，关闭日志
* -a，选择反应堆模型，默认Proactor
	* 0，Proactor模型
	* 1，Reactor模型


测试示例命令与含义

```C++
./server -p 9007 -d 1 -l 1 -m 0 -o 1 -s 10 -t 10 -c 1 -a 1
```

- [x] 端口9006
- [x] 后台运行
- [x] 异步写入日志
- [x] 使用LT + LT组合
- [x] 使用优雅关闭连接
- [x] 数据库连接池内有8条连接
- [x] 线程池内有8条线程
- [x] 关闭日志
- [x] Reactor反应堆模型