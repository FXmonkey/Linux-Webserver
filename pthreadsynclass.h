#ifndef PTHREADSYNCLASS_H
#define PTHREADSYNCLASS_H
#include<pthread.h>
#include<exception>
#include<semaphore.h>

class Locker{
    public:
        Locker(){
            if(pthread_mutex_init(&mutex,NULL)!=0){
                throw std::exception();
            }
        }
        ~Locker(){
            pthread_mutex_destroy(&mutex);
        }

        bool lock(){
            return pthread_mutex_lock(&mutex) == 0;
        }

        bool unlock(){
            return pthread_mutex_unlock(&mutex) == 0;
        }


        pthread_mutex_t *get(){
            return &mutex;
        }                       //获取互斥量成员mutex

    private:
        pthread_mutex_t mutex;
};



class Cond{                     //条件变量
    public:
        Cond(){
            if(pthread_cond_init(&cond,NULL)!=0){
                throw std::exception();
            }
        }

        ~Cond(){
            pthread_cond_destroy(&cond);
        }

        bool condWait(pthread_mutex_t *mutex){      //等待阻塞，阻塞会解锁
            return pthread_cond_wait(&cond,mutex)==0;
        }

        bool condTimeWait(pthread_mutex_t *mutex,struct timespec tim){
            return pthread_cond_timedwait(&cond,mutex,&tim)==0;
        }

        bool condSignal(){
            return pthread_cond_signal(&cond)==0;
        }

        bool condBrodcast(){
            return pthread_cond_broadcast(&cond)==0;
        }

    private:
        pthread_cond_t cond;
};

class Sem{                  //信号量
    public:
        Sem(int value){
            if(sem_init(&sem,0,value)!=0){
                throw std::exception();
            }  
        }
        Sem(){
            if(sem_init(&sem,0,0)!=0){
                throw std::exception();
            }  
        }

        ~Sem(){
            sem_destroy(&sem);  
        }

        bool semWait(){
            return sem_wait(&sem) == 0 ;
        }
        bool semPost(){
            return sem_post(&sem) == 0;
        }

    private:
        sem_t sem;
};

#endif