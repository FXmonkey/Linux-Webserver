#include"pool.h"
#include"http_connection.h"

#define Max_FD 65536        //运行最多的客户端连接数量
#define MAX_EVENTS  10000   //IO多路复用一次可以监听最大事件数

//信号捕捉
void addsig(int sig, void(handler)(int)){
    struct sigaction sa;
    memset(&sa,'\0',sizeof(sa));
    sa.sa_handler = handler;
    sigfillset(&sa.sa_mask);        //把信号加入信号集sa_mask（临时阻塞信号集）
    sigaction(sig,&sa,NULL);       
}


//添加文件描述符到epoll对象中
extern void addFd(int epollFd, int listenFd, bool one_shot);

//删除epoll对象中文件描述符
extern void deleteFd(int epollFd, int listenFd);     //在http_connection.cpp中实现

 //修改需要响应的文件描述符
 extern void updataFd(int epollFd, int listenFd, int ev);


int main(int argc, char *argv[]){

    if(argc<=1){
        printf("运行格式:  %s port_number\n",basename(argv[0]));
        exit(-1);
    }
    

    //获取端口号argv[1]
    int port = atoi(argv[1]);

    //一段断开另一个继续写会产生信号SIGPIPE(默认下会终止进程)，故需要处理（添加函数作信号捕捉）
    //捕捉：信号需要注册函数，在捕捉到信号后会执行注册的回调函数
    addsig(SIGPIPE,SIG_IGN);


    //线程池程序已打开就要启动,创建线程池并初始化
    pool<http_connection> *threadPool = NULL;   //连接的任务
    try{        //异常捕捉
        threadPool = new pool<http_connection>;     //可能抛出异常的语句
    }catch(...){        //指明了当前catch可以处理的异常类型
        exit(-1);
    }

    //创建数组用于保存客户端信息，可放在http_connection
    http_connection *users = new http_connection[Max_FD];  //允许最多的客户端数量Max_FD

    //网络连接
    int listenFd = socket(AF_INET,SOCK_STREAM,0);
    if(listenFd==-1){
        perror("socket");
        exit(-1);
    }
    
    //端口复用
    int flag_sockopt = 1;
    setsockopt(listenFd,SOL_SOCKET,SO_REUSEPORT,&flag_sockopt,sizeof(flag_sockopt));

    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port =htons(port);
    addr.sin_addr.s_addr = INADDR_ANY;  

    int ret_bind = bind(listenFd,(struct sockaddr *)&addr,sizeof(addr));
    if(ret_bind==-1){
        perror("bind");
        exit(-1);
    }

    int ret_listen = listen(listenFd,5);
    
    epoll_event events[MAX_EVENTS]; 
    int epoll_FD = epoll_create(1);
    if(epoll_FD == -1){
        perror("epoll_create");
        exit(-1);
    }   

    //将监听的文件描述符添加到epoll对象中
    addFd(epoll_FD,listenFd,false);         //监听不用EPOLLONESHOT事件
    http_connection::epollFd = epoll_FD;    
    

    while(1){           //主线程不断循环检测事件发生

        int num = epoll_wait(epoll_FD,events,MAX_EVENTS,-1);
        if((num == -1)&&(errno!=EINTR)){
            printf( "epoll failure\n" );
            break;
        }

        for(int i=0;i<num;i++){
            int sockfd = events[i].data.fd;
            if(sockfd==listenFd){
                struct sockaddr_in addr_client;
                socklen_t addr_client_len = sizeof(addr_client);
                int accept_fd = accept(listenFd,(struct sockaddr *)&addr_client,&addr_client_len);
                if(accept_fd<0){
                    printf("errno is:%d\n",errno);
                    continue;
                }
                
                if(http_connection::cilentNum>=Max_FD){
                    //客户端连接数满了
                    //向要连接的客户端发送服务器在忙
                    close(accept_fd);
                    continue;
                }
                //新的客户端数据初始化并放到客户端中
                users[accept_fd].init(accept_fd,addr_client);  //主线程只负责检测

            }else if(events[i].events & (EPOLLERR | EPOLLRDHUP | EPOLLHUP)){
                //对方异常断开或错误->需要关闭连接
                users[sockfd].close_connection(); 
            }else if(events[i].events & EPOLLIN){
                //一次性读完数据
                if(users[sockfd].readOneAll()){         //同步I/O模拟的Proactor模式，全部读出封装成对象
                    threadPool->addTask(users + sockfd);
                    printf("main readOneAll\n");
                }else{
                    users[sockfd].close_connection();
                }
            }else if(events[i].events & EPOLLOUT){
                if(!users[sockfd].writeOneAll()){
                    users[sockfd].close_connection();
                }
            }
        }

    }
    close(epoll_FD);
    close(listenFd);
    delete []users;
    delete threadPool;
    return 0;
}