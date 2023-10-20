#include"http_conn.h"

int http_conn:: m_epollfd=-1;//所有的socket上的事件都被注册到同一个epoll对象上
int http_conn:: m_user_count=0;//统计用户的数量

//设置文件描述符非阻塞
void setnonblocking(int fd){
    int old_flag=fcntl(fd,F_GETFL);
    int new_flag= old_flag|O_NONBLOCK;
    fcntl(fd,F_SETFL,new_flag);
}

//添加文件描述符到epoll中
void addfd(int epollfd,int fd,bool one_shot){
    epoll_event event;
    event.data.fd=fd;
    //event.events=EPOLLIN|EPOLLRDHUP;
    event.events=EPOLLIN|EPOLLET|EPOLLRDHUP;


    if(one_shot){
        event.events|EPOLLONESHOT;
    }
    epoll_ctl(epollfd,EPOLL_CTL_ADD,fd,&event);//追加到epoll中

    //设置文件描述符非阻塞
    setnonblocking(fd);

}

//从epoll中删除文件描述符
void removefd(int epollfd,int fd){
    epoll_ctl(epollfd,EPOLL_CTL_DEL,fd,0);
    close(fd);

}

//修改文件描述符,重置socket上EPOLLONESHOT事件，
//以确保下一次可读时，EPOLLIN事件能被触发
void modfd(int epollfd,int fd,int ev){
    epoll_event event;
    event.data.fd=fd;
    event.events=ev|EPOLLONESHOT|EPOLLRDHUP;
    epoll_ctl(epollfd,EPOLL_CTL_MOD,fd,&event);

}

//初始化连接
void http_conn::init(int sockfd,const sockaddr_in &addr){
    m_sockfd=sockfd;
    m_address=addr;

    //设置端口复用
    int reuse=1;
    setsockopt(m_sockfd,SOL_SOCKET,SO_REUSEADDR,&reuse,sizeof(reuse));

    //添加到epoll对象中
    addfd(m_epollfd,m_sockfd,true);
    m_user_count++;//总用户数加一

    init();

}

void http_conn::init(){//初始化其他的信息
    m_check_state=CHECK_STATE_REQUESTLINE;//初始化状态为解析请求首行
    m_checked_index=0;
    m_start_line=0;
    m_read_idx=0;

    m_method=GET;
    m_url=0;
    m_version=0;
    m_linger=false;

    bzero(m_read_buf,READ_BUFFER_SIZE);
}




//关闭连接
void http_conn::close_conn(){
    
    if(m_sockfd!=-1){
        removefd(m_epollfd,m_sockfd);
        m_sockfd=-1;
        m_user_count--;//总用户数减少
    }

}

//循环读取客户数据，直到无数据可读或者对方关闭连接
bool http_conn::read(){
    if(m_read_idx>=READ_BUFFER_SIZE){//缓冲区已经满了
        return false;
    }
    //读取到的字节
    int bytes_read=0;
    while(1){
        bytes_read=recv(m_sockfd,m_read_buf+m_read_idx,READ_BUFFER_SIZE-m_read_idx,0);
        if(bytes_read==-1){//错误
            if(errno==EAGAIN||errno==EWOULDBLOCK){
                //没有数据了
                break;
            }
            return false;
        }else if(bytes_read==0){
            //对方关闭连接
            return false;
        }
        //更新m_read_idx
        m_read_idx+=bytes_read;
    }
    printf("读取到了数据：%s\n",m_read_buf);
    return true;
}



    //主状态机,解析HTTP请求
    http_conn::HTTP_CODE http_conn::process_read(){

        LINE_STATUS line_status=LINE_OK;//从状态机
        HTTP_CODE ret=NO_REQUEST;

        char*text=0;
        while((m_check_state==CHECK_STATE_CONTENT)&&
        (line_status==LINE_OK)
        ||((line_status=parse_line())==LINE_OK)){
            //while中的条件:
            //解析到了一行完整的数据或者解析到了请求体，也是完整的数据
            
            //获取一行数据
            text=get_line();

            m_start_line=m_checked_index;
            printf("got 1 http line:%s\n",text);

            switch(m_check_state){
                case CHECK_STATE_REQUESTLINE:{
                    ret=parse_request_line(text);
                    if(ret==BAD_REQUEST){
                        return BAD_REQUEST;
                    }
                    break;
                }
                case CHECK_STATE_HEADER:{
                    ret=parse_headers(text);
                    if(ret==BAD_REQUEST){
                        return BAD_REQUEST;
                    }else if(ret==GET_REQUEST){
                        return do_request();//解析具体的内容
                    }
                }
                case CHECK_STATE_CONTENT:{
                    ret=parse_content(text);
                    if(ret==GET_REQUEST){
                        return do_request();//解析具体的内容
                    }
                    line_status=LINE_OPEN;//数据不完整

                }
                default:{
                    return INTERNAL_ERROR;

                }
            }
        }

        return NO_REQUEST;

    }

    //解析HTTP请求首行，获得请求方法，目标URL，HTTP版本
    http_conn::HTTP_CODE http_conn::parse_request_line(char *text){
        // GET /index.html HTTP/1.1
        m_url=strpbrk(text," \t");// 判断第二个参数中的字符哪个在text中最先出现
                                  // 并返回位置
        *m_url++='\0';//添加字符串结束符,添加之后请求行变成下面
        // GET\0/index.html HTTP/1.1
        // 置位空字符，字符串结束符

        char* method=text;//得到GET
        if(strcasecmp(method,"GET")==0){
            /*strcasecmp用忽略大小写比较字符串.，
            通过strncasecmp函数可以指定每个字符串用于比较的字符数，
            strcasecmp用来比较参数s1和s2字符串前n个字符，
            比较时会自动忽略大小写的差异。strcasecmp函数是二进制且对大小写不敏感。此函数只在Linux中提供，
            相当于windows平台的 stricmp。
            */
            m_method=GET;
        }else{
            return BAD_REQUEST;
        }

        // /index.html HTTP/1.1
        m_version=strpbrk(m_url," \t");
        if(!m_version){//如果没有值
            return BAD_REQUEST;
        }
        *m_version++='\0';

        // /index.html\0HTTP/1.1
        if(strcasecmp(m_version,"HTTP/1.1")!=0){
            return BAD_REQUEST;
        }

        //http://192.168.110.129:10000/index.html
        if(strncasecmp(m_url,"http://",7)==0){//比较前七个字符
            m_url+=7;
            m_url=strchr(m_url,'/');//m_url将会变成/index.html
            //char *strchr(const char *s, int c);
            //它表示在字符串 s 中查找字符 c，
            //返回字符 c 第一次在字符串 s 中出现的位置，
            //如果未找到字符 c，则返回 NULL。
        }

        if(!m_url||m_url[0]!='/'){
            return BAD_REQUEST;
        }

        m_check_state=CHECK_STATE_HEADER;//主状态机检查状态变成检查请求头
        
        return NO_REQUEST;
    }



    // 解析HTTP请求的一个头部信息
    http_conn::HTTP_CODE http_conn::parse_headers(char *text){
            // 遇到空行，表示头部字段解析完毕
        if( text[0] == '\0' ) {
            // 如果HTTP请求有消息体，则还需要读取m_content_length字节的消息体，
            // 状态机转移到CHECK_STATE_CONTENT状态
            if ( m_content_length != 0 ) {//在http报文中
                m_check_state = CHECK_STATE_CONTENT;
                return NO_REQUEST;
            }
            // 否则说明我们已经得到了一个完整的HTTP请求
            return GET_REQUEST;
        } else if ( strncasecmp( text, "Connection:", 11 ) == 0 ) {
            // 处理Connection 头部字段  Connection: keep-alive
            text += 11;
            text += strspn( text, " \t" );
            if ( strcasecmp( text, "keep-alive" ) == 0 ) {
                m_linger = true;
            }
        } else if ( strncasecmp( text, "Content-Length:", 15 ) == 0 ) {
            // 处理Content-Length头部字段
            text += 15;
            text += strspn( text, " \t" );
            m_content_length = atol(text);
        } else if ( strncasecmp( text, "Host:", 5 ) == 0 ) {
            // 处理Host头部字段
            text += 5;
            text += strspn( text, " \t" );
            m_host = text;
        } else {
            printf( "oop! unknow header %s\n", text );
        }

        return NO_REQUEST;
    }

    //解析HTTP请求体( 我们没有真正解析HTTP请求的消息体，只是判断它是否被完整的读入了)
    http_conn::HTTP_CODE http_conn::parse_content(char *text){
        if ( m_read_idx >= ( m_content_length + m_checked_index ) )
        {
            text[ m_content_length ] = '\0';
            return GET_REQUEST;
        }
            return NO_REQUEST;

    }

    //解析一行，判断依据\r\n
    http_conn::LINE_STATUS http_conn::parse_line(){
        char temp;
        for(;m_checked_index<m_read_idx;++m_checked_index){
            temp=m_read_buf[m_checked_index];
            if(temp=='\r'){
                if((m_checked_index+1)==m_read_idx){
                    return LINE_OPEN;
                }else if(m_read_buf[m_checked_index+1]=='\n'){
                    m_read_buf[m_checked_index++]='\0';
                    m_read_buf[m_checked_index++]='\0';
                    //在一行末尾修改出文件结束符，并移动m_checked_index的位置
                    return LINE_OK;
                }
                return LINE_BAD;
            }else if(temp=='\n'){
                if((m_checked_index>1)&&(m_read_buf[m_checked_index-1]=='\r')){
                    m_read_buf[m_checked_index-1]='\0';
                    m_read_buf[m_checked_index++]='\0';
                    return LINE_OK;
                }
                return LINE_BAD;
            }
            return LINE_OPEN;
        }
        return LINE_OK;
    }

    http_conn::HTTP_CODE http_conn::do_request(){
    
    
    }


//写数据，非阻塞写
bool http_conn::write(){
    printf("一次性写完\n");
    return true;
}

//由线程池中的工作线程调用,处理HTTP请求的入口函数
void http_conn::process(){
    //解析HTTP请求
    HTTP_CODE read_ret=process_read();
    if(read_ret==NO_REQUEST){//请求不完整
        modfd(m_epollfd,m_sockfd,EPOLLIN);
        return;
    }


    //生成响应

}