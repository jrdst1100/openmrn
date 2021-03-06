/** @copyright
 * Copyright (c) 2013, Stuart W Baker
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are  permitted provided that the following conditions are met:
 *
 *  - Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *
 *  - Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * @file SocketClient.hxx
 *
 * Connects to a remote TCP socket.
 *
 * @author Stuart Baker
 * @date 11 March 2017
 */

#ifndef _UTILS_SOCKET_CLIENT_HXX_
#define _UTILS_SOCKET_CLIENT_HXX_

#include <functional>
#include <netdb.h>
#include <arpa/inet.h>
#include <fcntl.h>

#include "executor/StateFlow.hxx"
#include "executor/Timer.hxx"
#include "os/MDNS.hxx"
#include "utils/Atomic.hxx"
#include "utils/format_utils.hxx"

class SocketClient : public StateFlowBase, private OSThread, private Atomic
{
public:
    /** Constructor.
     * @param service service that the StateFlowBase will be bound to.
     * @param mdns service name to connect to, nullptr to force use
     *                  hostname and port
     * @param host host to connect to if mdns_name resolution fails,
     *             nullptr to force use mDNS
     * @param port port number to connect to if mdns_name resolution fails
     * @param callback callback method to invoke when a client connection is
     *                 made successfully.  It is the responsibility of the
     *                 callee to register the notifiable (on close) if this
     *                 client shall reattempt the connection if the socket
     *                 is ever closed.
     *                 - First param is the file descriptor of the resulting
     *                   socket
     *                 - Second param is the struct addrinfo for the connected
     *                   peer
     *                 - Third param is a pointer to "this" class to notify
     *                   on exit that the socket has been torn down.
     * @param retry_seconds time in seconds that the client shall wait to retry
     *                      connecting on error.
     * @param timeout_seconds time in seconds that the connect is supposed to
     *                        timeout and look for a possible shutdown.
     */
    SocketClient(Service *service, const char *mdns, const char *host,
                 uint16_t port,
                 std::function<void(int, struct addrinfo *, Notifiable*)>callback,
                 uint8_t retry_seconds = 5, uint8_t timeout_seconds = 255)
        : StateFlowBase(service)
        , OSThread()
        , mdns_(mdns)
        , host_(host)
        , port_(port)
        , callback_(callback)
        , retrySeconds_(retry_seconds)
        , timeoutSeconds_(timeout_seconds)
        , state_(STATE_CREATED)
        , fd_(-1)
        , addr_(nullptr)
        , sem_()
    {
        HASSERT(mdns_ || (host_ && port_));
        start_flow(STATE(spawn_thread));
    }

    /** Destructor.
     */
    ~SocketClient()
    {
        shutdown();
        if (addr_)
        {
            freeaddrinfo(addr_);
        }
    }

    /** Shutdown the client so that it can be deleted.
     */
    void shutdown()
    {
        {
            AtomicHolder h(this);
            if (state_ == STATE_SHUTDOWN)
            {
                return;
            }
            state_ = STATE_SHUTDOWN_REQUESTED;
        }
        sem_.post();
        while (state_ != STATE_SHUTDOWN)
        {
            usleep(1000);
        }
    }

    /** Connects a tcp socket to the specified remote host:port. Returns -1 if
     *  unsuccessful; returns the fd if successful.
     *
     *  @param host hostname to connect to
     *  @param port TCP port number to connect to
     *
     *  @return fd of the connected socket.
     */
    static int connect(const char *host, int port);

    /** Connects a tcp socket to the specified remote host:port. Returns -1 if
     *  unsuccessful; returns the fd if successful.
     *
     *  @param host hostname to connect to. Shall be null for mDNS target.
     *  @param port_str TCP port number or mDNS hostname to connect to.
     *
     *  @return fd of the connected socket.
     */
    static int connect(const char *host, const char* port_str);

private:
    /** Execution state.
     */
    enum State
    {
        STATE_CREATED = 0, /**< constructed */
        STATE_STARTED,     /**< thread started */
        STATE_SHUTDOWN_REQUESTED, /**< shutdown requested */
        STATE_SHUTDOWN, /**< shutdown */
    };

    /** thread that will handle the blocking address resolution.
     * @return should never return
     */
    void *entry() override
    {
        int ai_ret = -1;
        struct addrinfo hints;

        memset(&hints, 0, sizeof(hints));
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_flags = 0;
        hints.ai_protocol = IPPROTO_TCP;

        sem_.wait();

        for ( ; /* forever */ ; )
        {
            {
                AtomicHolder h(this);
                switch (state_)
                {
                    case STATE_CREATED:
                        state_ = STATE_STARTED;
                    case STATE_STARTED:
                        break;
                    case STATE_SHUTDOWN_REQUESTED:
                        state_ = STATE_SHUTDOWN;
                    case STATE_SHUTDOWN:
                        return nullptr;
                }
            }
            long long start_time = OSTime::get_monotonic();

            if (mdns_)
            {
                LOG(INFO, "mdns lookup for %s", mdns_);
                /* try mDNS address resolution */
                ai_ret = MDNS::lookup(mdns_, &hints, &addr_);
                if (ai_ret != 0 || addr_ == nullptr) {
                    LOG(INFO, "mdns lookup for %s failed.", mdns_);
                }
            }
            if ((ai_ret != 0 || addr_ == nullptr) && host_)
            {
                /* try address resolution without mDNS */
                char port_str[30];
                integer_to_buffer(port_, port_str);
                ai_ret = getaddrinfo(host_, port_str, &hints, &addr_);
            }

            if (ai_ret == 0 && addr_)
            {
                /* able to resolve the hostname */
                fd_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
                if (fd_ >= 0)
                {
                    {
                        struct timeval tm;
                        tm.tv_sec = timeoutSeconds_;
                        tm.tv_usec = 0;
                        ERRNOCHECK("setsockopt_timeout", setsockopt(fd_,
                                                                    SOL_SOCKET,
                                                                    SO_RCVTIMEO,
                                                                    &tm,
                                                                    sizeof(tm))
                                  );
                    }
                    /* socket available */
                    int ret = ::connect(fd_, addr_->ai_addr, addr_->ai_addrlen);
                    if (ret == 0)
                    {
                        /* connect successful */
                        notify();
                        sem_.wait();
                    }
                    else
                    {
                        /* connect failed */
                        close(fd_);
                    }
                }
            }

            if (addr_)
            {
                freeaddrinfo(addr_);
                addr_ = nullptr;
            }

            long long diff_time = OSTime::get_monotonic() - start_time;
            if (NSEC_TO_SEC(diff_time) < retrySeconds_)
            {
                sleep(retrySeconds_ - NSEC_TO_SEC(diff_time));
            }
        }

        /* should never get here */
        return nullptr;
    }

    /** Entry point into the state flow.
     * @return next state is do_connect()
     */
    Action spawn_thread()
    {
        start("socket_client", 0, 1536);
        return call_immediately(STATE(do_connect));
    }

    /** Kick off connection attempt.
     * @return next state is connected() upon successful connection
     */
    Action do_connect()
    {
        sem_.post();
        return wait_and_call(STATE(connected));
    }

    /** Connected successfully, notify user through a callback.
     * @return next state is do_connect() after the connection has been broken
     */
    Action connected()
    {
        /* connect successful */
        callback_(fd_, addr_, this);
        return wait_and_call(STATE(do_connect));
    }

    /** mDNS service name */
    const char *mdns_;

    /** hostname */
    const char *host_;

    /** port number */
    int port_;

    /** callback to call on connection success */
    std::function<void(int, struct addrinfo *, Notifiable*)> callback_;

    /** number of seconds between retries */
    uint8_t retrySeconds_;
    uint8_t timeoutSeconds_;

    volatile State state_;

    /** socket descriptor */
    int fd_;

    /** address info metadata */
    struct addrinfo *addr_;

    /** Semaphore for synchronizing with the helper thread */
    OSSem sem_;

    DISALLOW_COPY_AND_ASSIGN(SocketClient);
};

#endif /* _UTILS_SOCKET_CLIENT_HXX */

