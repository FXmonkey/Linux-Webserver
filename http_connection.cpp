#include"http_connection.h"

int http_connection::epollFd = -1;      //默认值
int http_connection::cilentNum = 0;


// 定义HTTP响应的一些状态信息
const char* ok_200_title = "OK";
const char* error_400_title = "Bad Request";
const char* error_400_form = "Your request has bad syntax or is inherently impossible to satisfy.\n";
const char* error_403_title = "Forbidden";
const char* error_403_form = "You do not have permission to get file from this server.\n";
const char* error_404_title = "Not Found";
const char* error_404_form = "The requested file was not found on this server.\n";
const char* error_500_title = "Internal Error";
const char* error_500_form = "There was an unusual problem serving the requested file.\n";



const char* doc_root = "/home/zhou/Linux/pthread/pthread-pool/resources";

int setnonblocking(int fd){                        //设置文件描述符为非阻塞
    int flag = fcntl(fd,F_GETFL);
    fcntl(fd,F_SETFL,flag|O_NONBLOCK);
    return flag;
}

void addFd(int epollFd, int listenFd, bool one_shot){       //向EPOLL中添加需要响应的文件描述符

    epoll_event  event;
    event.data.fd =   listenFd;
    event.events = EPOLLIN | EPOLLRDHUP/*(断开可能出现的挂起) */;
    
    if(one_shot){   //socket连接在任意时刻都只被一个线程处理
                        //EPOLLONESHOT事件,注册了该事件的fd，操作系统最多触发其注册的一个读或写或-
                        //-异常事件，故要通过epoll_ctl重置才能确保socket下一次可读。 
        event.events |= EPOLLONESHOT;
    }

    epoll_ctl(epollFd,EPOLL_CTL_ADD,listenFd,&event);
    //设置文件描述符为非阻塞（ET会循环读完，若是阻塞，没有数据就会一直阻塞）
    setnonblocking(listenFd);
}

void deleteFd(int epollFd, int listenFd){           //向EPOLL中删除需要响应的文件描述符
    epoll_ctl(epollFd,EPOLL_CTL_DEL,listenFd,0);
    close(listenFd);
}   


void updataFd(int epollFd, int listenFd, int ev){   //向EPOLL中修改需要响应的文件描述符
    epoll_event event ;                                                 
    event.data.fd = listenFd;
    event.events = ev | EPOLLONESHOT | EPOLLRDHUP; //重置socket上EPOLLONETSHOT事件，确保下次可触发socket
    epoll_ctl(epollFd,EPOLL_CTL_MOD,listenFd,&event);
}   


void http_connection::init(int sockfd, const sockaddr_in & addr){//新的客户端数据初始化
    sockfd_http = sockfd;
    address = addr;
    http_connection::cilentNum++;

    int reuser = 1;         //端口复用
    setsockopt(sockfd_http,SOL_SOCKET,SO_REUSEPORT,&reuser,sizeof(reuser));

    //添加到epoll对象中
    addFd(epollFd,sockfd,true);
    cilentNum++;

    init_parse();
}


void http_connection::init_parse(){
    check_state = CHECK_STATE_REQUEST;
    checked_index = 0;
    statr_line = 0;
    read_index = 0;


    url = 0;
    get_method = GET;
    version = 0;            //初始化请求行三个信息
    link_keep = false;
    host = 0;
    content_length = 0;
    write_index = 0;

    bzero(READ_BUF,READ_BUF_SIZE);  //清空读缓存
    bzero(WRITE_BUF, WRITE_BUF_SIZE);
    bzero(real_file, FILENAME_LENGTH);
}


void http_connection::close_connection(){
    if(sockfd_http!=-1){
        deleteFd(epollFd,sockfd_http);
        sockfd_http = -1;
        cilentNum--;
    }
}

bool http_connection::readOneAll(){
    if(read_index >= READ_BUF_SIZE){
        return false;
    }
    int hadRead = 0;
    while(1){
        hadRead = recv(sockfd_http,READ_BUF + read_index,READ_BUF_SIZE,0);
        if(hadRead == -1){  
            if(errno == EAGAIN || EWOULDBLOCK){
                break;
            }
        return false;
        }else if(hadRead == 0){
            printf("断开连接\n");
            return false;
        }
        printf("读取到%s\n",READ_BUF);
        read_index += hadRead;
    }
    printf("readOneAll\n");
    return true;
}

bool http_connection::writeOneAll(){
    int temp = 0;
    int bytes_have_send = 0;    // 已经发送的字节
    int bytes_to_send = write_index;// 将要发送的字节写缓冲区中待发送的字节数
    
    if ( bytes_to_send == 0 ) {
        // 将要发送的字节为0，这一次响应结束。
        updataFd( epollFd, sockfd_http, EPOLLIN ); 
        init_parse();
        return true;
    }

    while(1) {
        // 分散写
        temp = writev(sockfd_http, m_iv, m_iv_count);
        if ( temp <= -1 ) {
            // 如果TCP写缓冲没有空间，则等待下一轮EPOLLOUT事件，虽然在此期间，
            // 服务器无法立即接收到同一客户的下一个请求，但可以保证连接的完整性。
            if( errno == EAGAIN ) {
                updataFd( epollFd, sockfd_http, EPOLLOUT );
                return true;
            }
            unmmap();
            return false;
        }
        bytes_to_send -= temp;
        bytes_have_send += temp;
        if ( bytes_to_send <= bytes_have_send ) {
            // 发送HTTP响应成功，根据HTTP请求中的Connection字段决定是否立即关闭连接
            unmmap();
            if(link_keep) {
                init_parse();
                updataFd( epollFd, sockfd_http, EPOLLIN );
                return true;
            } else {
                updataFd( epollFd, sockfd_http, EPOLLIN );
                return false;
            } 
        }
    }
}



http_connection::HTTP_CODE http_connection::process_read(){ //根据CHECK_STATE决定调用哪个请求处理
    LINE_STATUS line_statue = LINE_OK;
    HTTP_CODE ret = NO_REQUEST;

    char *text = 0;                 //保存获取的一行数据
    while ((line_statue = parse_line()) == LINE_OK || (line_statue == LINE_OK && check_state==CHECK_STATE_CONTENT))
    {   //两种情况：    1.当前还在解析非请求体内容，需要一行一行获取
        //             2.当前在解析请求体，不用一行一行获取
        //获取一行数据  start_line + buf  只用到两个变量，可用内联函数 
        text = get_line();
        statr_line = checked_index;

        switch (check_state)
        {
            case CHECK_STATE_REQUEST:
                {
                    ret = parse_request_line(text);
                    if(ret == BAD_REQUEST){
                        return BAD_REQUEST;
                    }
                    break;
                }

            case CHECK_STATE_HEADER:
                {
                    ret = parse_headers(text);
                    if(ret == BAD_REQUEST){
                        return BAD_REQUEST;
                    }else if(ret == GET_REQUEST){
                        return do_request();            //do_request();   解析具体的内容、信息
                    }
                    //不满足判断条件表示是请求体
                }
                
            case CHECK_STATE_CONTENT:
                {
                    ret = parse_cotent(text);
                    if(ret == GET_REQUEST){
                        return do_request();
                    }
                    line_statue = LINE_OPEN;
                    break;
                }
                
            default:
                {
                    return INTERNAL_ERROR;      //内部错误
                }
        }
    }
    return NO_REQUEST;          //若switch没有响应，表示请求不完整要继续获取
}


http_connection::HTTP_CODE http_connection::parse_request_line(char *text){
    //获得请求方法，目标URL，HTTP版本
    //将这三个信息封装在任务类中

    /*         GET / HTTP/index.html          */
    url = strpbrk(text," \t");  //源字符串中按顺序找出最先含有搜索字符串中任一字符的位置并返回

    /*         GET\0/ HTTP/index.html          */
    *url++ = '\0';     //结束标志
    char *method = text;
    if(strcasecmp(method,"GET") == 0){         //strcasecmp忽略大小写比较
        get_method = GET;
    }else{
        return BAD_REQUEST;
    }

    version = strpbrk(url," \t");
    if(!version){
        return BAD_REQUEST;
    }
    *version++ = '\0';
    if(strcasecmp(version,"HTTP/1.1") != 0){
        return BAD_REQUEST;
    }


    if(strncasecmp(url,"http://",7) == 0){       //比较字符串前7个字符
        url +=7;
        url = strchr(url,'/');
    }

    if(!url || url[0] != '/'){
        return BAD_REQUEST;
    }
    check_state = CHECK_STATE_HEADER;           //解析请求头

    return NO_REQUEST; 
}


http_connection::HTTP_CODE http_connection::parse_headers(char *text){

    if(text[0] == '\0'){            //消息体和请求头之间有空行，判断是否解析完
        if(content_length != 0){    //如果解析完请求头且消息体长度不为0，则状态机状态转为消息体
            check_state = CHECK_STATE_CONTENT;
            return NO_REQUEST;
        }
        return GET_REQUEST;
    }else if(strncasecmp(text,"Connection:",11) == 0){
        text += 11;
        text += strspn(text," \t");         //Connection：和后面的数据有空格，strspn检查text连续前几个字符在“ \t中出现的次数”
        
        if(strcasecmp(text,"link_keep") == 0){
            link_keep = true;
        }
    }else if(strncasecmp(text,"Content-length:",15) == 0){
        text += 15;
        text += strspn(text," \t");
        content_length = atol(text);
    }else if(strncasecmp(text,"host:",5) == 0){
        text += 5;
        text += strspn(text," \t");
        host = text;
    }else{
        printf("oop! unknow head %s\n",text);
    }
    return NO_REQUEST;
}


http_connection::HTTP_CODE http_connection::parse_cotent(char *text){   //判断数据是否被完整读入
    if(read_index >= (checked_index + content_length)){
        text[content_length] = '\0';
        return GET_REQUEST;
    }
    return NO_REQUEST;
}


http_connection::LINE_STATUS http_connection::parse_line(){     //解析行是否完整并取出
    char temp;
    for(;checked_index<read_index;checked_index++){
        temp = READ_BUF[checked_index];
        if(temp == '\r'){
            if((checked_index + 1)==read_index){
                return LINE_OPEN;
            }else if(READ_BUF[checked_index + 1]=='\n'){
                READ_BUF[checked_index++] = '\0';
                READ_BUF[checked_index++] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        }else if(temp == '\n'){
            if(checked_index>1 && (READ_BUF[checked_index - 1] == '\r')){
                READ_BUF[checked_index--] = '\0';
                READ_BUF[checked_index++] = '\0';   //check_index要指向行尾，故需要check_index++
            return LINE_OK;
            }
        return LINE_BAD;
        }
    }
    return LINE_OPEN;
}



http_connection::HTTP_CODE http_connection::do_request(){       //作具体处理
    //拼接本地文件地址和url
    strcpy(real_file,doc_root);
    int len = strlen(doc_root);
    strncpy(real_file + len, url, FILENAME_LENGTH - len - 1);
    
    //获取文件的相关状态,存入real_file_stat结构体
    if(stat(real_file,&real_file_stat) == -1){      
        return NO_RESOURCE;
    }

    //判断访问权限
    if(!(real_file_stat.st_mode & S_IROTH)){
        return FORBIDDEN_REQUEST;
    }

    //判断是否为目录
    if(S_ISDIR(real_file_stat.st_mode)){
        return BAD_REQUEST;
    }

    //以只读的方式打开文件
    int fd = open(real_file, O_RDONLY);

    //创建内存映射,需要释放
    file_address = (char *)mmap(0,real_file_stat.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    return FILE_REQUEST;
}

void http_connection::unmmap(){     //对内存映射去munmap()
    if(file_address){
        munmap(file_address, real_file_stat.st_size);
        file_address = 0;
    }
}

/*************************************************************************************/
/****************write面向的是连续内存块，writev面向的是分散的数据块**********************/
/****************writev的固有开销比write大,数据量大时使用writev**************************/
/**************************************************************************************/
//消息体被映射到内存中，而响应行和响应头在缓冲区中，两者地址不同，又数据量大，需要分散写
//基址结构体数组为2


//向缓冲区写入待写出的数据
bool http_connection::add_response( const char* format, ... ) {
    if( write_index >= WRITE_BUF_SIZE ) {
        return false;
    }
    
    va_list arg_list;       //解析参数
    va_start( arg_list, format );

    int len = vsnprintf( WRITE_BUF + write_index, WRITE_BUF_SIZE - 1 - write_index, format, arg_list );
    if( len >= ( WRITE_BUF_SIZE - 1 - write_index ) ) {
        return false;
    }
    write_index += len;
    va_end( arg_list );
    return true;
}



bool http_connection::add_status_line(int status, const char *title){
    return add_response( "%s %d %s\r\n", "HTTP/1.1", status, title );
}
bool http_connection::add_headers(int content_len) {
    add_content_length(content_len);
    add_content_type();     //请求文件类型
    add_linger();           //连接保持
    add_blank_line();       //加空行
}

bool http_connection::add_content( const char* content )
{
    return add_response( "%s", content );
}

bool http_connection::add_content_length(int content_len) {
    return add_response( "Content-Length: %d\r\n", content_len );
}
bool http_connection::add_linger()
{
    return add_response( "Connection: %s\r\n", ( link_keep == true ) ? "keep-alive" : "close" );
}
bool http_connection::add_blank_line()
{
    return add_response( "%s", "\r\n" );
}
bool http_connection::add_content_type(){
    return add_response("Content-Type:%s\r\n", "text/html");
}

bool http_connection::process_write(HTTP_CODE read_ret){
    switch (read_ret)
    {
    case INTERNAL_ERROR:
        add_status_line( 500, error_500_title );
        add_headers( strlen( error_500_form ) );
        if ( ! add_content( error_500_form ) ) {
            return false;
           }
        break;

    case NO_RESOURCE:
        add_status_line( 404, error_404_title );
        add_headers( strlen( error_404_form ) );
        if ( ! add_content( error_404_form ) ) {
            return false;
        }
        break;

    case FORBIDDEN_REQUEST:
        add_status_line( 403, error_403_title );
        add_headers(strlen( error_403_form));
        if ( ! add_content( error_403_form ) ) {
            return false;
        }
        break;
    
    case BAD_REQUEST:
        add_status_line( 400, error_400_title );
        add_headers( strlen( error_400_form ) );
        if ( ! add_content( error_400_form ) ) {
            return false;
        }
        break;
    
    case FILE_REQUEST:
        add_status_line(200, ok_200_title );
        add_headers(real_file_stat.st_size);       //分散写
        m_iv[ 0 ].iov_base = WRITE_BUF;
        m_iv[ 0 ].iov_len = write_index;
        m_iv[ 1 ].iov_base = file_address;
        m_iv[ 1 ].iov_len = real_file_stat.st_size;
        m_iv_count = 2;
        return true;
    
    default:
        return false;
    }
    m_iv[ 0 ].iov_base = WRITE_BUF;
    m_iv[ 0 ].iov_len = write_index;
    m_iv_count = 1;
    return true;
}


//有线程池工作线程调用，这里是HTTP请求处理入口
void http_connection::process(){        //解析http请求->用到有限状态机技术:数据报，
                                          //首行->请求头->请求数据三种状态转变
    // 解析HTTP请求
    HTTP_CODE read_ret = process_read();
    if(read_ret == NO_REQUEST){
        //前面执行完毕后判断是否请求完整，若不完整，则继续监听读事件，回到main函数中继续执行
        updataFd(epollFd,sockfd_http,EPOLLIN);      //继续监听是否可读
        return;
    }

    //生成响应
    bool write_ret = process_write(read_ret);
    if(!write_ret){
        close_connection();
    }

    updataFd(epollFd,sockfd_http,EPOLLOUT);     //继续监听是否可写,用了ONESHOT事件，所以每次都要重新添加事件
}