#include <sys/socket.h>//网路通信相关
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/epoll.h>
#include "locker.h"
#include "threadpool.h"
#include "http_conn.h"

#define MAX_FD 65535   // 最大的文件描述符个数
#define MAX_EVENT_NUMBER 10000  // 监听的最大的事件数量

// 添加文件描述符
extern void addfd( int epollfd, int fd, bool one_shot );
//删除文件描述符
extern void removefd( int epollfd, int fd );

//信号处理，添加信号捕捉
void addsig(int sig, void( handler )(int)){//handler作为函数指针传递进来
    
    struct sigaction sa;//信号处理结构体(sigaction函数的参数)
    //sa：注册信号的一个参数

    memset( &sa, '\0', sizeof( sa ) );//清空内存（同bzero）
    sa.sa_handler = handler;//处理函数

    sigfillset( &sa.sa_mask );//设置临时阻塞的信号集（sa.sa_mask）
    // sigfillset函数功能: 将信号集中的所有的标志位置为1(全部阻塞)


    //sigaction注册信号
    assert( sigaction( sig, &sa, NULL ) != -1 );
    
}

int main( int argc, char* argv[] ) {//参数是端口号
    
    if( argc <= 1 ) {
        printf( "usage: %s port_number\n", basename(argv[0]));
        return 1;
    }

    //获取端口号
    int port = atoi( argv[1] );//0是程序名(a.out可执行文件名)，1是端口号

    //对sigpie信号进行处理（处理一端断开连接，另外一段仍然写数据，处理异常）
    addsig( SIGPIPE, SIG_IGN );//收到sigpipe信号忽略（IGN）它


    //初始化线程池
    threadpool< http_conn >* pool = NULL;//模板类：任务：http_conn
    try {
        pool = new threadpool<http_conn>;//创建线程池
    } catch( ... ) {
        return 1;
    }

    //创建一个数组，用于保存所有的客户端信息，所有的信息都封装在http_conn
    http_conn* users = new http_conn[ MAX_FD ];

    //创建监听的套接字
    int listenfd = socket( PF_INET, SOCK_STREAM, 0 );

    int ret = 0;


    // 设置端口复用
    //这段代码用于设置套接字选项，具体的选项是SO_REUSEADDR。SO_REUSEADDR是一个Socket选项，它允许在同一端口上绑定多个套接字。当一个套接字关闭后，该端口可以立即被其他套接字绑定，而不必等待一段时间。这可以解决端口被占用的问题。
    int reuse = 1;//reuse表示复用
    setsockopt( listenfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof( reuse ) );
    
    //绑定，第二个参数为服务器端的地址
    struct sockaddr_in address;
    address.sin_addr.s_addr = INADDR_ANY;//表示使用任何可用的addr
    address.sin_family = AF_INET;
    address.sin_port = htons( port );//转化成网路字节序
    ret = bind( listenfd, ( struct sockaddr* )&address, sizeof( address ) );
    
    //监听
    ret = listen( listenfd, 5 );


    //epoll实现io多路复用
    
    // 创建epoll对象，和事件数组，添加(添加监听的文件描述符)

    epoll_event events[ MAX_EVENT_NUMBER ];//事件数组
    int epollfd = epoll_create( 5 );//参数只要不是0就行

    // 将监听的文件描述符添加到epoll对象中,listenfd不需要添加oneshort事件
    addfd( epollfd, listenfd, false );

    //任务类的m_epollfd等于epollfd
    http_conn::m_epollfd = epollfd;

    while(true) {
        
        int number = epoll_wait( epollfd, events, MAX_EVENT_NUMBER, -1 );
        //number记录检测到了几个事件

        if ( ( number < 0 ) && ( errno != EINTR ) ) {//返回错误码为EINTR时，表示函数被一个信号中断了
            printf( "epoll failure\n" );
            break;
        }

        //循环遍历事件数组
        for ( int i = 0; i < number; i++ ) {
            
            int sockfd = events[i].data.fd;
            
            if( sockfd == listenfd ) {//有客户端连接进来
                
                //建立连接
                struct sockaddr_in client_address;
                socklen_t client_addrlength = sizeof( client_address );

                //连接的fd
                int connfd = accept( listenfd, ( struct sockaddr* )&client_address, &client_addrlength );
                
                if ( connfd < 0 ) {
                    printf( "errno is: %d\n", errno );
                    continue;
                } 

                if( http_conn::m_user_count >= MAX_FD ) {//目前最大支持的连接数满了
                    close(connfd);
                    continue;
                }
                //将新的客户的数据初始化，放到数组中
                users[connfd].init( connfd, client_address);

            } else if( events[i].events & ( EPOLLRDHUP | EPOLLHUP | EPOLLERR ) ) {
                //取出EPOLLRDHUP | EPOLLHUP | EPOLLERR事件
                //对方异常断开等错误事件发生
                users[sockfd].close_conn();

            } else if(events[i].events & EPOLLIN) {//取出读事件

                if(users[sockfd].read()) {//read：一次性读完所有数据
                    pool->append(users + sockfd);//添加到线程池中
                } else {//读失败
                    users[sockfd].close_conn();
                }

            }  else if( events[i].events & EPOLLOUT ) {//取出写事件

                if( !users[sockfd].write() ) {//一次性写完
                    users[sockfd].close_conn();
                }

            }
        }
    }
    
    close( epollfd );
    close( listenfd );
    delete [] users;
    delete pool;
    return 0;
}