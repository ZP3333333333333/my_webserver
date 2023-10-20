#include<stdio.h>
#include<stdlib.h>
#include<string.h>
#include<sys/socket.h>
#include<netinet/in.h>
#include<arpa/inet.h>
#include<unistd.h>
#include<errno.h>
#include<fcntl.h>
#include<sys/epoll.h>
#include"locker.h"
#include"threadpool.h"
#include<signal.h>
#include"http_conn.h"

#define MAX_FD 65535//最大的文件描述符个数
#define MAX_EVENT_NUMBER 10000//监听的最大的事件数量

//添加信号捕捉
void addsig(int sig,void(handler)(int)){//信号处理函数
    struct sigaction sa;
    memset(&sa,'\0',sizeof(sa));//清空信号(初始化)
    sa.sa_handler=handler;
    sigfillset(&sa.sa_mask);
    sigaction(sig,&sa,NULL);
}


//extern 关键字实质上是声明，别的地方必须实现定义 
//添加文件描述符到epoll中
extern void addfd(int epollfd,int fd,bool one_shot);

//从epoll中删除文件描述符
extern void removefd(int epollfd,int fd);

//修改文件描述符
extern void modfd(int epollfd,int fd,int ev);

int main(int argc,char*argv[]){
    if(argc<=1){//参数个数＜＝1，有问题，因为至少要传递一个端口号
        printf("按照如下格式运行：%s port_number\n",basename(argv[0]));
        exit(-1);
    }

    //获取端口号
    int port=atoi(argv[1]);//[0]是程序名，[1]是端口号
    
    //对sigpie信号进行处理
    addsig(SIGPIPE,SIG_IGN);

    //创建线程池，初始化线程池
    threadpool<http_conn>*pool=NULL;
    try{
        pool=new threadpool<http_conn>;
    }catch(...){
        exit(-1);
    }

    //创建数组，用于保存所有客户端信息
    http_conn *users=new http_conn[MAX_FD];

    //创建监听的套接字
    int listenfd=socket(PF_INET,SOCK_STREAM,0);

    //设置端口复用(一定是在绑定之前设置)
    int reuse=1;
    setsockopt(listenfd,SOL_SOCKET,SO_REUSEADDR,&reuse,sizeof(reuse));

    //绑定
    struct sockaddr_in address;
    address.sin_family=AF_INET;
    address.sin_addr.s_addr=INADDR_ANY;
    address.sin_port=htons(port);
    bind(listenfd,(struct sockaddr*)&address,sizeof(address));

    //监听
    listen(listenfd,5);

    //创建epoll对象，事件数组，添加
    epoll_event events[MAX_EVENT_NUMBER];
    int epollfd=epoll_create(5);//任意传值，只要不是0就行

    //将监听的文件描述符添加到epoll对象中
    //创建添加函数
    addfd(epollfd,listenfd,false);//false:不需要添加oneshot事件
    http_conn::m_epollfd=epollfd;

    while(1){
        int num=epoll_wait(epollfd,events,MAX_EVENT_NUMBER,-1);
        //num:检测到了几个事件
        if((num<0)&&(errno!=EINTR)){
            printf("epoll failure\n");
            break;
        }

        //循环遍历事件数组
        for(int i=0;i<num;i++){
            int sockfd=events[i].data.fd;
            if(sockfd==listenfd){
                //有客户端连接进来,连接客户端
                struct sockaddr_in client_address;
                socklen_t client_addrlen=sizeof(client_address);
                int connfd= accept(listenfd,(struct sockaddr*)&client_address,&client_addrlen);
                
                if(http_conn::m_user_count>=MAX_FD){
                    //目前连接数满了，回写一个数据给客户端
                    close(connfd);
                    continue;
                }
                //将新的客户的数据初始化，放到数组中
                users[connfd].init(connfd,client_address);
            }else if(events[i].events&(EPOLLRDHUP|EPOLLHUP|EPOLLERR)){
                    //对方异常断开或者错误等事件
                    users[sockfd].close_conn();
            }else if(events[i].events&EPOLLIN){//判断是不是有读的事件发生
                //有读的事件发生，则一次性把所有事件读出来
                if(users[sockfd].read()){
                    //成功读完所有事件
                    //交给工作线程处理
                    pool->append(users+sockfd);//地址  
                }else{
                    //没读到数据，关闭连接
                    users[sockfd].close_conn();
                }

            }else if(events[i].events&EPOLLOUT){
                //检测写事件
                if(!users[sockfd].write()){//一次性写完所有事件
                    users[sockfd].close_conn();
                }

            }


        }

    }
    close(epollfd);
    close(listenfd);
    delete[]users;
    delete pool;

    return 0;

}