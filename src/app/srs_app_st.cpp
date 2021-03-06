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

#include <srs_app_st.hpp>

#include <srs_kernel_error.hpp>
#include <srs_kernel_log.hpp>

//构造函数：传入客户端的fd
SrsStSocket::SrsStSocket(st_netfd_t client_stfd)
{
    stfd = client_stfd;
    //超时时间默认为永不超时
    send_timeout = recv_timeout = ST_UTIME_NO_TIMEOUT;
    recv_bytes = send_bytes = 0;
}

SrsStSocket::~SrsStSocket()
{
}

/*
 * 超时时间相关函数
 * 超时时间用于在st库读写时传入，决定st库读写的超时。如果没有设置超时
 * 时间，那么默认为永不超时，即一直等待读或写。
 * */
bool SrsStSocket::is_never_timeout(int64_t timeout_us)
{
    return timeout_us == (int64_t)ST_UTIME_NO_TIMEOUT;
}

void SrsStSocket::set_recv_timeout(int64_t timeout_us)
{
    recv_timeout = timeout_us;
}

int64_t SrsStSocket::get_recv_timeout()
{
    return recv_timeout;
}

void SrsStSocket::set_send_timeout(int64_t timeout_us)
{
    send_timeout = timeout_us;
}

int64_t SrsStSocket::get_send_timeout()
{
    return send_timeout;
}

int64_t SrsStSocket::get_recv_bytes()
{
    return recv_bytes;
}

int64_t SrsStSocket::get_send_bytes()
{
    return send_bytes;
}

//从stfd读取size个字节到buf，nread为读取的字节数
int SrsStSocket::read(void* buf, size_t size, ssize_t* nread)
{
    int ret = ERROR_SUCCESS;
    //st_read读取，传入recv_timeout
    ssize_t nb_read = st_read(stfd, buf, size, recv_timeout);
    if (nread) {
        *nread = nb_read;
    }
    
    // On success a non-negative integer indicating the number of bytes actually read is returned
    // (a value of 0 means the network connection is closed or end of file is reached).
    // Otherwise, a value of -1 is returned and errno is set to indicate the error.
    //出错处理处理
    if (nb_read <= 0) {
        // @see https://github.com/ossrs/srs/issues/200
        if (nb_read < 0 && errno == ETIME) { //超时
            return ERROR_SOCKET_TIMEOUT;
        }
        
        if (nb_read == 0) { //断开连接
            errno = ECONNRESET;
        }
        
        return ERROR_SOCKET_READ; //读取错误
    }
    
    recv_bytes += nb_read; //读取到的字节增加nb_read
    
    return ret;
}
//一次读取完
int SrsStSocket::read_fully(void* buf, size_t size, ssize_t* nread)
{
    int ret = ERROR_SUCCESS;
    //st_read_fully读取所有的字节
    ssize_t nb_read = st_read_fully(stfd, buf, size, recv_timeout);
    if (nread) {
        *nread = nb_read;
    }
    
    // On success a non-negative integer indicating the number of bytes actually read is returned
    // (a value less than nbyte means the network connection is closed or end of file is reached)
    // Otherwise, a value of -1 is returned and errno is set to indicate the error.
    if (nb_read != (ssize_t)size) {
        // @see https://github.com/ossrs/srs/issues/200
        if (nb_read < 0 && errno == ETIME) {
            return ERROR_SOCKET_TIMEOUT;
        }
        
        if (nb_read >= 0) {
            errno = ECONNRESET;
        }
        
        return ERROR_SOCKET_READ_FULLY;
    }
    
    recv_bytes += nb_read;
    
    return ret;
}

//将buf中的size个字节写入到stfd, nwrite为写入的字节数
int SrsStSocket::write(void* buf, size_t size, ssize_t* nwrite)
{
    int ret = ERROR_SUCCESS;
    
    ssize_t nb_write = st_write(stfd, buf, size, send_timeout);
    if (nwrite) {
        *nwrite = nb_write;
    }
    
    // On success a non-negative integer equal to nbyte is returned.
    // Otherwise, a value of -1 is returned and errno is set to indicate the error.
    if (nb_write <= 0) {
        // @see https://github.com/ossrs/srs/issues/200
        if (nb_write < 0 && errno == ETIME) {
            return ERROR_SOCKET_TIMEOUT;
        }
        
        return ERROR_SOCKET_WRITE;
    }
    
    send_bytes += nb_write;
    
    return ret;
}
//将iov_size个iov写入到stfd, nwrite为写入的个数
int SrsStSocket::writev(const iovec *iov, int iov_size, ssize_t* nwrite)
{
    int ret = ERROR_SUCCESS;
    
    ssize_t nb_write = st_writev(stfd, iov, iov_size, send_timeout);
    if (nwrite) {
        *nwrite = nb_write;
    }
    
    // On success a non-negative integer equal to nbyte is returned.
    // Otherwise, a value of -1 is returned and errno is set to indicate the error.
    if (nb_write <= 0) {
        // @see https://github.com/ossrs/srs/issues/200
        if (nb_write < 0 && errno == ETIME) {
            return ERROR_SOCKET_TIMEOUT;
        }
        
        return ERROR_SOCKET_WRITE;
    }
    
    send_bytes += nb_write;
    
    return ret;
}

#ifdef __linux__
#include <sys/epoll.h>

bool srs_st_epoll_is_supported(void)
{
    struct epoll_event ev;

    ev.events = EPOLLIN;
    ev.data.ptr = NULL;
    /* Guaranteed to fail */
    epoll_ctl(-1, EPOLL_CTL_ADD, -1, &ev);

    return (errno != ENOSYS);
}
#endif

//初始化st,linux下使用epoll
int srs_st_init()
{
    int ret = ERROR_SUCCESS;
    
#ifdef __linux__
    // check epoll, some old linux donot support epoll.
    // @see https://github.com/ossrs/srs/issues/162
    if (!srs_st_epoll_is_supported()) {
        ret = ERROR_ST_SET_EPOLL;
        srs_error("epoll required on Linux. ret=%d", ret);
        return ret;
    }
#endif
    
    // Select the best event system available on the OS. In Linux this is
    // epoll(). On BSD it will be kqueue.
    //设置为epoll模式
    if (st_set_eventsys(ST_EVENTSYS_ALT) == -1) {
        ret = ERROR_ST_SET_EPOLL;
        srs_error("st_set_eventsys use %s failed. ret=%d", st_get_eventsys_name(), ret);
        return ret;
    }
    srs_trace("st_set_eventsys to %s", st_get_eventsys_name());
    //初始协程
    if(st_init() != 0){
        ret = ERROR_ST_INITIALIZE;
        srs_error("st_init failed. ret=%d", ret);
        return ret;
    }
    srs_trace("st_init success, use %s", st_get_eventsys_name());
    
    return ret;
}

//关闭fd
void srs_close_stfd(st_netfd_t& stfd)
{
    if (stfd) {
        // we must ensure the close is ok.
        int err = st_netfd_close(stfd);
        srs_assert(err != -1);
        stfd = NULL;
    }
}

