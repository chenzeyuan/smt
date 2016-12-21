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
#include "libavutil/avassert.h"
#include "smt_proto.h"

#include <pthread.h>



#define SMT_MAX_PKT_SIZE (65536 * 20)
#define SMT_DATA_NOT_READY -11

#define SMT_NO_AUDIO 0
#define SMT_NO_VIDEO 0

#define SMT_MAX_DELIVERY_NUM    10
#define SMT_MAX_PACKED_NUM      2

#define MAX_STRING_LEN_FOR_DECIMAL_NUMBER 50
#define MAX_LEN_FOR_DICTIONARY_KEY        50

typedef struct SMT4AvLogExt {
    int     send_counter;
    int64_t start_time;
    int64_t len_sum;
} SMT4AvLogExt;

typedef struct SMTContext {
    const AVClass *class;
    int smt_fd[SMT_MAX_DELIVERY_NUM];
    int smt_fd_size;
    int buffer_size;
    int pkt_size;
    int is_multicast[SMT_MAX_DELIVERY_NUM];
    int is_broadcast;
    int local_port;
    int reuse_socket;
    char *localaddr;
    int is_connected;
    char *sources;
    struct sockaddr_storage dest_addr[SMT_MAX_DELIVERY_NUM];
    int dest_addr_len[SMT_MAX_DELIVERY_NUM];
    struct sockaddr_storage local_addr_storage[SMT_MAX_DELIVERY_NUM];

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
    char remote_server[100];
    struct SMT4AvLogExt info_av_log_ext;
    int64_t begin_time[SMT_MAX_PACKED_NUM];//0:audio, 1:video
    int64_t mpu_sequence_number[SMT_MAX_PACKED_NUM];//0:audio, 1:video
    int64_t mpu_lost_counter[SMT_MAX_PACKED_NUM];//0:audio, 1:video
    int64_t media_duration[SMT_MAX_PACKED_NUM];//0:audio, 1:video, duration is an integer that declares the duration of this media(in the scale of the timescale). 
    int64_t mpu_duration[SMT_MAX_PACKED_NUM];//0:audio, 1:video, mpu_duration is an integer that declares the duration of this media(in the scale of millisecond). 
    unsigned int last_packet_counter;
} SMTContext;

static unsigned int consumption_length = 0;
static SMTContext * smtContext;
static URLContext * smtH;

static struct SMT4AvLogExt info_av_log_ext = {0};
#define NUMBER_SIZE 5000000


int smt_add_delivery_url(const char *uri);
int smt_del_delivery_url(const char *uri);
int server_socket_fd = -1 ;
int smt_bitrate = 0;

static int count_colon(char *str)
{
    char *p = str;
    int cnt = 0;
 
    while (*p != '\0') {
        if (*p == ':') {
            cnt++;
        }
        p++;
    } 
    return cnt;
}


static int64_t smt_on_mpu_lost(URLContext *h, unsigned short packet_id, int64_t count) {
    if(!h || !h->priv_data) return 0;
    assert(packet_id < SMT_MAX_PACKED_NUM);
    SMTContext *s = h->priv_data;
    av_log(h, AV_LOG_ERROR, "\n %d  mpu lost, packet_id=%d\n", count, packet_id);
    int64_t tmp = s->mpu_lost_counter[packet_id];
    s->mpu_lost_counter[packet_id] += count;
    return tmp;
}

static void inform_server_add(char * server_addr, int fd) 
{
    char * pch;
    char * address; 
    char * port;
    char buffer[100];
    char cpy[100];
    struct sockaddr_in server;

    strcpy(cpy, server_addr);
    pch = strtok (cpy, ":");
    address = pch; 
    pch = strtok (NULL, ":");
    port = pch;
    
    bzero(&server, sizeof(server));
    server.sin_family = AF_INET;
    server.sin_addr.s_addr = inet_addr(address);
    server.sin_port = htons(atoi(port));

    strcpy(buffer, "add ");
    strcpy(buffer+strlen(buffer), "SOURCE");
    
    if(sendto(fd, buffer, 100,0,(struct sockaddr*)&server,sizeof(server)) < 0)
    {
        av_log(NULL, AV_LOG_WARNING, "[ERROR!!] inform server address %s failed\n", address);
        return;
    }

    av_log(NULL, AV_LOG_WARNING, "[Add] inform server address %s:%s command %s\n", address, port, buffer);
}

static void informs_server_delete(char * server_addr, int fd) 
{
    char * pch;
    char * address; 
    char * port;
    char buffer[100];
    char cpy[100];
    struct sockaddr_in server;

    strcpy(cpy, server_addr);
    pch = strtok (cpy, ":");
    address = pch; 
    pch = strtok (NULL, ":");
    port = pch;
    
    bzero(&server, sizeof(server));
    server.sin_family = AF_INET;
    server.sin_addr.s_addr = inet_addr(address);
    server.sin_port = htons(atoi(port));

    strcpy(buffer, "delete ");
    strcpy(buffer+strlen(buffer), "SOURCE");
    
    if(sendto(fd, buffer, 100,0,(struct sockaddr*)&server,sizeof(server)) < 0)
    {
        av_log(NULL, AV_LOG_WARNING, "[ERROR!!] inform server address %s failed\n", address);
        return;
    }

    av_log(NULL, AV_LOG_WARNING, "[Delete] inform server address %s:%s command %s\n", address, port, buffer);
}

static void smt_on_get_mpu(URLContext *h, 
                                 smt_mpu *mpu)
{
    SMTContext *s = h->priv_data;


    /* calculate the duration of every mpu */
    /* The duration of video MPU is fixed, but the duration of audio MPU is not fixed. 
       now average value is used for estimate duration. 
       To be modified to a better solution in the future */
    if(/*0 == s->mpu_duration[mpu->asset] && */ 0 != s->media_duration[mpu->asset]) {
        int64_t temp = (mpu->media_duration - s->media_duration[mpu->asset])  / (mpu->sequence - s->mpu_sequence_number[mpu->asset]);
        if(s->mpu_duration[mpu->asset] == 0) {
            s->mpu_duration[mpu->asset] = temp * 1000 / mpu->timescale;
        } else {
            s->mpu_duration[mpu->asset] = (4 * s->mpu_duration[mpu->asset] + temp * 1000 / mpu->timescale + 1)/5;
        }
        //av_log(NULL, AV_LOG_INFO, "\n mpu_duration[%d]=%lld\n", mpu->asset,  s->mpu_duration[mpu->asset]);
    }
    s->media_duration[mpu->asset] = mpu->media_duration;

    if(s->mpu_sequence_number[mpu->asset] >= 0 && s->mpu_sequence_number[mpu->asset] + 1 != mpu->sequence) {
        av_assert0(mpu->sequence > s->mpu_sequence_number[mpu->asset] + 1);
        smt_on_mpu_lost(h, mpu->asset, mpu->sequence - s->mpu_sequence_number[mpu->asset] - 1); 
    }

    s->mpu_sequence_number[mpu->asset] = mpu->sequence;    
    //av_log(NULL, AV_LOG_INFO, "\n on get mpu, asset=%d, sequence=%d, duration=%lld, timescale=%ld\n", 
    //                                 mpu->asset,  mpu->sequence, mpu->media_duration, mpu->timescale);

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

/* ----------------------------------------------*/
#define CACHE_SIZE 10000
typedef struct SeqQueue {
    char  *data[CACHE_SIZE];
    int   len[CACHE_SIZE];
    int   type[CACHE_SIZE]; //0:mmtp  0x20:net_state,for test delay
    int   front;
    int   rear;
    pthread_mutex_t q_lock;
    pthread_cond_t cond;
    int init_flag;
    URLContext *h;
}Queue;

Queue cache_queue;

static Queue *InitQueue(Queue *q) {
    q->front = 0;
    q->rear = 0;
    pthread_mutex_init(&q->q_lock, NULL);      
    pthread_cond_init(&q->cond, NULL);
    return q;
}

static Queue *GetQueue() {
    return &cache_queue;
}

static int IsFull(Queue *q) {
    return ((q->rear+1)%CACHE_SIZE == q->front);
}

static int IsEmpty(Queue *q) {
    return (q->front == q->rear);
}

static int GetQueueSize(Queue *q) {
    return (q->rear + CACHE_SIZE - q->front)%CACHE_SIZE;
}


static void Enqueue(void *data, int len) { 
    Queue *q = GetQueue();
    pthread_mutex_lock(&q->q_lock);
    if(q && data && !IsFull(q)) {
        q->data[q->rear] = data;
        q->len[q->rear] = len;
        q->rear = (q->rear+1)%CACHE_SIZE;
        pthread_cond_signal(&q->cond);
    }
    pthread_mutex_unlock(&q->q_lock);
}

static int Dequeue(Queue *q, void **data) {
    int len;
    pthread_mutex_lock(&q->q_lock);
    while(IsEmpty(q)) {
        pthread_cond_wait(&q->cond, &q->q_lock);
    }
    *data = q->data[q->front];
    len = q->len[q->front];
    q->front = (q->front+1)%CACHE_SIZE;
    pthread_mutex_unlock(&q->q_lock);
    return len;
}
#ifdef SMT_NET_STATE
#define NET_STATE_REPORT_DEALY_INTERVAL   500   //unit milisecond
static int smt_send_net_state(SMTContext *s, unsigned char** buf) {
    static int64_t net_state_send_time = 0;
    int64_t now_time = av_gettime();
    if(now_time - net_state_send_time < NET_STATE_REPORT_DEALY_INTERVAL * 1000) {
        return 0;
    } 
    net_state_send_time  = now_time;
    return smt_pack_net_state(buf);
}
#endif

static float smt_calc_rate(struct SMT4AvLogExt *info, char *filename, int len, int number_size) {
    char* device = NULL;
    if(0 == info->send_counter) {
        info->start_time = av_gettime();
    } else {
        int64_t end_time = av_gettime();
        int64_t diff_time = end_time - info->start_time; 
        if( diff_time >= 100 * 1000 || number_size <= info->send_counter) {
            float rate = info->len_sum * 8 * 1.0f * 1000 * 1000 / (1024 * 1024 * ( end_time - info->start_time));
            device = get_av_log_device_info();
            if(!device) device = "none";
            av_log_ext(NULL, AV_LOG_INFO, "{\"device\":\"%s\",\"filename\":\"%s\",\"time\":\"%lld\",\"bitrate\":\"%f\"}\n", device, filename, end_time, rate);
            info->start_time = 0;
            info->send_counter = 0;
            info->len_sum = 0;
            return rate;
        }
    }
    info->len_sum += len;
    info->send_counter++;
    return -1;
}

#define MAX_BPS  (25 * 1024 * 1024)
#define MAX_DELAY (MTU * 8 * 1E6) / (500 * 1024)   // 500K bps     11077us
#define MIN_DELAY (MTU * 8 * 1E6) / (25 * 1024 * 1024)  // 25M bps    443us
#define DEFAUT_SMT_BITRATE (20 * 1024)      // unit is Kbps
static void send_socket_cache(Queue *q) {

    unsigned char* buf = NULL;
    int ret;
    int delay_time = 0; //
    SMTContext *s = q->h->priv_data;
    if(0 == smt_bitrate) smt_bitrate = DEFAUT_SMT_BITRATE;      
    delay_time =(MTU * 8 * 1E6) / (smt_bitrate  * 1024 * 12 /10);
    delay_time -= 100;  //subtract program processing time
    delay_time  = delay_time>MAX_DELAY?MAX_DELAY:delay_time;       
    delay_time  = delay_time<MIN_DELAY?MIN_DELAY:delay_time;       

    while(1) {
#ifdef SMT_NET_STATE
        int len = smt_send_net_state(s, &buf);
        if(len > 0) {
        }
        else
#endif
		{
            len = Dequeue(q, &buf);
        }
        if(NULL == buf) continue;
        for(int i = 0 ; i < s->smt_fd_size; i++) {
            if (!s->is_connected) {
                struct sockaddr_in * dest_addr = (struct sockaddr_in *) &s->dest_addr[i];                
                if(s->smt_fd[i] == NULL || ntohs(dest_addr->sin_port) == 1) continue;

                if(server_socket_fd != -1) {
                    ret = sendto (server_socket_fd, buf, len, 0,
                            (struct sockaddr *) &s->dest_addr[i],
                            s->dest_addr_len[i]);
                }
                else {
                    ret = sendto (s->smt_fd[i], buf, len, 0,
                            (struct sockaddr *) &s->dest_addr[i],
                            s->dest_addr_len[i]);
                }
            } else {
                if(s->smt_fd[i] == NULL) continue;
                ret = send(s->smt_fd[i], buf, len, 0);
            }
            //smt_calc_rate(&info_av_log_ext, q->h->filename, len, NUMBER_SIZE * s->smt_fd_size);
            smt_calc_rate(&info_av_log_ext, q->h->filename, len, NUMBER_SIZE);
        }
        av_usleep(delay_time);
    }
}




void init_send_socket_cache() {
    pthread_t send_socket_cache_thread;
    send_socket_cache_thread= pthread_create(&send_socket_cache_thread, NULL, send_socket_cache, GetQueue());
}

static void set_socket_cache_queue(URLContext *h) {
    Queue *q = GetQueue();
    pthread_mutex_lock(&q->q_lock);
    if(q->init_flag == 0) {
        q->h = h;
        init_send_socket_cache();
        q->init_flag = 1;
    }
    pthread_mutex_unlock(&q->q_lock);
}


/* ----------------------------------------------*/

#define SMT_OUTPUT_CACHE_CONTROL
static int smt_on_packet_deliver(URLContext *h, unsigned char *buf, int len)
{
    SMTContext *s = h->priv_data;
    int ret;
    int i;

#ifdef SMT_OUTPUT_CACHE_CONTROL
    set_socket_cache_queue(h);
    void *data = malloc(len);
    memcpy(data, buf, len);
    Enqueue(data, len);
#else
    for(i = 0 ; i < s->smt_fd_size; i++) {
        if (!s->is_connected) {
            struct sockaddr_in * dest_addr = (struct sockaddr_in *) &s->dest_addr[i];                
            //av_log(NULL, AV_LOG_WARNING, "sending data to client %s:%d\n", inet_ntoa(dest_addr->sin_addr), ntohs(dest_addr->sin_port));
            if(s->smt_fd[i] == NULL || ntohs(dest_addr->sin_port) == 1) continue;


             // TODO: liminghao smt_fd[] is not used for hole punching
            if(server_socket_fd != -1) {
                ret = sendto (server_socket_fd, buf, len, 0,
                              (struct sockaddr *) &s->dest_addr[i],
                              s->dest_addr_len[i]);
            }
            else {
                ret = sendto (s->smt_fd[i], buf, len, 0,
                              (struct sockaddr *) &s->dest_addr[i],
                              s->dest_addr_len[i]);
            }
                
        } else {
            if(s->smt_fd[i] == NULL) continue;
            ret = send(s->smt_fd[i], buf, len, 0);
        }
        //smt_calc_rate(&info_av_log_ext, h->filename, len, s->smt_fd_size * NUMBER_SIZE);
        smt_calc_rate(&info_av_log_ext, h->filename, len, NUMBER_SIZE);
    }
    av_usleep(50);
#endif
    /*
        switch(s->smt_fd_size) {
            case 1:   av_usleep(100);  break;
            case 2:   av_usleep(200);  break;
            case 3:   av_usleep(300);  break;
            case 4:   av_usleep(400);  break;
            case 5:   av_usleep(500);  break;
            default:
                break;
        } 
        */
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
        len = recv(s->smt_fd[0], buf, MTU, 0);
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
                             socklen_t *addr_len, const char *localaddr, int localport)
{
    SMTContext *s = h->priv_data;
    int smt_fd = -1;
    struct addrinfo *res0, *res;
    int family = AF_UNSPEC;

    if (((struct sockaddr *) &s->dest_addr[s->smt_fd_size])->sa_family)
        family = ((struct sockaddr *) &s->dest_addr[s->smt_fd_size])->sa_family;
    res0 = smt_resolve_host(h, (localaddr && localaddr[0]) ? localaddr : NULL,
                            localport,
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
    char proto[8];

    av_url_split(proto, sizeof(proto), NULL, 0, hostname, sizeof(hostname), &port, NULL, 0, uri);

    /* make sure only smt protocol could be set*/
    if(strcmp(proto, "smt") != 0) {
        av_log(h, AV_LOG_WARNING, "[Warning] proto [%s] cannot be set, only smt is supported\n", proto);
        return -1;
    }
    
    /* set the destination address */
    s->dest_addr_len[s->smt_fd_size] = smt_set_url(h, &s->dest_addr[s->smt_fd_size], hostname, port);
    if (s->dest_addr_len < 0) {
        return AVERROR(EIO);
    }
    s->is_multicast[s->smt_fd_size] = ff_is_multicast_address((struct sockaddr*) &(s->dest_addr[s->smt_fd_size]));
    
    return port;
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
    s->smt_fd_size = 0;
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

    if ((s->is_multicast[0] || s->local_port <= 0) && (h->flags & AVIO_FLAG_READ))
            s->local_port = port;


    smt_fd = smt_socket_create(h, &my_addr, &len, s->localaddr, s->local_port);
    if (smt_fd < 0)
        goto fail;
    s->local_addr_storage[0]=my_addr; //store for future multicast join

    /* Follow the requested reuse option, unless it's multicast in which
     * case enable reuse unless explicitly disabled.
     */
    if (s->reuse_socket > 0 || (s->is_multicast[0] && s->reuse_socket < 0)){
        s->reuse_socket = 1;
        if (setsockopt (smt_fd, SOL_SOCKET, SO_REUSEADDR, &(s->reuse_socket), sizeof(s->reuse_socket)) != 0)
            goto fail;
    }
        
    /* If multicast, try binding the multicast address first, to avoid
     * receiving UDP packets from other sources aimed at the same UDP
     * port. This fails on windows. This makes sending to the same address
     * using sendto() fail, so only do it if we're opened in read-only mode. */
    if (s->is_multicast[0] && !(h->flags & AVIO_FLAG_WRITE)) {
        bind_ret = bind(smt_fd,(struct sockaddr *)&s->dest_addr[0], len);
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

    if (s->is_multicast[0]) {
        if (h->flags & AVIO_FLAG_READ) {
            if (smt_join_multicast_group(smt_fd, (struct sockaddr *)&s->dest_addr[0],(struct sockaddr *)&s->local_addr_storage[0]) < 0)
                goto fail;
        }
    }

    tmp = s->buffer_size;
    if (setsockopt(smt_fd, SOL_SOCKET, SO_RCVBUF, &tmp, sizeof(tmp)) < 0) {
        log_net_error(h, AV_LOG_WARNING, "setsockopt(SO_RECVBUF)");
    }


    s->smt_fd[0] = smt_fd;
    s->smt_fd_size = 1;
    
    smtContext = s;
    smtH = h;

    SMT_FD[nb_smt_fd+1] = smt_fd;
    nb_smt_fd++;

    if(count_colon(uri) == 3) {
        sprintf(s->remote_server, "%s:%d", hostname, port);
        inform_server_add(s->remote_server, smt_fd);
    }


    s->send = NULL;
    s->receive = NULL;
     
    s->fifo_size *= MTU;
    s->fifo = NULL;
    memset(s->begin_time, 0, SMT_MAX_PACKED_NUM * sizeof(int64_t));
    memset(s->mpu_lost_counter, 0, SMT_MAX_PACKED_NUM * sizeof(int64_t));
    memset(s->mpu_sequence_number, 0xFF, SMT_MAX_PACKED_NUM * sizeof(int64_t));

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
        }else if(ret == SMT_STATUS_OK){
            //
            //static int counter = 0;
            //if(counter % 2 == 0) 
            //    smt_pack_signal(h);
            //counter++;
            //
            return size;
        }
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
    int i;
    SMTContext *s = h->priv_data;
    time_t t = time(NULL);
    struct tm *tp = localtime(&t);
    av_log(h, AV_LOG_INFO, "smt socket close at: %d:%d:%d\n", tp->tm_hour, tp->tm_min, tp->tm_sec);

    informs_server_delete(s->remote_server, s->smt_fd[0]);

    for(i = 0; i < s->smt_fd_size; i++) {
        if (s->is_multicast[i]) 
            smt_leave_multicast_group(s->smt_fd[i], (struct sockaddr *)&s->dest_addr[i],(struct sockaddr *)&s->local_addr_storage[i]);
        closesocket(s->smt_fd[i]);
    }

    s->generate_thread_run = 0;
#ifndef SMT_ANDROID
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
    return s->smt_fd[0];
}


static int64_t smt_set(URLContext *h, AVDictionary *options)
{
    SMTContext *s = h->priv_data;
    smt_status status;
    
    //av_log(NULL, AV_LOG_INFO, "\n %s \n",__FUNCTION__);
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

static int64_t smt_set_begin_time(URLContext *h, unsigned short packet_id, int64_t begin_time) {
    if(!h || !h->priv_data) return 0;
    assert(packet_id < SMT_MAX_PACKED_NUM);
    SMTContext *s = h->priv_data;
    int64_t tmp = s->begin_time;
    s->begin_time[packet_id] = begin_time;
    return tmp;
}

static int64_t smt_get_begin_time(URLContext *h, unsigned short packet_id) {
    if(!h || !h->priv_data) return 0;
    assert(packet_id < SMT_MAX_PACKED_NUM);
    SMTContext *s = h->priv_data;
    return s->begin_time[packet_id];
}

static int smt_set_last_packet_counter(URLContext *h, unsigned int counter) {
    if(!h || !h->priv_data) return 0;
    SMTContext *s = h->priv_data;
    s->last_packet_counter = counter;
    return 0;
}

static unsigned int smt_get_last_packet_counter(URLContext *h) {
    if(!h || !h->priv_data) return 0;
    SMTContext *s = h->priv_data;
    return s->last_packet_counter;    
}


static unsigned int smt_set_media_info(URLContext *h, unsigned int mpu_sequence, unsigned short packet_id, int64_t duration, int32_t timescale) {
    if(!h || !h->priv_data) return 0;
    SMTContext *s = h->priv_data;
    return s->last_packet_counter;    
}


static int64_t smt_get(URLContext *h, AVDictionary **options)
{
    SMTContext *s = NULL;
    int i = 0;
    int64_t value = 0;
    char    s_value[MAX_STRING_LEN_FOR_DECIMAL_NUMBER] ={0};//max number is 18446744073709551615 for decimal number
    char    s_key[MAX_LEN_FOR_DICTIONARY_KEY]={0};
    
    if(!h || !h->priv_data) return 0;
    s = h->priv_data;

    for(i = 0; i < SMT_MAX_PACKED_NUM; i++) {
        /*--------------begin time------------------*/
        memset(s_key, 0, MAX_LEN_FOR_DICTIONARY_KEY *  sizeof(char));
        sprintf(s_key, "begin_time[%d]", i);
        value = s->begin_time[i];

        if(0 != value)  {
            memset(s_value, 0, MAX_STRING_LEN_FOR_DECIMAL_NUMBER * sizeof(char));
            sprintf(s_value, "%lld", value);
            if(strlen(s_value) > 0) {
                av_dict_set(&(*options), s_key, s_value, AV_DICT_MATCH_CASE);
            }
        }
        /*--------------mpu lost------------------*/
        memset(s_key, 0, MAX_LEN_FOR_DICTIONARY_KEY *  sizeof(char));
        sprintf(s_key, "mpu_lost_time[%d]", i);
        value = s->mpu_lost_counter[i] * s->mpu_duration[i];

        if(0 != value)  {
            memset(s_value, 0, MAX_STRING_LEN_FOR_DECIMAL_NUMBER * sizeof(char));
            sprintf(s_value, "%lld", value);
            if(strlen(s_value) > 0) {
                av_dict_set(&(*options), s_key, s_value, AV_DICT_MATCH_CASE);
            }
        }
    }
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
    .set_begin_time     = smt_set_begin_time,
    .get_begin_time     = smt_get_begin_time,
    .set_last_packet_counter = smt_set_last_packet_counter,
    .get_last_packet_counter = smt_get_last_packet_counter,
    .on_mpu_lost    = smt_on_mpu_lost,
};


int smt_add_delivery_url(const char *uri)
{
    char hostname[1024];
    int port = 0, smt_fd = -1;
    struct sockaddr_storage my_addr;
    socklen_t len;
    char *localaddr;

    if(smtContext->smt_fd_size == SMT_MAX_DELIVERY_NUM)
        return -1;
    port = ff_smt_set_remote_url(smtH, uri);
    if(port <= 0)
        return port;

    smt_fd = smt_socket_create(smtH, &my_addr, &len, localaddr, port);
    if (smt_fd < 0)
        return -1;
    smtContext->local_addr_storage[smtContext->smt_fd_size]=my_addr; //store for future multicast join

    if (smtContext->is_multicast[smtContext->smt_fd_size]) {
        if (smt_join_multicast_group(smt_fd, (struct sockaddr *)&smtContext->dest_addr[smtContext->smt_fd_size],(struct sockaddr *)&smtContext->local_addr_storage[smtContext->smt_fd_size]) < 0)
                return -1;
    }

    smtContext->smt_fd[smtContext->smt_fd_size] = smt_fd;
    smtContext->smt_fd_size++;

    return 1;
}

int smt_del_delivery_url(const char *uri)
{
    char hostname[1024];
    int port = 0, tmp, smt_fd = -1, bind_ret = -1;
    struct sockaddr_storage my_addr;
    socklen_t len;
    char *localaddr;
    int i, j;
    
    if(smtContext->smt_fd_size == 1)
        return -1;

    for(i = 1; i < smtContext->smt_fd_size; i++)
    {    
        struct sockaddr_in * dest_addr = (struct sockaddr_in *)&smtContext->dest_addr[i];        
        char addr_buf[100];
        sprintf(addr_buf, "%s:%d", inet_ntoa(dest_addr->sin_addr), (int)ntohs(dest_addr->sin_port));
        av_log(NULL, AV_LOG_WARNING, "try to del %s with %s, total: %d \n", uri, addr_buf, smtContext->smt_fd_size);
        if(!strcmp(uri, addr_buf))
            continue;
        
        av_log(NULL, AV_LOG_WARNING, "found try to del %s on slot %d \n", inet_ntoa(dest_addr->sin_addr), i);
        if (smtContext->is_multicast[i]) 
            smt_leave_multicast_group(smtContext->smt_fd[i], (struct sockaddr *)&smtContext->dest_addr[i],(struct sockaddr *)&smtContext->local_addr_storage[i]);
        closesocket(smtContext->smt_fd[i]);
        for(j = i+1; j < smtContext->smt_fd_size; j++) {
            smtContext->smt_fd[j-1] = smtContext->smt_fd[j];
            smtContext->dest_addr[j-1] = smtContext->dest_addr[j];
            smtContext->dest_addr_len[j-1] = smtContext->dest_addr_len[j];
            smtContext->local_addr_storage[j-1] = smtContext->local_addr_storage[j];
         }
        smtContext->smt_fd_size--;
        return 1;
    }
    return 1;
}

