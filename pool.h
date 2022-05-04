#ifndef POOL_H
#define POOL_H
#include"pthreadsynclass.h"
#include<cstdio>

#include<list>


template<typename T>   //模板类，T后面会用，为任务类
class pool{
    public:
        pool(int thread_num = 8,int maxRequest = 10000);//初始化
        ~pool();
        
        //向任务队列添加任务
        bool addTask(T* request);

    private:
        //静态函数,工作线程
        static void *worker(void *arg);
        //其后续可能访问后续的成员，但他们为非静态，无法访问
        void run();

    private:
        //线程数量
        int p_thread_num;

        //线程池数组（数组用来装线程）
        pthread_t *p_threadQueue;

        //请求队列中最多允许的，等待处理的请求的数量
        int p_maxRequest;

        //请求队列
        std::list<T *>p_requestQueue;

        //互斥锁
        Locker p_mutex_queue;

        //信号量，判断是否有任务需要处理
        Sem p_taskNeedHandle;

        //是否结束线程
        bool p_stop;

};

//实现方法
template<typename T>
pool<T>::pool(int thread_num,int maxRequest):           //对参数初始化
    p_thread_num(thread_num),p_maxRequest(maxRequest),
    p_stop(false),p_threadQueue(NULL){
        if(thread_num<=0||maxRequest<=0){
            throw std::exception();
        }
        p_threadQueue = new pthread_t[thread_num];  //用new动态创建线程池数组,后面需要delete释放

        if(!p_threadQueue){
            throw std::exception();  
        }

        //创建线程并将其设置为分离态
        for(int i=0;i<thread_num;i++){
            printf("正在创建第%d个线程\n",i);
            if(0!=pthread_create(p_threadQueue + i,NULL,worker,this))   //work：必为静态函数
            {
                delete []p_threadQueue;
                throw std::exception();
            }
            if(pthread_detach(p_threadQueue[i])){
                delete []p_threadQueue;
                throw std::exception();
            }
        }
    }

template<typename T>
pool<T>::~pool(){               //析构函数
    delete []p_threadQueue;     //释放线程池数组
    p_stop = true;               //可停止线程
}


template<typename T>                    //类模板
bool pool<T>::addTask(T* request){      //poll<T>表示用<>中的代替类模板中的<>类型
    //添加任务，要保证线程同步push_back
    p_mutex_queue.lock();               //加锁

    if(p_requestQueue.size() > p_maxRequest){  //若请求队列已满，则不能添加
        p_mutex_queue.unlock();
        return false;
    }
    p_requestQueue.push_back(request);

    p_mutex_queue.unlock();         //解锁

    //信号量post，表示添加了一个
    p_taskNeedHandle.semPost();
    return true;        

}


template<typename T>
void * pool<T>::worker(void *arg){
    //静态不能访问其他非静态成员，可利用this
    pool *work = (pool *)arg;       //this为pool对象
    work->run();                    //需要运行（从工作队列中取数据后执行任务）    
}


template<typename T>
void pool<T>::run(){
    while(!p_stop){
        p_taskNeedHandle.semWait();
        p_mutex_queue.lock();

        T *request = p_requestQueue.front();        //取出请求队中最前的任务
        p_requestQueue.pop_front();                 //删除被取出任务
        
        p_mutex_queue.unlock();

        if(!request){               //若没有取出，则continue
            continue;
        }
        request->process();         //去执行任务的函数process();
    }
}
#endif