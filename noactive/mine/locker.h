#ifndef LOCKER_H
#define LOCKER_H

#include<pthread.h>
#include<exception>
#include<semaphore.h>

//线程同步机制封装类

//互斥锁类
class locker{
public:
    locker(){
        if(pthread_mutex_init(&m_mutex,NULL)!=0){
            throw std::exception();
        }
    }

    ~locker(){
        pthread_mutex_destroy(&m_mutex);
    }

    bool lock(){
        return pthread_mutex_lock(&m_mutex)==0;
    }

    bool unlock(){
        return pthread_mutex_unlock(&m_mutex)==0;
    }

    pthread_mutex_t *get(){//获取成员变量的地址
        return &m_mutex;

    }


private:
    pthread_mutex_t m_mutex;
};

//条件变量类
class cond{

public:
    cond(){
        if(pthread_cond_init(&m_cond,NULL)!=0){
            throw std::exception();
        }
    }

    ~cond(){
        pthread_cond_destroy(&m_cond);
    }
    
/*
    int pthread_cond_wait(pthread_cond_t *restrict cond, pthread_mutex_t *restrict mutex);
        - 等待，调用了该函数，线程会阻塞。
*/
    bool wait(pthread_mutex_t* m_mutex){
        return pthread_cond_wait(&m_cond,m_mutex)==0;
    }

/*
    int pthread_cond_timedwait(pthread_cond_t *restrict cond, pthread_mutex_t *restrict mutex, const struct timespec *restrict abstime);
        - 等待多长时间，调用了这个函数，线程会阻塞，直到指定的时间结束。
*/
    bool timewait(pthread_mutex_t* m_mutex,struct timespec t){
        return pthread_cond_timedwait(&m_cond,m_mutex,&t)==0;
    }

/*
    int pthread_cond_signal(pthread_cond_t *cond);
        - 唤醒一个或者多个等待的线程
*/
    bool signal(pthread_mutex_t* m_mutex){
        return pthread_cond_signal(&m_cond)==0;
    }

/*
    int pthread_cond_broadcast(pthread_cond_t *cond);
        - 唤醒所有的等待的线程
*/
    bool broadcast(){
        return pthread_cond_broadcast(&m_cond)==0;
    }


private:
    pthread_cond_t m_cond;
};

//信号量类
class sem{
public:

/*
    信号量的类型 sem_t
    int sem_init(sem_t *sem, int pshared, unsigned int value);
        - 初始化信号量
        - 参数：
            - sem : 信号量变量的地址
            - pshared : 0 用在线程间 ，非0 用在进程间
            - value : 信号量中的值
*/
    sem(){
        if(sem_init(&m_sem,0,0)!=0){
            throw std::exception();
        }
    }

    sem(int num){
        if(sem_init(&m_sem,0,num)!=0){
            throw std::exception();
        }
    }

    ~sem(){
        sem_destroy(&m_sem);
    }

    //等待信号量
/*
    int sem_wait(sem_t *sem);
        - 对信号量加锁，调用一次对信号量的值-1，如果值为0，就阻塞
*/
    bool wait(){
        return sem_wait(&m_sem)==0;
    }

    //增加信号量
/*
    int sem_post(sem_t *sem);
        - 对信号量解锁，调用一次对信号量的值+1
*/
    bool post(){
        return sem_post(&m_sem)==0;
    }

private:
    sem_t m_sem;
};


#endif