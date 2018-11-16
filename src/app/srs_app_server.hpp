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

#ifndef SRS_APP_SERVER_HPP
#define SRS_APP_SERVER_HPP

/*
#include <srs_app_server.hpp>
*/

#include <srs_core.hpp>

#include <vector>
#include <string>

#include <srs_app_st.hpp>
#include <srs_app_reload.hpp>
#include <srs_app_source.hpp>
#include <srs_app_hls.hpp>
#include <srs_app_listener.hpp>
#include <srs_app_conn.hpp>

class SrsServer;
class SrsConnection;
class SrsHttpServeMux;
class SrsHttpServer;
class SrsIngester;
class SrsHttpHeartbeat;
class SrsKbps;
class SrsConfDirective;
class ISrsTcpHandler;
class ISrsUdpHandler;
class SrsUdpListener;
class SrsTcpListener;
#ifdef SRS_AUTO_STREAM_CASTER
class SrsAppCasterFlv;
#endif

// listener type for server to identify the connection,
// that is, use different type to process the connection.
//监听类型
enum SrsListenerType 
{
    // RTMP client,
    SrsListenerRtmpStream       = 0,
    // HTTP api,
    SrsListenerHttpApi          = 1,
    // HTTP stream, HDS/HLS/DASH
    SrsListenerHttpStream       = 2,
    // UDP stream, MPEG-TS over udp.
    SrsListenerMpegTsOverUdp    = 3,
    // TCP stream, RTSP stream.
    SrsListenerRtsp             = 4,
    // TCP stream, FLV stream over HTTP.
    SrsListenerFlv              = 5,
};

/**
* the common tcp listener, for RTMP/HTTP server.
*/
//listener
class SrsListener
{
protected:
    SrsListenerType type; //类型
protected:
    std::string ip; //ip
    int port; //端口
    SrsServer* server; //所属的服务
public:
    SrsListener(SrsServer* svr, SrsListenerType t);
    virtual ~SrsListener();
public:
    virtual SrsListenerType listen_type();
    virtual int listen(std::string i, int p) = 0; //监听
};

/**
* tcp listener.
*/
//tcp监听，继承SrsListener和ISrsTcpHandler
class SrsStreamListener : virtual public SrsListener, virtual public ISrsTcpHandler
{
private:
    SrsTcpListener* listener; //包含一个监听
public:
    SrsStreamListener(SrsServer* server, SrsListenerType type);
    virtual ~SrsStreamListener();
public:
    virtual int listen(std::string ip, int port); //listen
// ISrsTcpHandler
public:
    virtual int on_tcp_client(st_netfd_t stfd); //连接到来的处理
};

#ifdef SRS_AUTO_STREAM_CASTER
/**
* the tcp listener, for rtsp server.
*/
class SrsRtspListener : virtual public SrsListener, virtual public ISrsTcpHandler
{
private:
    SrsTcpListener* listener;
    ISrsTcpHandler* caster;
public:
    SrsRtspListener(SrsServer* svr, SrsListenerType t, SrsConfDirective* c);
    virtual ~SrsRtspListener();
public:
    virtual int listen(std::string i, int p);
// ISrsTcpHandler
public:
    virtual int on_tcp_client(st_netfd_t stfd);
};

/**
 * the tcp listener, for flv stream server.
 */
class SrsHttpFlvListener : virtual public SrsListener, virtual public ISrsTcpHandler
{
private:
    SrsTcpListener* listener;
    SrsAppCasterFlv* caster;
public:
    SrsHttpFlvListener(SrsServer* svr, SrsListenerType t, SrsConfDirective* c);
    virtual ~SrsHttpFlvListener();
public:
    virtual int listen(std::string i, int p);
// ISrsTcpHandler
public:
    virtual int on_tcp_client(st_netfd_t stfd);
};
#endif

/**
 * the udp listener, for udp server.
 */
class SrsUdpStreamListener : public SrsListener
{
protected:
    SrsUdpListener* listener;
    ISrsUdpHandler* caster;
public:
    SrsUdpStreamListener(SrsServer* svr, SrsListenerType t, ISrsUdpHandler* c);
    virtual ~SrsUdpStreamListener();
public:
    virtual int listen(std::string i, int p);
};

/**
 * the udp listener, for udp stream caster server.
 */
#ifdef SRS_AUTO_STREAM_CASTER
class SrsUdpCasterListener : public SrsUdpStreamListener
{
public:
    SrsUdpCasterListener(SrsServer* svr, SrsListenerType t, SrsConfDirective* c);
    virtual ~SrsUdpCasterListener();
};
#endif

/**
* convert signal to io,
* @see: st-1.9/docs/notes.html
*/
//信号管理类，继承ISrsEndlessThreadHandler
class SrsSignalManager : public ISrsEndlessThreadHandler
{
private:
    /* Per-process pipe which is used as a signal queue. */
    /* Up to PIPE_BUF/sizeof(int) signals can be queued up. */
    int sig_pipe[2]; //用于写入的管道
    st_netfd_t signal_read_stfd; //读取的fd封装为stfd
private:
    SrsServer* _server; //所属的server
    SrsEndlessThread* pthread; //协程
public:
    SrsSignalManager(SrsServer* server);
    virtual ~SrsSignalManager();
public:
    virtual int initialize(); //初始化
    virtual int start(); //启动协程
// interface ISrsEndlessThreadHandler.
public:
    virtual int cycle(); //循环
private:
    // global singleton instance
    static SrsSignalManager* instance; //全局的单例
    /* Signal catching function. */
    /* Converts signal event to I/O event. */
    static void sig_catcher(int signo); //将信号事件转为io
};

/**
* the handler to the handle cycle in SRS RTMP server.
*/
//服务循环
class ISrsServerCycle
{
public:
    ISrsServerCycle();
    virtual ~ISrsServerCycle();
public: 
    /**
    * initialize the cycle handler.
    */
    virtual int initialize() = 0;
    /**
    * do on_cycle while server doing cycle.
    */
    virtual int on_cycle(int connections) = 0;
};

/**
* SRS RTMP server, initialize and listen, 
* start connection service thread, destroy client.
*/
//SRS RTMP server类：初始化，监听，启动连接服务现场，销毁客户端连接
class SrsServer : virtual public ISrsReloadHandler
    , virtual public ISrsSourceHandler
    , virtual public IConnectionManager
{
private:
#ifdef SRS_AUTO_HTTP_API
    // TODO: FIXME: rename to http_api
    SrsHttpServeMux* http_api_mux; //http api
#endif
#ifdef SRS_AUTO_HTTP_SERVER
    SrsHttpServer* http_server; //http server
#endif
#ifdef SRS_AUTO_HTTP_CORE
    SrsHttpHeartbeat* http_heartbeat;
#endif
#ifdef SRS_AUTO_INGEST
    SrsIngester* ingester; ////推流给SRS服务器
#endif
private:
    /**
    * the pid file fd, lock the file write when server is running.
    * @remark the init.d script should cleanup the pid file, when stop service,
    *       for the server never delete the file; when system startup, the pid in pid file
    *       maybe valid but the process is not SRS, the init.d script will never start server.
    */
    int pid_fd;//pid文件的fd
    /**
    * all connections, connection manager
    */
    std::vector<SrsConnection*> conns; //所有的连接
    /**
    * all listners, listener manager.
    */
    std::vector<SrsListener*> listeners; //所有的监听
    /**
    * signal manager which convert gignal to io message.
    */
    SrsSignalManager* signal_manager; //信号管理
    /**
    * handle in server cycle.
    */
    ISrsServerCycle* handler; //循环的handler
    /**
    * user send the signal, convert to variable.
    */
    //信号转为bool值
    bool signal_reload;
    bool signal_gmc_stop;
    bool signal_gracefully_quit;
    // parent pid for asprocess.
    int ppid;
public:
    SrsServer();
    virtual ~SrsServer();
private:
    /**
    * the destroy is for gmc to analysis the memory leak,
    * if not destroy global/static data, the gmc will warning memory leak.
    * in service, server never destroy, directly exit when restart.
    */
    virtual void destroy();//销毁
    /**
     * when SIGTERM, SRS should do cleanup, for example, 
     * to stop all ingesters, cleanup HLS and dvr.
     */
    virtual void dispose();//清理
// server startup workflow, @see run_master()
public:
    /**
     * initialize server with callback handler.
     * @remark user must free the cycle handler.
     */
    virtual int initialize(ISrsServerCycle* cycle_handler);//初始化server
    virtual int initialize_st();//初始化st
    virtual int initialize_signal();//初始化信号
    virtual int acquire_pid_file();// 获取pid并写入文件
    virtual int listen();//开始监听
    virtual int register_signal();//注册信号
    virtual int http_handle();//http处理
    virtual int ingest();//其他流包装为rtmp相关
    virtual int cycle();//循环
// IConnectionManager
public:
    /**
    * callback for connection to remove itself.
    * when connection thread cycle terminated, callback this to delete connection.
    * @see SrsConnection.on_thread_stop().
    */
    //移除连接
    virtual void remove(SrsConnection* conn);
// server utilities.
public:
    /**
    * callback for signal manager got a signal.
    * the signal manager convert signal to io message,
    * whatever, we will got the signo like the orignal signal(int signo) handler.
    * @remark, direclty exit for SIGTERM.
    * @remark, do reload for SIGNAL_RELOAD.
    * @remark, for SIGINT and SIGUSR2:
    *       no gmc, directly exit.
    *       for gmc, set the variable signal_gmc_stop, the cycle will return and cleanup for gmc.
    */
    //由signal manager类调用，signal manager将信号转为message
    virtual void on_signal(int signo);
private:
    /**
    * the server thread main cycle,
    * update the global static data, for instance, the current time,
    * the cpu/mem/network statistic.
    */
    virtual int do_cycle(); //server线程的主循环，更新系统统计信息
    /**
    * listen at specified protocol.
    */
    //监听各种协议流
    virtual int listen_rtmp();
    virtual int listen_http_api();
    virtual int listen_http_stream();
    virtual int listen_stream_caster();
    /**
    * close the listeners for specified type, 
    * remove the listen object from manager.
    */
    virtual void close_listeners(SrsListenerType type); //关闭监听
    /**
    * resample the server kbs.
    */
    virtual void resample_kbps(); //获取server的kbps
// internal only
public:
    /**
    * when listener got a fd, notice server to accept it.
    * @param type, the client type, used to create concrete connection, 
    *       for instance RTMP connection to serve client.
    * @param client_stfd, the client fd in st boxed, the underlayer fd.
    */
    //接受连接
    virtual int accept_client(SrsListenerType type, st_netfd_t client_stfd);
// interface ISrsReloadHandler.
public:
    //reload
    virtual int on_reload_listen();
    virtual int on_reload_pid();
    virtual int on_reload_vhost_added(std::string vhost);
    virtual int on_reload_vhost_removed(std::string vhost);
    virtual int on_reload_http_api_enabled();
    virtual int on_reload_http_api_disabled();
    virtual int on_reload_http_stream_enabled();
    virtual int on_reload_http_stream_disabled();
    virtual int on_reload_http_stream_updated();
// interface ISrsSourceHandler
public:
    //publish流
    virtual int on_publish(SrsSource* s, SrsRequest* r);
    virtual void on_unpublish(SrsSource* s, SrsRequest* r);
};

#endif

