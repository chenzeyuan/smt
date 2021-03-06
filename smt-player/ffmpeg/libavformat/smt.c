/*
 * SMT prototype streaming system
 * Copyright (c) 2000, 2001, 2002 Fabrice Bellard
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

/**
 * @file
 * SMT protocol
 */

#define _DEFAULT_SOURCE
#define _BSD_SOURCE     /* Needed for using struct ip_mreq with recent glibc */

#include "avformat.h"
#include "libavutil/opt.h"
#include "libavutil/log.h"
#include "libavutil/fifo.h"
#include "libavutil/time.h"
#include "network.h"
#include "url.h"
#include "smt_proto.h"

#include <pthread.h>



#define SMT_MAX_PKT_SIZE 65536
#define SMT_DATA_NOT_READY -11

#define SMT_NO_AUDIO 0
#define SMT_NO_VIDEO 0



typedef struct SMTContext {
    const AVClass *class;
    int smt_fd;
    int buffer_size;
    int pkt_size;
    int is_multicast;
    int is_broadcast;
    int local_port;
    int reuse_socket;
    char *localaddr;
    int is_connected;
    char *sources;
    struct sockaddr_storage dest_addr;
    int dest_addr_len;
    struct sockaddr_storage local_addr_storage;

    int fifo_size;
    AVFifoBuffer *fifo, *head, *cache;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    pthread_t mpu_generate_thread;
    bool generate_thread_run;
    bool hflag;
    int stream_index;
    int audio_head_available, video_head_available;
    smt_receive_entity *receive;
    smt_send_entity *send;
} SMTContext;

static unsigned int consumption_length = 0;

static void smt_on_get_mpu(URLContext *h, smt_mpu *mpu)
{
    SMTContext *s = h->priv_data;
    int free_space_in_fifo;
    //if(!mpu->asset)
        //return;
    if(!s->audio_head_available && mpu->asset == 0){
        if(!s->head)
            s->head = av_fifo_alloc(mpu->mpu_header_length);
        free_space_in_fifo = av_fifo_space(s->head);
        if(free_space_in_fifo < mpu->mpu_header_length)
            av_fifo_grow(s->head, mpu->mpu_header_length - free_space_in_fifo);
        av_fifo_generic_write(s->head, mpu->mpu_header_data, mpu->mpu_header_length, NULL);
        s->audio_head_available = 1;
#if SMT_NO_VIDEO
        s->video_head_available = 1;  //fake video
#endif
    }

    if(!s->video_head_available && mpu->asset == 1){
        if(!s->head)
            s->head = av_fifo_alloc(mpu->mpu_header_length);
        free_space_in_fifo = av_fifo_space(s->head);
        if(free_space_in_fifo < mpu->mpu_header_length)
            av_fifo_grow(s->head, mpu->mpu_header_length - free_space_in_fifo);
        av_fifo_generic_write(s->head, mpu->mpu_header_data, mpu->mpu_header_length, NULL);
        s->video_head_available = 1;
#if SMT_NO_AUDIO
        s->audio_head_available = 1;  //fake audio
#endif

    }
    
    if(!s->fifo)
        s->fifo = av_fifo_alloc(s->fifo_size);
    free_space_in_fifo = av_fifo_space(s->fifo);
    pthread_mutex_lock(&s->mutex);
    if(free_space_in_fifo < mpu->moof_header_length + mpu->sample_length)
        av_fifo_grow(s->fifo, mpu->moof_header_length + mpu->sample_length - free_space_in_fifo);
    av_fifo_generic_write(s->fifo, mpu->moof_header_data, mpu->moof_header_length, NULL);
    av_fifo_generic_write(s->fifo, mpu->sample_data, mpu->sample_length, NULL); 
    pthread_cond_signal(&s->cond);
    pthread_mutex_unlock(&s->mutex);
    smt_release_mpu(h, mpu);
    //av_log(NULL, AV_LOG_INFO, "fifo size: %d, fifo space: %d\n", av_fifo_size(s->fifo),  av_fifo_space(s->fifo));
}

static void smt_on_get_sig(URLContext *h, smt_sig *sig)
{

}

static void smt_on_get_gfd(URLContext *h, smt_sig *sig)
{

}

static void smt_on_get_id(URLContext *h, smt_sig *sig)
{

}

static int smt_on_packet_deliver(URLContext *h, unsigned char *buf, int len)
{
    SMTContext *s = h->priv_data;
    int ret;

    if (!s->is_connected) {
        ret = sendto (s->smt_fd, buf, len, 0,
                      (struct sockaddr *) &s->dest_addr,
                      s->dest_addr_len);
    } else
        ret = send(s->smt_fd, buf, len, 0);
    av_usleep(100); //DO NOT SEND PKT TOO FAST
    return ret < 0 ? ff_neterrno() : ret;

}


static void *smt_mpu_generate_task( void *_URLContext)
{
    URLContext *h = (URLContext *)_URLContext;
    SMTContext *s = h->priv_data;
    while(s->generate_thread_run){
        unsigned char buf[MTU];
        int len;
        memset(buf, 0, MTU);
        len = recv(s->smt_fd, buf, MTU, 0);
        if(len <= 0)
            break;
        if(!s->receive)
            s->receive = (smt_receive_entity *)av_mallocz(sizeof(smt_receive_entity));
        smt_parse(h, s->receive, buf, &len);
    }

    return  NULL;
}


static void log_net_error(void *ctx, int level, const char* prefix)
{
    char errbuf[100];
    av_strerror(ff_neterrno(), errbuf, sizeof(errbuf));
    av_log(ctx, level, "%s: %s\n", prefix, errbuf);
}


static int smt_port(struct sockaddr_storage *addr, int addr_len)
{
    char sbuf[sizeof(int)*3+1];
    int error;

    if ((error = getnameinfo((struct sockaddr *)addr, addr_len, NULL, 0,  sbuf, sizeof(sbuf), NI_NUMERICSERV)) != 0) {
        av_log(NULL, AV_LOG_ERROR, "getnameinfo: %s\n", gai_strerror(error));
        return -1;
    }

    return strtol(sbuf, NULL, 10);
}



#define OFFSET(x) offsetof(SMTContext, x)
#define D AV_OPT_FLAG_DECODING_PARAM
#define E AV_OPT_FLAG_ENCODING_PARAM
static const AVOption options[] = {
    { "buffer_size",    "System data size (in bytes)",                     OFFSET(buffer_size),    AV_OPT_TYPE_INT,    { .i64 = -1 },    -1, INT_MAX, .flags = D|E },
    { "localport",      "Local port",                                      OFFSET(local_port),     AV_OPT_TYPE_INT,    { .i64 = -1 },    -1, INT_MAX, D|E },
    { "local_port",     "Local port",                                      OFFSET(local_port),     AV_OPT_TYPE_INT,    { .i64 = -1 },    -1, INT_MAX, .flags = D|E },
    { "localaddr",      "Local address",                                   OFFSET(localaddr),      AV_OPT_TYPE_STRING, { .str = NULL },               .flags = D|E },
    { "pkt_size",       "Maximum UDP packet size",                         OFFSET(pkt_size),       AV_OPT_TYPE_INT,    { .i64 = 1472 },  -1, INT_MAX, .flags = D|E },
    { "reuse",          "explicitly allow reusing UDP sockets",            OFFSET(reuse_socket),   AV_OPT_TYPE_BOOL,   { .i64 = -1 },    -1, 1,       D|E },
    { "reuse_socket",   "explicitly allow reusing UDP sockets",            OFFSET(reuse_socket),   AV_OPT_TYPE_BOOL,   { .i64 = -1 },    -1, 1,       .flags = D|E },
    { "broadcast", "explicitly allow or disallow broadcast destination",   OFFSET(is_broadcast),   AV_OPT_TYPE_BOOL,   { .i64 = 0  },     0, 1,       E },
    { "connect",        "set if connect() should be called on socket",     OFFSET(is_connected),   AV_OPT_TYPE_BOOL,   { .i64 =  0 },     0, 1,       .flags = D|E },
    { "sources",        "Source list",                                     OFFSET(sources),        AV_OPT_TYPE_STRING, { .str = NULL },               .flags = D|E },
    { "fifo_size",      "set the SMT receiving buffer size, expressed as a number of MTU", OFFSET(fifo_size), AV_OPT_TYPE_INT, {.i64 = 8*4096}, 0, INT_MAX, D },
    { NULL }
};



static const AVClass smt_class = {
    .class_name = "smt",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};

static struct addrinfo *smt_resolve_host(URLContext *h,
                                         const char *hostname, int port,
                                         int type, int family, int flags)
{
    struct addrinfo hints = { 0 }, *res = 0;
    int error;
    char sport[16];
    const char *node = 0, *service = "0";

    if (port > 0) {
        snprintf(sport, sizeof(sport), "%d", port);
        service = sport;
    }
    if ((hostname) && (hostname[0] != '\0') && (hostname[0] != '?')) {
        node = hostname;
    }
    hints.ai_socktype = type;
    hints.ai_family   = family;
    hints.ai_flags = flags;
    if ((error = getaddrinfo(node, service, &hints, &res))) {
        res = NULL;
        av_log(h, AV_LOG_ERROR, "getaddrinfo(%s, %s): %s\n",
               node ? node : "unknown",
               service ? service : "unknown",
               gai_strerror(error));
    }

    return res;
}


static int smt_set_url(URLContext *h,
                       struct sockaddr_storage *addr,
                       const char *hostname, int port)
{
    struct addrinfo *res0;
    int addr_len;

    res0 = smt_resolve_host(h, hostname, port, SOCK_DGRAM, AF_UNSPEC, 0);
    if (!res0) return AVERROR(EIO);
    memcpy(addr, res0->ai_addr, res0->ai_addrlen);
    addr_len = res0->ai_addrlen;
    freeaddrinfo(res0);

    return addr_len;
}


static int smt_socket_create(URLContext *h, struct sockaddr_storage *addr,
                             socklen_t *addr_len, const char *localaddr)
{
    SMTContext *s = h->priv_data;
    int smt_fd = -1;
    struct addrinfo *res0, *res;
    int family = AF_UNSPEC;

    if (((struct sockaddr *) &s->dest_addr)->sa_family)
        family = ((struct sockaddr *) &s->dest_addr)->sa_family;
    res0 = smt_resolve_host(h, (localaddr && localaddr[0]) ? localaddr : NULL,
                            s->local_port,
                            SOCK_DGRAM, family, AI_PASSIVE);
    if (!res0)
        goto fail;
    for (res = res0; res; res=res->ai_next) {
        smt_fd = ff_socket(res->ai_family, SOCK_DGRAM, 0);
        if (smt_fd != -1) break;
        log_net_error(NULL, AV_LOG_ERROR, "socket");
    }

    if (smt_fd < 0)
        goto fail;

    memcpy(addr, res->ai_addr, res->ai_addrlen);
    *addr_len = res->ai_addrlen;

    freeaddrinfo(res0);

    return smt_fd;

 fail:
    if (smt_fd >= 0)
        closesocket(smt_fd);
    if(res0)
        freeaddrinfo(res0);
    return -1;
}


static int smt_join_multicast_group(int sockfd, struct sockaddr *addr,struct sockaddr *local_addr)
{
#ifdef IP_ADD_MEMBERSHIP
    if (addr->sa_family == AF_INET) {
        struct ip_mreq mreq;

        mreq.imr_multiaddr.s_addr = ((struct sockaddr_in *)addr)->sin_addr.s_addr;
        if (local_addr)
            mreq.imr_interface= ((struct sockaddr_in *)local_addr)->sin_addr;
        else
            mreq.imr_interface.s_addr= INADDR_ANY;
        if (setsockopt(sockfd, IPPROTO_IP, IP_ADD_MEMBERSHIP, (const void *)&mreq, sizeof(mreq)) < 0) {
            log_net_error(NULL, AV_LOG_ERROR, "setsockopt(IP_ADD_MEMBERSHIP)");
            return -1;
        }
    }
#endif
#if HAVE_STRUCT_IPV6_MREQ && defined(IPPROTO_IPV6)
    if (addr->sa_family == AF_INET6) {
        struct ipv6_mreq mreq6;

        memcpy(&mreq6.ipv6mr_multiaddr, &(((struct sockaddr_in6 *)addr)->sin6_addr), sizeof(struct in6_addr));
        mreq6.ipv6mr_interface= 0;
        if (setsockopt(sockfd, IPPROTO_IPV6, IPV6_ADD_MEMBERSHIP, &mreq6, sizeof(mreq6)) < 0) {
            log_net_error(NULL, AV_LOG_ERROR, "setsockopt(IPV6_ADD_MEMBERSHIP)");
            return -1;
        }
    }
#endif
    return 0;
}


static int smt_leave_multicast_group(int sockfd, struct sockaddr *addr,struct sockaddr *local_addr)
{
#ifdef IP_DROP_MEMBERSHIP
    if (addr->sa_family == AF_INET) {
        struct ip_mreq mreq;

        mreq.imr_multiaddr.s_addr = ((struct sockaddr_in *)addr)->sin_addr.s_addr;
        if (local_addr)
            mreq.imr_interface= ((struct sockaddr_in *)local_addr)->sin_addr;
        else
            mreq.imr_interface.s_addr= INADDR_ANY;
        if (setsockopt(sockfd, IPPROTO_IP, IP_DROP_MEMBERSHIP, (const void *)&mreq, sizeof(mreq)) < 0) {
            log_net_error(NULL, AV_LOG_ERROR, "setsockopt(IP_DROP_MEMBERSHIP)");
            return -1;
        }
    }
#endif
#if HAVE_STRUCT_IPV6_MREQ && defined(IPPROTO_IPV6)
    if (addr->sa_family == AF_INET6) {
        struct ipv6_mreq mreq6;

        memcpy(&mreq6.ipv6mr_multiaddr, &(((struct sockaddr_in6 *)addr)->sin6_addr), sizeof(struct in6_addr));
        mreq6.ipv6mr_interface= 0;
        if (setsockopt(sockfd, IPPROTO_IPV6, IPV6_DROP_MEMBERSHIP, &mreq6, sizeof(mreq6)) < 0) {
            log_net_error(NULL, AV_LOG_ERROR, "setsockopt(IPV6_DROP_MEMBERSHIP)");
            return -1;
        }
    }
#endif
    return 0;
}



int ff_smt_set_remote_url(URLContext *h, const char *uri)
{
    SMTContext *s = h->priv_data;
    char hostname[256], buf[10];
    int port;
    
    av_url_split(NULL, 0, NULL, 0, hostname, sizeof(hostname), &port, NULL, 0, uri);

    /* set the destination address */
    s->dest_addr_len = smt_set_url(h, &s->dest_addr, hostname, port);
    if (s->dest_addr_len < 0) {
        return AVERROR(EIO);
    }
    s->is_multicast = ff_is_multicast_address((struct sockaddr*) &s->dest_addr);

    return 0;
}


static int smt_open(URLContext *h, const char *uri, int flags)
{
    char hostname[1024];
    int port = 0, tmp, smt_fd = -1, bind_ret = -1;
    SMTContext *s = h->priv_data;
    struct sockaddr_storage my_addr;
    socklen_t len;
    int ret;
    int stream_index;

    s->buffer_size = SMT_MAX_PKT_SIZE;

    h->is_streamed = 1;
    
    /* fill the dest addr */
    av_url_split(NULL, 0, NULL, 0, hostname, sizeof(hostname), &port, NULL, 0, uri);
    
    if (hostname[0] == '\0' || hostname[0] == '?') {
        /* only accepts null hostname if input e.g. smt://@:5050 */
        if (!(flags & AVIO_FLAG_READ))
            goto fail;
    } else {
        if (ff_smt_set_remote_url(h, uri) < 0)
            goto fail;
    }

    if ((s->is_multicast || s->local_port <= 0) && (h->flags & AVIO_FLAG_READ))
            s->local_port = port;


    smt_fd = smt_socket_create(h, &my_addr, &len, s->localaddr);
    if (smt_fd < 0)
        goto fail;
    s->local_addr_storage=my_addr; //store for future multicast join

    /* Follow the requested reuse option, unless it's multicast in which
     * case enable reuse unless explicitly disabled.
     */
    if (s->reuse_socket > 0 || (s->is_multicast && s->reuse_socket < 0)){
        s->reuse_socket = 1;
        if (setsockopt (smt_fd, SOL_SOCKET, SO_REUSEADDR, &(s->reuse_socket), sizeof(s->reuse_socket)) != 0)
            goto fail;
    }
        
    /* If multicast, try binding the multicast address first, to avoid
     * receiving UDP packets from other sources aimed at the same UDP
     * port. This fails on windows. This makes sending to the same address
     * using sendto() fail, so only do it if we're opened in read-only mode. */
    if (s->is_multicast && !(h->flags & AVIO_FLAG_WRITE)) {
        bind_ret = bind(smt_fd,(struct sockaddr *)&s->dest_addr, len);
    }

    /* bind to the local address if not multicast or if the multicast
     * bind failed */
    /* the bind is needed to give a port to the socket now */
    if (bind_ret < 0 && bind(smt_fd,(struct sockaddr *)&my_addr, len) < 0) {
        log_net_error(h, AV_LOG_ERROR, "bind failed");
        goto fail;
    }


    len = sizeof(my_addr);
    getsockname(smt_fd, (struct sockaddr *)&my_addr, &len);
    s->local_port = smt_port(&my_addr, len);

    if (s->is_multicast) {
        if (h->flags & AVIO_FLAG_READ) {
            if (smt_join_multicast_group(smt_fd, (struct sockaddr *)&s->dest_addr,(struct sockaddr *)&s->local_addr_storage) < 0)
                goto fail;
        }
    }

    tmp = s->buffer_size;
    if (setsockopt(smt_fd, SOL_SOCKET, SO_RCVBUF, &tmp, sizeof(tmp)) < 0) {
        log_net_error(h, AV_LOG_WARNING, "setsockopt(SO_RECVBUF)");
    }

    s->smt_fd = smt_fd;

    s->send = NULL;
    s->receive = NULL;
     
    s->fifo_size *= MTU;
    s->fifo = NULL;
    ret = pthread_mutex_init(&s->mutex, NULL);
    if (ret != 0) {
        av_log(h, AV_LOG_ERROR, "pthread_mutex_init failed : %s\n", strerror(ret));
        goto fail;
    }
    ret = pthread_cond_init(&s->cond, NULL);
    if (ret != 0) {
        av_log(h, AV_LOG_ERROR, "pthread_cond_init failed : %s\n", strerror(ret));
        goto cond_fail;
    }

    s->generate_thread_run = 1;
    ret = pthread_create(&s->mpu_generate_thread, NULL, smt_mpu_generate_task, h);
    if (ret != 0) {
        av_log(h, AV_LOG_ERROR, "pthread_create failed : %s\n", strerror(ret));
        goto thread_fail;
    }
    return 0;
thread_fail:
   pthread_cond_destroy(&s->cond);
cond_fail:
   pthread_mutex_destroy(&s->mutex);
fail:
    return AVERROR(EIO);

}


static int smt_read(URLContext *h, uint8_t *buf, int size)
{
    SMTContext *s = h->priv_data;
    int asset = s->stream_index;
    int avail;
    
    if(!s->audio_head_available || !s->video_head_available)
        return SMT_DATA_NOT_READY;

    if(!s->hflag){
        avail = av_fifo_size(s->head);
        if(avail > size){
            av_log(h, AV_LOG_WARNING, "insufficient buffer size for smt header\n");
            avail = size;
        }else
            s->hflag = true;
        av_fifo_generic_read(s->head, buf, avail, NULL);
    }else{
        pthread_mutex_lock(&s->mutex);
        avail = av_fifo_size(s->fifo);
        if(0 >= avail){
            if (pthread_cond_wait(&s->cond, &s->mutex) < 0) {
                pthread_mutex_unlock(&s->mutex);
                return AVERROR(errno == ETIMEDOUT ? EAGAIN : errno);
            }
        }
        avail = av_fifo_size(s->fifo);
        if(avail > size){
            //av_log(h, AV_LOG_WARNING, "insufficient buffer size for smt data. size = %u, avail = %u\n",size, avail);
            avail = size;
        }
        av_fifo_generic_read(s->fifo, buf, avail, NULL);
        pthread_mutex_unlock(&s->mutex);
    }

    //avformat_dump("data.mp4", buf, avail, "a+");
    consumption_length += avail;
    return avail;
}

static int smt_write(URLContext *h, const uint8_t *buf, int size)
{
    SMTContext *s = h->priv_data;
    int avail, space;
    int ret;
    //avformat_dump("../../../../Temp/snd_mpu_seq.data", buf, size, "a+");
    if(!s->cache){
        if(!s->send)
            s->send = (smt_send_entity *)av_mallocz(sizeof(smt_send_entity));
        ret = smt_pack_mpu(h, s->send, buf, size);
        if(ret == SMT_STATUS_NEED_MORE_DATA){
            pthread_mutex_lock(&s->mutex);
            s->cache = av_fifo_alloc(size);
            ret = av_fifo_generic_write(s->cache, buf, size, NULL);
            pthread_mutex_unlock(&s->mutex);
            return ret;
        }else if(ret == SMT_STATUS_OK)
            return size;
    }
    pthread_mutex_lock(&s->mutex);
    space = av_fifo_space(s->cache);
    if(space < size)
        av_fifo_grow(s->cache, size - space);
    ret =  av_fifo_generic_write(s->cache, buf, size, NULL);
    pthread_mutex_unlock(&s->mutex);
    return ret;    
}


static int smt_close(URLContext *h)
{
    int ret;
    SMTContext *s = h->priv_data;
    time_t t = time(NULL);
    struct tm *tp = localtime(&t);
    av_log(h, AV_LOG_INFO, "smt socket close at: %d:%d:%d\n", tp->tm_hour, tp->tm_min, tp->tm_sec);
    if (s->is_multicast) 
        smt_leave_multicast_group(s->smt_fd, (struct sockaddr *)&s->dest_addr,(struct sockaddr *)&s->local_addr_storage);
    closesocket(s->smt_fd);

    s->generate_thread_run = 0;
#ifndef __ANDROID__
    pthread_cancel(s->mpu_generate_thread);
#endif
    ret = pthread_join(s->mpu_generate_thread, NULL);
    if (ret != 0)
        av_log(h, AV_LOG_ERROR, "pthread_join(): %s\n", strerror(ret));
    pthread_mutex_destroy(&s->mutex);
    pthread_cond_destroy(&s->cond);
    
    if(s->head)
        av_fifo_freep(&s->head);
    if(s->fifo)
        av_fifo_freep(&s->fifo);
    if(s->cache)
        av_fifo_freep(&s->cache);

    if (s->send)
        av_freep(&s->send);
    if (s->receive)
        av_freep(&s->receive);
    
    return 0;
}

static int smt_seek(URLContext *h, int64_t pos, int whence)
{
    //SMTContext *c = h->priv_data;
    //int64_t ret;
    return consumption_length;
}

static int smt_get_file_handle(URLContext *h)
{
    SMTContext *s = h->priv_data;
    return s->smt_fd;
}


static int64_t smt_set(URLContext *h, AVDictionary *options)
{
    SMTContext *s = h->priv_data;
    smt_status status;
    if(!options)
        return AVERROR(EINVAL);
    AVDictionaryEntry *entry = av_dict_get(options, "smt_payload_size", NULL, AV_DICT_MATCH_CASE);

    if(!entry)
        return AVERROR(EINVAL);

    int size = atoi(entry->value);
    if(size > 0 && !s->cache)
        s->cache = av_fifo_alloc(size);
    else if(size < 0 && s->cache){
        int avail = av_fifo_size(s->cache);
        pthread_mutex_lock(&s->mutex);
        if(avail > 0){
            unsigned char* buf = (unsigned char *)av_mallocz(avail);
            av_fifo_generic_read(s->cache, buf, avail, NULL);
            if(!s->send)
                s->send = (smt_send_entity *)av_mallocz(sizeof(smt_send_entity));
            status = smt_pack_mpu(h, s->send, buf, avail);
            if(SMT_STATUS_OK != status){
                  av_fifo_generic_write(s->cache, buf, avail, NULL); 
            }
            av_freep(&buf);
        }
        if(SMT_STATUS_OK == status)
            av_fifo_freep(&s->cache);

        pthread_mutex_unlock(&s->mutex);
    }
    return 0;
}

static int64_t smt_get(URLContext *h, AVDictionary **options)
{
    uint64_t play_tm = 0;
    char s_play_tm[20] = {"\0"}; //max number is 18446744073709551615 for decimal number
    /*
        TBD
        add get start playing time here !!
        play_tm = ....
    */
    sprintf(s_play_tm, "%d", play_tm);
    if(strlen(s_play_tm) > 0)
            av_dict_set(&(*options), "smt_playing time", s_play_tm, AV_DICT_MATCH_CASE);
    
    return 0;
}

URLProtocol ff_smt_protocol = {
    .name                = "smt",
    .url_open            = smt_open,
    .url_read            = smt_read,
    .url_write           = smt_write,
    .url_close           = smt_close,
    .url_seek            = smt_seek,
    .url_set             = smt_set,
    .url_get             = smt_get,
    .url_get_file_handle = smt_get_file_handle,
    .priv_data_size      = sizeof(SMTContext),
    .priv_data_class     = &smt_class,
    .flags               = URL_PROTOCOL_FLAG_NETWORK,
};

smt_callback smt_callback_entity = {
    .mpu_callback_fun   = smt_on_get_mpu,
    .sig_callback_fun   = smt_on_get_sig,
    .gfd_callback_fun   = smt_on_get_gfd,
    .id_callback_fun    = smt_on_get_id,
    .packet_send        = smt_on_packet_deliver,
};


