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

#ifndef SRS_APP_THREAD_HPP
#define SRS_APP_THREAD_HPP

/*
#include <srs_app_thread.hpp>
*/
#include <srs_core.hpp>

#include <srs_app_st.hpp>

// the internal classes, user should never use it.
// user should use the public classes at the bellow:
// @see SrsEndlessThread, SrsOneCycleThread, SrsReusableThread
namespace internal {
    /**
     * the handler for the thread, callback interface.
     * the thread model defines as:
     *     handler->on_thread_start()
     *     while loop:
     *        handler->on_before_cycle()
     *        handler->cycle()
     *        handler->on_end_cycle()
     *        if !loop then break for user stop thread.
     *        sleep(CycleIntervalMilliseconds)
     *     handler->on_thread_stop()
     * when stop, the thread will interrupt the st_thread,
     * which will cause the socket to return error and
     * terminate the cycle thread.
     *
     * @remark why should check can_loop() in cycle method?
     *       when thread interrupt, the socket maybe not got EINT,
     *       espectially on st_usleep(), so the cycle must check the loop,
     *       when handler->cycle() has loop itself, for example:
     *               while (true):
     *                   if (read_from_socket(skt) < 0) break;
     *       if thread stop when read_from_socket, it's ok, the loop will break,
     *       but when thread stop interrupt the s_usleep(0), then the loop is
     *       death loop.
     *       in a word, the handler->cycle() must:
     *               while (pthread->can_loop()):
     *                   if (read_from_socket(skt) < 0) break;
     *       check the loop, then it works.
     *
     * @remark why should use stop_loop() to terminate thread in itself?
     *       in the thread itself, that is the cycle method,
     *       if itself want to terminate the thread, should never use stop(),
     *       but use stop_loop() to set the loop to false and terminate normally.
     *
     * @remark when should set the interval_us, and when not?
     *       the cycle will invoke util cannot loop, eventhough the return code of cycle is error,
     *       so the interval_us used to sleep for each cycle.
     */
    /*
     * 线程处理类，定制线程启动的回调函数
     * */
    class ISrsThreadHandler
    {
    public:
        ISrsThreadHandler();
        virtual ~ISrsThreadHandler();
    public:
        virtual void on_thread_start(); //线程启动
        virtual int on_before_cycle(); //cycle前
        virtual int cycle() = 0; //cycle
        virtual int on_end_cycle(); //cycle后
        virtual void on_thread_stop(); //stop时
    };
    
    /**
     * provides service from st_thread_t,
     * for common thread usage.
     */
     /*
      * 协程的封装，作为内部使用的类
      * */
    class SrsThread
    {
    private:
        st_thread_t tid; //tid
        int _cid; //cid
        bool loop; //是否支持loop
        bool can_run; //是否能run
        bool really_terminated; //是否terminate
        bool _joinable; //是否joinable
        const char* _name; //协程名字
        bool disposed; //是否dispose
    private:
        ISrsThreadHandler* handler; //回调处理
        int64_t cycle_interval_us; //循环时间us
    public:
        /**
         * initialize the thread.
         * @param name, human readable name for st debug.
         * @param thread_handler, the cycle handler for the thread.
         * @param interval_us, the sleep interval when cycle finished.
         * @param joinable, if joinable, other thread must stop the thread.
         * @remark if joinable, thread never quit itself, or memory leak.
         * @see: https://github.com/ossrs/srs/issues/78
         * @remark about st debug, see st-1.9/README, _st_iterate_threads_flag
         */
        /**
         * TODO: FIXME: maybe all thread must be reap by others threads,
         * @see: https://github.com/ossrs/srs/issues/77
         */
         //初始化协程
        SrsThread(const char* name, ISrsThreadHandler* thread_handler, int64_t interval_us, bool joinable);
        virtual ~SrsThread();
    public:
        /**
         * get the context id. @see: ISrsThreadContext.get_id().
         * used for parent thread to get the id.
         * @remark when start thread, parent thread will block and wait for this id ready.
         */
        virtual int cid(); //获取cid
        /**
         * start the thread, invoke the cycle of handler util
         * user stop the thread.
         * @remark ignore any error of cycle of handler.
         * @remark user can start multiple times, ignore if already started.
         * @remark wait for the cid is set by thread pfn.
         */
        virtual int start(); //启动线程
        /**
         * stop the thread, wait for the thread to terminate.
         * @remark user can stop multiple times, ignore if already stopped.
         */
        virtual void stop(); //暂停线程
    public:
        /**
         * whether the thread should loop,
         * used for handler->cycle() which has a loop method,
         * to check this method, break if false.
         */
        virtual bool can_loop(); //是否能loop
        /**
         * for the loop thread to stop the loop.
         * other thread can directly use stop() to stop loop and wait for quit.
         * this stop loop method only set loop to false.
         */
        virtual void stop_loop(); //停止loop
    private:
        virtual void dispose(); //释放
        virtual void thread_cycle(); //线程循环
        static void* thread_fun(void* arg); //线程循环调用的函数
    };
}

/**
 * the endless thread is a loop thread never quit.
 *      user can create thread always running util server terminate.
 *      the step to create a thread never stop:
 *      1. create SrsEndlessThread field.
 *      for example:
 *          class SrsStreamCache : public ISrsEndlessThreadHandler {
 *               public: SrsStreamCache() { pthread = new SrsEndlessThread("http-stream", this); }
 *               public: virtual int cycle() {
 *                   // do some work never end.
 *               }
 *          }
 * @remark user must use block method in cycle method, for example, sleep or socket io.
 */
 //永不退出协程的处理类
class ISrsEndlessThreadHandler
{
public:
    ISrsEndlessThreadHandler();
    virtual ~ISrsEndlessThreadHandler();
public:
    /**
     * the cycle method for the common thread.
     * @remark user must use block method in cycle method, for example, sleep or socket io.
     */
    virtual int cycle() = 0;
public:
    /**
     * other callback for handler.
     * @remark all callback is optional, handler can ignore it.
     */
    virtual void on_thread_start();
    virtual int on_before_cycle();
    virtual int on_end_cycle();
    virtual void on_thread_stop();
};

//永不退出的协程
class SrsEndlessThread : public internal::ISrsThreadHandler
{
private:
    internal::SrsThread* pthread; //包含一个协程封装类
    ISrsEndlessThreadHandler* handler; //协程处理类
public:
    SrsEndlessThread(const char* n, ISrsEndlessThreadHandler* h);
    virtual ~SrsEndlessThread();
public:
    /**
     * for the endless thread, never quit.
     */
    virtual int start(); //启动
// interface internal::ISrsThreadHandler
public:
    virtual int cycle(); //执行的循环
    virtual void on_thread_start();
    virtual int on_before_cycle();
    virtual int on_end_cycle();
    virtual void on_thread_stop();
};

/**
 * the one cycle thread is a thread do the cycle only one time,
 * that is, the thread will quit when return from the cycle.
 *       user can create thread which stop itself,
 *       generally only need to provides a start method,
 *       the object will destroy itself then terminate the thread, @see SrsConnection
 *       1. create SrsThread field
 *       2. the thread quit when return from cycle.
 *       for example:
 *           class SrsConnection : public ISrsOneCycleThreadHandler {
 *               public: SrsConnection() { pthread = new SrsOneCycleThread("conn", this); }
 *               public: virtual int start() { return pthread->start(); }
 *               public: virtual int cycle() {
 *                   // serve client.
 *                   // set loop to stop to quit, stop thread itself.
 *                   pthread->stop_loop();
 *               }
 *               public: virtual void on_thread_stop() {
 *                   // remove the connection in thread itself.
 *                   server->remove(this);
 *               }
 *           };
 */
 //一次循环的thread, 在cycle()完后自己退出
class ISrsOneCycleThreadHandler
{
public:
    ISrsOneCycleThreadHandler();
    virtual ~ISrsOneCycleThreadHandler();
public:
    /**
     * the cycle method for the one cycle thread.
     */
    virtual int cycle() = 0;
public:
    /**
     * other callback for handler.
     * @remark all callback is optional, handler can ignore it.
     */
    virtual void on_thread_start();
    virtual int on_before_cycle();
    virtual int on_end_cycle();
    virtual void on_thread_stop();
};
class SrsOneCycleThread : public internal::ISrsThreadHandler
{
private:
    internal::SrsThread* pthread;
    ISrsOneCycleThreadHandler* handler;
public:
    SrsOneCycleThread(const char* n, ISrsOneCycleThreadHandler* h);
    virtual ~SrsOneCycleThread();
public:
    /**
     * for the one cycle thread, quit when cycle return.
     */
    virtual int start();
// interface internal::ISrsThreadHandler
public:
    virtual int cycle();
    virtual void on_thread_start();
    virtual int on_before_cycle();
    virtual int on_end_cycle();
    virtual void on_thread_stop();
};

/**
 * the reuse thread is a thread stop and start by other thread.
 *       user can create thread and stop then start again and again,
 *       generally must provides a start and stop method, @see SrsIngester.
 *       the step to create a thread stop by other thread:
 *       1. create SrsReusableThread field.
 *       2. must manually stop the thread when started it.
 *       for example:
 *           class SrsIngester : public ISrsReusableThreadHandler {
 *               public: SrsIngester() { pthread = new SrsReusableThread("ingest", this, SRS_AUTO_INGESTER_SLEEP_US); }
 *               public: virtual int start() { return pthread->start(); }
 *               public: virtual void stop() { pthread->stop(); }
 *               public: virtual int cycle() {
 *                   // check status, start ffmpeg when stopped.
 *               }
 *           };
 */
 //可以重复使用的thread, 被其他thread启动和停止
class ISrsReusableThreadHandler
{
public:
    ISrsReusableThreadHandler();
    virtual ~ISrsReusableThreadHandler();
public:
    /**
     * the cycle method for the one cycle thread.
     * @remark when the cycle has its inner loop, it must check whether
     * the thread is interrupted.
     */
    virtual int cycle() = 0;
public:
    /**
     * other callback for handler.
     * @remark all callback is optional, handler can ignore it.
     */
    virtual void on_thread_start();
    virtual int on_before_cycle();
    virtual int on_end_cycle();
    virtual void on_thread_stop();
};
class SrsReusableThread : public internal::ISrsThreadHandler
{
private:
    internal::SrsThread* pthread;
    ISrsReusableThreadHandler* handler;
public:
    SrsReusableThread(const char* n, ISrsReusableThreadHandler* h, int64_t interval_us = 0);
    virtual ~SrsReusableThread();
public:
    /**
     * for the reusable thread, start and stop by user.
     */
    virtual int start();
    /**
     * stop the thread, wait for the thread to terminate.
     * @remark user can stop multiple times, ignore if already stopped.
     */
    virtual void stop();
public:
    /**
     * get the context id. @see: ISrsThreadContext.get_id().
     * used for parent thread to get the id.
     * @remark when start thread, parent thread will block and wait for this id ready.
     */
    virtual int cid();
// interface internal::ISrsThreadHandler
public:
    virtual int cycle();
    virtual void on_thread_start();
    virtual int on_before_cycle();
    virtual int on_end_cycle();
    virtual void on_thread_stop();
};

/**
 * the reuse thread is a thread stop and start by other thread.
 * the version 2, is the thread cycle has its inner loop, which should
 * check the interrupt, and should interrupt thread when the inner loop want
 * to quit the thread.
 *       user can create thread and stop then start again and again,
 *       generally must provides a start and stop method, @see SrsIngester.
 *       the step to create a thread stop by other thread:
 *       1. create SrsReusableThread field.
 *       2. must manually stop the thread when started it.
 *       for example:
 *           class SrsIngester : public ISrsReusableThreadHandler {
 *               public: SrsIngester() { pthread = new SrsReusableThread("ingest", this, SRS_AUTO_INGESTER_SLEEP_US); }
 *               public: virtual int start() { return pthread->start(); }
 *               public: virtual void stop() { pthread->stop(); }
 *               public: virtual int cycle() {
 *                  while (!pthread->interrupted()) {
 *                      // quit thread when error.
 *                      if (ret != ERROR_SUCCESS) {
 *                          pthread->interrupt();
 *                      }
 *
 *                      // do something.
 *                  }
 *               }
 *           };
 */
class ISrsReusableThread2Handler
{
public:
    ISrsReusableThread2Handler();
    virtual ~ISrsReusableThread2Handler();
public:
    /**
     * the cycle method for the one cycle thread.
     * @remark when the cycle has its inner loop, it must check whether
     * the thread is interrupted.
     */
    virtual int cycle() = 0;
public:
    /**
     * other callback for handler.
     * @remark all callback is optional, handler can ignore it.
     */
    virtual void on_thread_start();
    virtual int on_before_cycle();
    virtual int on_end_cycle();
    virtual void on_thread_stop();
};
//需要在循环里检查错误
class SrsReusableThread2 : public internal::ISrsThreadHandler
{
private:
    internal::SrsThread* pthread;
    ISrsReusableThread2Handler* handler;
public:
    SrsReusableThread2(const char* n, ISrsReusableThread2Handler* h, int64_t interval_us = 0);
    virtual ~SrsReusableThread2();
public:
    /**
     * for the reusable thread, start and stop by user.
     */
    virtual int start();
    /**
     * stop the thread, wait for the thread to terminate.
     * @remark user can stop multiple times, ignore if already stopped.
     */
    virtual void stop();
public:
    /**
     * get the context id. @see: ISrsThreadContext.get_id().
     * used for parent thread to get the id.
     * @remark when start thread, parent thread will block and wait for this id ready.
     */
    virtual int cid();
    /**
     * interrupt the thread to stop loop.
     * we only set the loop flag to false, not really interrupt the thread.
     */
    virtual void interrupt();
    /**
     * whether the thread is interrupted,
     * for the cycle has its loop, the inner loop should quit when thread
     * is interrupted.
     */
    virtual bool interrupted();
// interface internal::ISrsThreadHandler
public:
    virtual int cycle();
    virtual void on_thread_start();
    virtual int on_before_cycle();
    virtual int on_end_cycle();
    virtual void on_thread_stop();
};

#endif

