#ifndef HTTP_CONNECTION_
#define HTTP_CONNECTION_
#include<sys/epoll.h>
#include<stdio.h>
#include<string.h>
#include<stdlib.h>
#include<arpa/inet.h>
#include<unistd.h>
#include<fcntl.h>
#include<signal.h>
#include<sys/stat.h>
#include<sys/mman.h>
#include<sys/uio.h>
#include<errno.h>
#include<stdarg.h>
#include"pthreadsynclass.h"

    class http_connection
    {    
    public:
        static int epollFd;             //epollFd共享，socket所有的事件都注册到同一个epoll对象中
        static int cilentNum;           //用户的数量

        static const int FILENAME_LENGTH = 200;
        static const int READ_BUF_SIZE = 2048;
        static const int WRITE_BUF_SIZE = 1024;

        http_connection(){}
        ~http_connection(){}

        void init(int sockfd, const sockaddr_in & addr);         //客户端数据初始化,引用访问私有成员
        
        void close_connection();

        void process();         //处理客户端请求

        bool readOneAll();

        bool writeOneAll();
        
        

        private:
            int sockfd_http;     //HTTP连接的socket
            sockaddr_in address;       //通信的socket地址

            char READ_BUF[READ_BUF_SIZE];
            int read_index = 0;
            int write_index = 0;
            char WRITE_BUF[WRITE_BUF_SIZE];
            

            //http请求方法，这里只支持GET
            enum HTTP_METHOD{GET=0,POST,HEAD,PUT,DELETE,TRACE,OPTION,CONNECT};

            /*主状态机状态
            CHECK_STATE_REQUESTLINE;  当前正在解析请求首行
            CHECK_STATE_HEADER;       当前正在解析请求头
            CHECK_STATE_CONTENT;      当前正在解析请求体*/
            enum CHECK_STATE{CHECK_STATE_REQUEST=0,CHECK_STATE_HEADER,CHECK_STATE_CONTENT};

            /*从状态机三种可能状态
            LINE_OK         读取到完整的行
            LINE_BAD        行错误
            LINE_OPEN       行数据不完整*/
            enum LINE_STATUS{LINE_OK=0,LINE_BAD,LINE_OPEN};

            /*服务器处理HTTP请求的可能结果
            NO_REQUEST          请求不完整，需要继续读取客户数据
            GET_REQUEST         获得完整的客户请求
            BAD_REQUEST         客户端请求语法错误
            NO_RESOURCE         服务器没有资源
            FORBIDDEN_REQUEST   客户端没有相关权限访问资源
            FILE_REQUEST        文件请求，获取文件成功
            INTERNAL_ERROR      服务器内部错误
            CLOSE_CONNECTION    客户端断开连接*/
            enum HTTP_CODE{GET_REQUEST,BAD_REQUEST,NO_REQUEST,NO_RESOURCE,FORBIDDEN_REQUEST,FILE_REQUEST,INTERNAL_ERROR,CLOSE_CONNECTION};


            HTTP_CODE process_read();           //主状态机，获取数据，决定具体交给那种解析
            
            HTTP_CODE parse_request_line(char *text);
            HTTP_CODE parse_headers(char *text);
            HTTP_CODE parse_cotent(char *text); 

            LINE_STATUS parse_line();   //具体某一行，根据\r\n，
            
            void init_parse();
            int checked_index;          //正在解析的字符在读缓冲区的位置
            int statr_line;             //正在解析的行的起始位置
            CHECK_STATE check_state;    //当前主状态机所处的状态

            char *version;
            char *url;
            HTTP_METHOD get_method;             //需要初始化

            char *host;
            bool link_keep;         //是否保持连接

            int content_length;     //消息体长度

            /*.......................*************不懂*************...............*/
            char *get_line(){ return statr_line + READ_BUF; };

            char real_file[FILENAME_LENGTH];    //客户请求的目标文件的完整路径，doc_root + url
            struct stat real_file_stat;         //文件状态，判断文件是否存在、是否为目录、是否可读，并获取文件大小等信息
            char *file_address;              //目标文件被mmap到内存的起始地址
            //相对于传统的write/read　IO系统调用, 把数据从磁盘拷贝至到内核缓冲区中(页缓冲)，再把数据
            //拷贝至用户进程中。两者相比，mmap会少一次拷贝数据，这样带来的性能提升是巨大的。
            


            bool process_write(HTTP_CODE read_ret);
            bool add_status_line(int status, const char *title);
            bool add_response( const char* format, ... );
            bool add_headers(int content_len);
            bool add_linger();
            bool add_blank_line();
            bool add_content_length(int content_len);
            bool add_content_type();

            bool add_content( const char* content );
            
            struct iovec m_iv[2];
            int m_iv_count;

            void unmmap();
            
            HTTP_CODE do_request();
        };
    
#endif