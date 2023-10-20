#ifndef THREADPOOL_H
#define THREADPOOL_H

#include<pthread.h>
#include<list>
#include<exception>
#include<cstdio>
#include"locker.h"

//线程池类，定义成模板类为了代码复用,T是任务类
template<typename T>
class threadpool{
public:
    //构造函数
    threadpool(int thread_number=8,int max_requests=10000);
    //析构函数
    ~threadpool();
    //添加任务
    bool append(T* request);

private:
    static void* worker(void* arg);
    void run();//线程池的工作

private:
    //线程数量
    int m_thread_number;

    //线程池数组,大小为m_thread_number
    pthread_t* m_threads;

    //请求队列中最多允许的，等待处理的请求数量
    int m_max_requests;

    //请求队列，工作队列
    std::list<T*>m_workqueue;

    //互斥锁
    locker m_queuelocker;

    //信号量，判断是否有任务需要处理
    sem m_queuestat;

    //是否结束线程
    bool m_stop;

};

template<typename T>
threadpool<T>::threadpool(int thread_number,int max_requests):
    m_thread_number(thread_number),
    m_max_requests(max_requests),
    m_stop(false),//默认不停止线程
    m_threads(NULL){//线程池数组,默认初始化为NULL
    if((thread_number<=0)||(max_requests<=0)){
        throw std::exception();
    }

    //创建线程池数组
    m_threads=new pthread_t[m_thread_number];

    if(!m_threads){//创建失败
        throw std::exception();
    }

    //创建thread_number个线程，并将它们设置为线程脱离
    for(int i=0;i<thread_number;i++){
        printf("creat the %d thread\n",i);

        if(pthread_create(m_threads+i,NULL,worker,this)!=0){
            //C++中worker必须为静态函数
            delete[]m_threads;//释放内存，注意要带[]
            throw std::exception();
        }

        //创建成功，设置线程分离
        //On success, pthread_detach() returns 0; on error, it  returns
       //an error number.
        if(pthread_detach(m_threads[i])){
            delete[]m_threads;
            throw std::exception();
        }
    }
}

template<typename T>
threadpool<T>::~threadpool(){
    delete[]m_threads;
    m_stop=true;//停止线程
}

template<typename T>
bool threadpool<T>::append(T*request){
    //保证线程同步，加锁
    m_queuelocker.lock();
    if(m_workqueue.size()>m_max_requests){
        m_queuelocker.unlock();
        return false;
    }

    m_workqueue.push_back(request);
    m_queuelocker.unlock();
    m_queuestat.post();//成功加入一个线程，信号量加一
    return true;
}

template<typename T>
void* threadpool<T>::worker(void* arg){
    //把this作为参数传递到worker函数中
    //这样就能让静态函数worker访问非静态的类内对象
    threadpool*pool=(threadpool*)arg;//强制类型转换
    //执行run
    pool->run();
    
    //返回值没有什么用
    return pool;
}

template<typename T>
void threadpool<T>::run(){
    while(!m_stop){//一直循环，直到线程关闭

        //用信号量来判断，有无工作可做
        m_queuestat.wait();//信号量-1
        m_queuelocker.lock();//有任务可做，先上锁
        if(m_workqueue.empty()){//工作队列为空，无任务需要执行
            m_queuelocker.unlock();
            continue;
        }

        //有数据
        T*request= m_workqueue.front();//取出队列头的任务
        m_workqueue.pop_front();//删除任务
        m_queuelocker.unlock();//完成取出任务，解锁工作队列

        if(!request){
            continue;
        }

        request->process();//调用任务类的process

    }
}


#endif