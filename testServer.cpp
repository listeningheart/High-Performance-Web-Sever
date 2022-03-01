#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <sys/socket.h>
#include <sys/unistd.h>
#include <sys/types.h>
#include <sys/errno.h>
#include <sys/epoll.h>
#include <netinet/in.h>
#include <signal.h>
#include <netdb.h>
#include <fcntl.h>
#include "ThreadPool.h"

#define BUFFSIZE 2048
#define DEFAULT_PORT 16555
#define MAXLINK 2048
#define MAXEVENT 1000

int sockfd, connfd, epfd;//

static int set_socket_non_blocking (int fd) {
    int flags, s;
    // 获取当前flag
    flags = fcntl(fd, F_GETFL, 0);
    if (-1 == flags) {
        perror("Get fd status");
        return -1;
    }

    flags |= O_NONBLOCK;

    // 设置flag
    s = fcntl(fd, F_SETFL, flags);
    if (-1 == s) {
        perror("Set fd status");
        return -1;
    }
    return 0;
}

void setResponse(char *buff)
{
    bzero(buff, sizeof(buff));
    strcat(buff, "HTTP/1.1 200 OK\r\n");
    strcat(buff, "Connection: close\r\n");
    strcat(buff, "\r\n");
    strcat(buff,"Hello!!\n");
}

void stopServerRunning(int p){
    close(sockfd);
    printf("Clost Server\n");
    exit(0);
}
void ResponseClient(int & ret,int conn,  char* buff){
                ret = read(conn, buff, sizeof(buff));
                if(ret == -1)
                {
                    // perror("read");
                    return ;
                }

                 printf("%s\n",buff);
                setResponse(buff);
                send(conn, buff, sizeof(buff),0);
                printf("%s\n",buff);
                memset(buff, 0, sizeof(buff));
}

int main()
{
    struct sockaddr_in servaddr;
    char buff[BUFFSIZE];

    sockfd = socket(AF_INET, SOCK_STREAM, 0);//
    if(sockfd == -1)
    {
        printf("Create socket error(%d): %s\n", errno, strerror(errno));
        return -1;
    }

    bzero(&servaddr, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    servaddr.sin_port = htons(DEFAULT_PORT);
    if(-1 == bind(sockfd, (struct sockaddr*)&servaddr, sizeof(servaddr)))
    {
        printf("Bind error(%d): %s\n",errno, strerror(errno));
        return -1;
    }

    if(-1 == listen(sockfd, MAXLINK))//listen chenggong fanhui  0.zhuan wei listen zhuangtai
    {
        printf("Listen error(%d): %s\n",errno, strerror(errno));
        return -1;
    }
    //打开 socket 端口复用, 防止测试的时候出现 Address already in use
    int on=100;
    setsockopt( sockfd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on) );

    printf("ZL Listening...\n");

// 建立epoll非阻塞多重IO模式
    set_socket_non_blocking( sockfd);
    struct epoll_event ev, event[MAXEVENT];
    epfd = epoll_create1(0);
    if(1 == epfd)
    {
        printf("Create epoll error");
        return -1;
    }
    //ep绑定
    ev.data.fd = sockfd;
    ev.events = EPOLLIN | EPOLLET;/*边缘触发选项,LIN写事件*/
    if(-1 == epoll_ctl(epfd, EPOLL_CTL_ADD, sockfd, &ev))//监视sockfd
    {
        printf("Set epoll error");
        return -1;
    }

    //开启线程池，初始化
	ThreadPool pool(10);
	pool.init();

    while(true)
    {
        int epoll_wait_count;//epool接收数量
        // signal(SIGINT, stopServerRunning);//ctrl+c退出
        epoll_wait_count = epoll_wait(epfd, event, MAXEVENT, -1);

        for(int i=0; i<epoll_wait_count; ++i)
        {
            uint32_t events = event[i].events;
            //接收到的IP地址,PORT缓存
            char host_buf[NI_MAXHOST];
            char port_buf[NI_MAXSERV];

            int __result;
            if(events & EPOLLERR || events & EPOLLHUP || (!events & EPOLLIN)){
                    printf("Epoll has error\n");
                    close(event[i].data.fd);//关闭申请的客户端连接
                    continue;
            }
            else if(sockfd != event[i].data.fd)
            {
                //处理已连接的客户端connfd，事件；
                int conn=0;
                conn = event[i].data.fd;
                if(conn < 0)
                    continue;
                int ret;
                pool.submit(ResponseClient,std::ref(ret),conn,buff);//提交任务给线程池
                if(ret == -1)
                {
                    perror("read");
                    return -5;
                }
                // epoll_ctl(epfd, EPOLL_CTL_DEL, conn, &event[i]);
                close(conn);
            }
            else{
                //监听到新的客户端连接，进行accept，获取客户端的socket
                while(true)
                {
                    struct sockaddr in_addr;
                    socklen_t in_addr_len = sizeof(in_addr);
                    bzero(&in_addr, in_addr_len);
                    int accp_fd = accept(sockfd, &in_addr, &in_addr_len);

                    if(-1 == accp_fd)
                    {
                        printf("Accept error!");
                        break;
                    }
                   set_socket_non_blocking(accp_fd );
                    __result = getnameinfo(&in_addr, in_addr_len, host_buf, NI_MAXHOST, port_buf, NI_MAXSERV,
                                                                    NI_NUMERICHOST | NI_NUMERICSERV);//设置转为数字形式.
                    if(! __result)
                    {
                        printf("New connection : host = %s, port= %s\n", host_buf, port_buf);
                    }

                    ev.data.fd = accp_fd;
                    ev.events = EPOLLIN | EPOLLET;
                    if(-1 == epoll_ctl(epfd, EPOLL_CTL_ADD, accp_fd, &ev))
                    {
                        printf("Epoll ctl error\n");
                        return -1;
                    }
                }
                continue;
            }
        }
    }
        pool.shutdown();
        close(epfd);
        close(sockfd);
        return 0;
}