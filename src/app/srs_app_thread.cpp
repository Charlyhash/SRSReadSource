/*
The MIT License (MIT)

Copyright (c) 2013-2015 SRS(ossrs)

Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
the Software, and to permit persons to whom the Software is furnished to do so,
subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

#include <srs_app_thread.hpp>

#include <srs_kernel_error.hpp>
#include <srs_kernel_log.hpp>
#include <srs_app_log.hpp>

namespace internal {
    ISrsThreadHandler::ISrsThreadHandler()
    {
    }
    
    ISrsThreadHandler::~ISrsThreadHandler()
    {
    }
    
    void ISrsThreadHandler::on_thread_start()
    {
    }
    
    int ISrsThreadHandler::on_before_cycle()
    {
        int ret = ERROR_SUCCESS;
        return ret;
    }
    
    int ISrsThreadHandler::on_end_cycle()
    {
        int ret = ERROR_SUCCESS;
        return ret;
    }
    
    void ISrsThreadHandler::on_thread_stop()
    {
    }

    /*
     * 线程的构造函数
     * name：函数名
     * thread_handle：线程处理函数
     * interval_us: 休眠时长
     * joinalbe: 是否能join
     * */
    SrsThread::SrsThread(const char* name, ISrsThreadHandler* thread_handler, int64_t interval_us, bool joinable)
    {
        _name = name;
        handler = thread_handler;
        cycle_interval_us = interval_us;
        
        tid = NULL;
        loop = false;
        really_terminated = true;
        _cid = -1;
        _joinable = joinable;
        disposed = false;
        
        // in start(), the thread cycle method maybe stop and remove the thread itself,
        // and the thread start() is waiting for the _cid, and segment fault then.
        // @see https://github.com/ossrs/srs/issues/110
        // thread will set _cid, callback on_thread_start(), then wait for the can_run signal.
        can_run = false;
    }
    //析构函数，调用stop
    SrsThread::~SrsThread()
    {
        stop();
    }
    
    int SrsThread::cid()
    {
        return _cid;
    }
    //启动一个协程
    int SrsThread::start()
    {
        int ret = ERROR_SUCCESS;
        
        if(tid) {
            srs_info("thread %s already running.", _name);
            return ret;
        }
        //创建协程，调用thread_fun
        if((tid = st_thread_create(thread_fun, this, (_joinable? 1:0), 0)) == NULL){
            ret = ERROR_ST_CREATE_CYCLE_THREAD;
            srs_error("st_thread_create failed. ret=%d", ret);
            return ret;
        }
        //是否dispose
        disposed = false;
        // we set to loop to true for thread to run.
        loop = true; //loop为true
        
        // wait for cid to ready, for parent thread to get the cid.
        while (_cid < 0) {
            st_usleep(10 * 1000);
        }
        
        // now, cycle thread can run.
        can_run = true;
        
        return ret;
    }

    //停止一个协程
    void SrsThread::stop()
    {
        if (!tid) {
            return;
        }
        
        loop = false; //loop为false, 那么不会继续执行cycle()
        
        dispose(); //释放协程
        
        _cid = -1;
        can_run = false;
        tid = NULL;        
    }
    
    bool SrsThread::can_loop()
    {
        return loop;
    }
    
    void SrsThread::stop_loop()
    {
        loop = false;
    }

    //清理
    void SrsThread::dispose()
    {
        if (disposed) {
            return;
        }
        
        // the interrupt will cause the socket to read/write error,
        // which will terminate the cycle thread.
        st_thread_interrupt(tid);
        
        // when joinable, wait util quit.
        if (_joinable) {
            // wait the thread to exit.
            int ret = st_thread_join(tid, NULL);
            if (ret) {
                srs_warn("core: ignore join thread failed.");
            }
        }
        
        // wait the thread actually terminated.
        // sometimes the thread join return -1, for example,
        // when thread use st_recvfrom, the thread join return -1.
        // so here, we use a variable to ensure the thread stopped.
        // @remark even the thread not joinable, we must ensure the thread stopped when stop.
        while (!really_terminated) {
            st_usleep(10 * 1000);
            
            if (really_terminated) {
                break;
            }
            srs_warn("core: wait thread to actually terminated");
        }
        
        disposed = true;
    }

    //协程的循环
    void SrsThread::thread_cycle()
    {
        int ret = ERROR_SUCCESS;
        
        _srs_context->generate_id(); //生成cid
        srs_info("thread %s cycle start", _name);
        
        _cid = _srs_context->get_id();
        
        srs_assert(handler);
        handler->on_thread_start(); //调用handle的on_thread_start
        
        // thread is running now.
        really_terminated = false;
        
        // wait for cid to ready, for parent thread to get the cid.
        while (!can_run && loop) {
            st_usleep(10 * 1000);
        }
        //正在的loop，loop里执行函数为：on_before_cycle->cycle->on_end_cycle
        while (loop) {
            if ((ret = handler->on_before_cycle()) != ERROR_SUCCESS) {
                srs_warn("thread %s on before cycle failed, ignored and retry, ret=%d", _name, ret);
                goto failed;
            }
            srs_info("thread %s on before cycle success", _name);
            
            if ((ret = handler->cycle()) != ERROR_SUCCESS) {
                if (!srs_is_client_gracefully_close(ret) && !srs_is_system_control_error(ret)) {
                    srs_warn("thread %s cycle failed, ignored and retry, ret=%d", _name, ret);
                }
                goto failed;
            }
            srs_info("thread %s cycle success", _name);
            
            if ((ret = handler->on_end_cycle()) != ERROR_SUCCESS) {
                srs_warn("thread %s on end cycle failed, ignored and retry, ret=%d", _name, ret);
                goto failed;
            }
            srs_info("thread %s on end cycle success", _name);
            
        failed:
            if (!loop) {
                break;
            }
            
            // to improve performance, donot sleep when interval is zero.
            // @see: https://github.com/ossrs/srs/issues/237
            if (cycle_interval_us != 0) {
                st_usleep(cycle_interval_us);
            }
        }
        
        // really terminated now.
        really_terminated = true;
        
        handler->on_thread_stop();//停止时的回调
        srs_info("thread %s cycle finished", _name);
    }

    //协程执行的函数
    void* SrsThread::thread_fun(void* arg)
    {
        SrsThread* obj = (SrsThread*)arg;
        srs_assert(obj);
        
        obj->thread_cycle(); //调用cycle函数
        
        // for valgrind to detect.
        SrsThreadContext* ctx = dynamic_cast<SrsThreadContext*>(_srs_context);
        if (ctx) {
            ctx->clear_cid();
        }
        
        st_thread_exit(NULL); //退出协程
        
        return NULL;
    }
}

ISrsEndlessThreadHandler::ISrsEndlessThreadHandler()
{
}

ISrsEndlessThreadHandler::~ISrsEndlessThreadHandler()
{
}

void ISrsEndlessThreadHandler::on_thread_start()
{
}

int ISrsEndlessThreadHandler::on_before_cycle()
{
    return ERROR_SUCCESS;
}

int ISrsEndlessThreadHandler::on_end_cycle()
{
    return ERROR_SUCCESS;
}

void ISrsEndlessThreadHandler::on_thread_stop()
{
}

//构造函数，n:协程的名字，h:协程的处理函数
SrsEndlessThread::SrsEndlessThread(const char* n, ISrsEndlessThreadHandler* h)
{
    handler = h;
    pthread = new internal::SrsThread(n, this, 0, false); //创建一个协程
}
//析构函数，停止协程，并释放协程指针
SrsEndlessThread::~SrsEndlessThread()
{
    pthread->stop();
    srs_freep(pthread);
}
//启动，调用协程的start()
int SrsEndlessThread::start()
{
    return pthread->start();
}

int SrsEndlessThread::cycle()
{
    return handler->cycle();
}

void SrsEndlessThread::on_thread_start()
{
    handler->on_thread_start();
}

int SrsEndlessThread::on_before_cycle()
{
    return handler->on_before_cycle();
}

int SrsEndlessThread::on_end_cycle()
{
    return handler->on_end_cycle();
}

void SrsEndlessThread::on_thread_stop()
{
    handler->on_thread_stop();
}

ISrsOneCycleThreadHandler::ISrsOneCycleThreadHandler()
{
}

ISrsOneCycleThreadHandler::~ISrsOneCycleThreadHandler()
{
}

void ISrsOneCycleThreadHandler::on_thread_start()
{
}

int ISrsOneCycleThreadHandler::on_before_cycle()
{
    return ERROR_SUCCESS;
}

int ISrsOneCycleThreadHandler::on_end_cycle()
{
    return ERROR_SUCCESS;
}

void ISrsOneCycleThreadHandler::on_thread_stop()
{
}

SrsOneCycleThread::SrsOneCycleThread(const char* n, ISrsOneCycleThreadHandler* h)
{
    handler = h;
    pthread = new internal::SrsThread(n, this, 0, false);
}

SrsOneCycleThread::~SrsOneCycleThread()
{
    pthread->stop();
    srs_freep(pthread);
}

int SrsOneCycleThread::start()
{
    return pthread->start();
}

//执行完handler->cycle()后，调用stop_loop，退出
int SrsOneCycleThread::cycle()
{
    int ret = handler->cycle();
    pthread->stop_loop();
    return ret;
}

void SrsOneCycleThread::on_thread_start()
{
    handler->on_thread_start();
}

int SrsOneCycleThread::on_before_cycle()
{
    return handler->on_before_cycle();
}

int SrsOneCycleThread::on_end_cycle()
{
    return handler->on_end_cycle();
}

void SrsOneCycleThread::on_thread_stop()
{
    handler->on_thread_stop();
}

ISrsReusableThreadHandler::ISrsReusableThreadHandler()
{
}

ISrsReusableThreadHandler::~ISrsReusableThreadHandler()
{
}

void ISrsReusableThreadHandler::on_thread_start()
{
}

int ISrsReusableThreadHandler::on_before_cycle()
{
    return ERROR_SUCCESS;
}

int ISrsReusableThreadHandler::on_end_cycle()
{
    return ERROR_SUCCESS;
}

void ISrsReusableThreadHandler::on_thread_stop()
{
}

//构造函数，传入名字，处理类以及时间间隔
SrsReusableThread::SrsReusableThread(const char* n, ISrsReusableThreadHandler* h, int64_t interval_us)
{
    handler = h;
    pthread = new internal::SrsThread(n, this, interval_us, true);
}

SrsReusableThread::~SrsReusableThread()
{
    pthread->stop();
    srs_freep(pthread);
}

int SrsReusableThread::start()
{
    return pthread->start();
}
//可以调用stop
void SrsReusableThread::stop()
{
    pthread->stop();
}

int SrsReusableThread::cid()
{
    return pthread->cid();
}

int SrsReusableThread::cycle()
{
    return handler->cycle();
}

void SrsReusableThread::on_thread_start()
{
    handler->on_thread_start();
}

int SrsReusableThread::on_before_cycle()
{
    return handler->on_before_cycle();
}

int SrsReusableThread::on_end_cycle()
{
    return handler->on_end_cycle();
}

void SrsReusableThread::on_thread_stop()
{
    handler->on_thread_stop();
}

ISrsReusableThread2Handler::ISrsReusableThread2Handler()
{
}

ISrsReusableThread2Handler::~ISrsReusableThread2Handler()
{
}

void ISrsReusableThread2Handler::on_thread_start()
{
}

int ISrsReusableThread2Handler::on_before_cycle()
{
    return ERROR_SUCCESS;
}

int ISrsReusableThread2Handler::on_end_cycle()
{
    return ERROR_SUCCESS;
}

void ISrsReusableThread2Handler::on_thread_stop()
{
}

SrsReusableThread2::SrsReusableThread2(const char* n, ISrsReusableThread2Handler* h, int64_t interval_us)
{
    handler = h;
    pthread = new internal::SrsThread(n, this, interval_us, true);
}

SrsReusableThread2::~SrsReusableThread2()
{
    pthread->stop();
    srs_freep(pthread);
}

int SrsReusableThread2::start()
{
    return pthread->start();
}

void SrsReusableThread2::stop()
{
    pthread->stop();
}

int SrsReusableThread2::cid()
{
    return pthread->cid();
}

//多了interrupt
void SrsReusableThread2::interrupt()
{
    pthread->stop_loop();
}

bool SrsReusableThread2::interrupted()
{
    return !pthread->can_loop();
}

int SrsReusableThread2::cycle()
{
    return handler->cycle();
}

void SrsReusableThread2::on_thread_start()
{
    handler->on_thread_start();
}

int SrsReusableThread2::on_before_cycle()
{
    return handler->on_before_cycle();
}

int SrsReusableThread2::on_end_cycle()
{
    return handler->on_end_cycle();
}

void SrsReusableThread2::on_thread_stop()
{
    handler->on_thread_stop();
}

