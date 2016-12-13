/*
 * log functions
 * Copyright (c) 2003 Michel Bardiaux
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
 * logging functions
 */

#include "config.h"

#if HAVE_UNISTD_H
#include <unistd.h>
#endif
#if HAVE_IO_H
#include <io.h>
#endif
#include <stdarg.h>
#include <stdlib.h>
#include "avutil.h"
#include "bprint.h"
#include "common.h"
#include "internal.h"
#include "log.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/time.h>

#if HAVE_PTHREADS
#include <pthread.h>
static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
#endif

#define LINE_SZ 1024

#if HAVE_VALGRIND_VALGRIND_H
#include <valgrind/valgrind.h>
/* this is the log level at which valgrind will output a full backtrace */
#define BACKTRACE_LOGLEVEL AV_LOG_ERROR
#endif

static int av_log_level = AV_LOG_INFO;
static int flags;

#define NB_LEVELS 8
#if defined(_WIN32) && !defined(__MINGW32CE__) && HAVE_SETCONSOLETEXTATTRIBUTE
#include <windows.h>
static const uint8_t color[16 + AV_CLASS_CATEGORY_NB] = {
    [AV_LOG_PANIC  /8] = 12,
    [AV_LOG_FATAL  /8] = 12,
    [AV_LOG_ERROR  /8] = 12,
    [AV_LOG_WARNING/8] = 14,
    [AV_LOG_INFO   /8] =  7,
    [AV_LOG_VERBOSE/8] = 10,
    [AV_LOG_DEBUG  /8] = 10,
    [AV_LOG_TRACE  /8] = 8,
    [16+AV_CLASS_CATEGORY_NA              ] =  7,
    [16+AV_CLASS_CATEGORY_INPUT           ] = 13,
    [16+AV_CLASS_CATEGORY_OUTPUT          ] =  5,
    [16+AV_CLASS_CATEGORY_MUXER           ] = 13,
    [16+AV_CLASS_CATEGORY_DEMUXER         ] =  5,
    [16+AV_CLASS_CATEGORY_ENCODER         ] = 11,
    [16+AV_CLASS_CATEGORY_DECODER         ] =  3,
    [16+AV_CLASS_CATEGORY_FILTER          ] = 10,
    [16+AV_CLASS_CATEGORY_BITSTREAM_FILTER] =  9,
    [16+AV_CLASS_CATEGORY_SWSCALER        ] =  7,
    [16+AV_CLASS_CATEGORY_SWRESAMPLER     ] =  7,
    [16+AV_CLASS_CATEGORY_DEVICE_VIDEO_OUTPUT ] = 13,
    [16+AV_CLASS_CATEGORY_DEVICE_VIDEO_INPUT  ] = 5,
    [16+AV_CLASS_CATEGORY_DEVICE_AUDIO_OUTPUT ] = 13,
    [16+AV_CLASS_CATEGORY_DEVICE_AUDIO_INPUT  ] = 5,
    [16+AV_CLASS_CATEGORY_DEVICE_OUTPUT       ] = 13,
    [16+AV_CLASS_CATEGORY_DEVICE_INPUT        ] = 5,
};

static int16_t background, attr_orig;
static HANDLE con;
#else

static const uint32_t color[16 + AV_CLASS_CATEGORY_NB] = {
    [AV_LOG_PANIC  /8] =  52 << 16 | 196 << 8 | 0x41,
    [AV_LOG_FATAL  /8] = 208 <<  8 | 0x41,
    [AV_LOG_ERROR  /8] = 196 <<  8 | 0x11,
    [AV_LOG_WARNING/8] = 226 <<  8 | 0x03,
    [AV_LOG_INFO   /8] = 253 <<  8 | 0x09,
    [AV_LOG_VERBOSE/8] =  40 <<  8 | 0x02,
    [AV_LOG_DEBUG  /8] =  34 <<  8 | 0x02,
    [AV_LOG_TRACE  /8] =  34 <<  8 | 0x07,
    [16+AV_CLASS_CATEGORY_NA              ] = 250 << 8 | 0x09,
    [16+AV_CLASS_CATEGORY_INPUT           ] = 219 << 8 | 0x15,
    [16+AV_CLASS_CATEGORY_OUTPUT          ] = 201 << 8 | 0x05,
    [16+AV_CLASS_CATEGORY_MUXER           ] = 213 << 8 | 0x15,
    [16+AV_CLASS_CATEGORY_DEMUXER         ] = 207 << 8 | 0x05,
    [16+AV_CLASS_CATEGORY_ENCODER         ] =  51 << 8 | 0x16,
    [16+AV_CLASS_CATEGORY_DECODER         ] =  39 << 8 | 0x06,
    [16+AV_CLASS_CATEGORY_FILTER          ] = 155 << 8 | 0x12,
    [16+AV_CLASS_CATEGORY_BITSTREAM_FILTER] = 192 << 8 | 0x14,
    [16+AV_CLASS_CATEGORY_SWSCALER        ] = 153 << 8 | 0x14,
    [16+AV_CLASS_CATEGORY_SWRESAMPLER     ] = 147 << 8 | 0x14,
    [16+AV_CLASS_CATEGORY_DEVICE_VIDEO_OUTPUT ] = 213 << 8 | 0x15,
    [16+AV_CLASS_CATEGORY_DEVICE_VIDEO_INPUT  ] = 207 << 8 | 0x05,
    [16+AV_CLASS_CATEGORY_DEVICE_AUDIO_OUTPUT ] = 213 << 8 | 0x15,
    [16+AV_CLASS_CATEGORY_DEVICE_AUDIO_INPUT  ] = 207 << 8 | 0x05,
    [16+AV_CLASS_CATEGORY_DEVICE_OUTPUT       ] = 213 << 8 | 0x15,
    [16+AV_CLASS_CATEGORY_DEVICE_INPUT        ] = 207 << 8 | 0x05,
};

#endif
static int use_color = -1;

static char* device_information = NULL;

static void check_color_terminal(void)
{
#if defined(_WIN32) && !defined(__MINGW32CE__) && HAVE_SETCONSOLETEXTATTRIBUTE
    CONSOLE_SCREEN_BUFFER_INFO con_info;
    con = GetStdHandle(STD_ERROR_HANDLE);
    use_color = (con != INVALID_HANDLE_VALUE) && !getenv("NO_COLOR") &&
                !getenv("AV_LOG_FORCE_NOCOLOR");
    if (use_color) {
        GetConsoleScreenBufferInfo(con, &con_info);
        attr_orig  = con_info.wAttributes;
        background = attr_orig & 0xF0;
    }
#elif HAVE_ISATTY
    char *term = getenv("TERM");
    use_color = !getenv("NO_COLOR") && !getenv("AV_LOG_FORCE_NOCOLOR") &&
                (getenv("TERM") && isatty(2) || getenv("AV_LOG_FORCE_COLOR"));
    if (   getenv("AV_LOG_FORCE_256COLOR")
        || (term && strstr(term, "256color")))
        use_color *= 256;
#else
    use_color = getenv("AV_LOG_FORCE_COLOR") && !getenv("NO_COLOR") &&
               !getenv("AV_LOG_FORCE_NOCOLOR");
#endif
}

static void colored_fputs(int level, int tint, const char *str)
{
    int local_use_color;
    if (!*str)
        return;

    if (use_color < 0)
        check_color_terminal();

    if (level == AV_LOG_INFO/8) local_use_color = 0;
    else                        local_use_color = use_color;

#if defined(_WIN32) && !defined(__MINGW32CE__) && HAVE_SETCONSOLETEXTATTRIBUTE
    if (local_use_color)
        SetConsoleTextAttribute(con, background | color[level]);
    fputs(str, stderr);
    if (local_use_color)
        SetConsoleTextAttribute(con, attr_orig);
#else
    if (local_use_color == 1) {
        fprintf(stderr,
                "\033[%d;3%dm%s\033[0m",
                (color[level] >> 4) & 15,
                color[level] & 15,
                str);
    } else if (tint && use_color == 256) {
        fprintf(stderr,
                "\033[48;5;%dm\033[38;5;%dm%s\033[0m",
                (color[level] >> 16) & 0xff,
                tint,
                str);
    } else if (local_use_color == 256) {
        fprintf(stderr,
                "\033[48;5;%dm\033[38;5;%dm%s\033[0m",
                (color[level] >> 16) & 0xff,
                (color[level] >> 8) & 0xff,
                str);
    } else
        fputs(str, stderr);
#endif

}

const char *av_default_item_name(void *ptr)
{
    return (*(AVClass **) ptr)->class_name;
}

AVClassCategory av_default_get_category(void *ptr)
{
    return (*(AVClass **) ptr)->category;
}

static void sanitize(uint8_t *line){
    while(*line){
        if(*line < 0x08 || (*line > 0x0D && *line < 0x20))
            *line='?';
        line++;
    }
}

static int get_category(void *ptr){
    AVClass *avc = *(AVClass **) ptr;
    if(    !avc
        || (avc->version&0xFF)<100
        ||  avc->version < (51 << 16 | 59 << 8)
        ||  avc->category >= AV_CLASS_CATEGORY_NB) return AV_CLASS_CATEGORY_NA + 16;

    if(avc->get_category)
        return avc->get_category(ptr) + 16;

    return avc->category + 16;
}

static const char *get_level_str(int level)
{
    switch (level) {
    case AV_LOG_QUIET:
        return "quiet";
    case AV_LOG_DEBUG:
        return "debug";
    case AV_LOG_VERBOSE:
        return "verbose";
    case AV_LOG_INFO:
        return "info";
    case AV_LOG_WARNING:
        return "warning";
    case AV_LOG_ERROR:
        return "error";
    case AV_LOG_FATAL:
        return "fatal";
    case AV_LOG_PANIC:
        return "panic";
    default:
        return "";
    }
}

static void format_line(void *avcl, int level, const char *fmt, va_list vl,
                        AVBPrint part[4], int *print_prefix, int type[2])
{
    AVClass* avc = avcl ? *(AVClass **) avcl : NULL;
    av_bprint_init(part+0, 0, 1);
    av_bprint_init(part+1, 0, 1);
    av_bprint_init(part+2, 0, 1);
    av_bprint_init(part+3, 0, 65536);

    if(type) type[0] = type[1] = AV_CLASS_CATEGORY_NA + 16;
    if (*print_prefix && avc) {
        if (avc->parent_log_context_offset) {
            AVClass** parent = *(AVClass ***) (((uint8_t *) avcl) +
                                   avc->parent_log_context_offset);
            if (parent && *parent) {
                av_bprintf(part+0, "[%s @ %p] ",
                         (*parent)->item_name(parent), parent);
                if(type) type[0] = get_category(parent);
            }
        }
        av_bprintf(part+1, "[%s @ %p] ",
                 avc->item_name(avcl), avcl);
        if(type) type[1] = get_category(avcl);

        if (flags & AV_LOG_PRINT_LEVEL)
            av_bprintf(part+2, "[%s] ", get_level_str(level));
    }

    av_vbprintf(part+3, fmt, vl);

    if(*part[0].str || *part[1].str || *part[2].str || *part[3].str) {
        char lastc = part[3].len && part[3].len <= part[3].size ? part[3].str[part[3].len - 1] : 0;
        *print_prefix = lastc == '\n' || lastc == '\r';
    }
}

void av_log_format_line(void *ptr, int level, const char *fmt, va_list vl,
                        char *line, int line_size, int *print_prefix)
{
    AVBPrint part[4];
    format_line(ptr, level, fmt, vl, part, print_prefix, NULL);
    snprintf(line, line_size, "%s%s%s%s", part[0].str, part[1].str, part[2].str, part[3].str);
    av_bprint_finalize(part+3, NULL);
}

void av_log_default_callback(void* ptr, int level, const char* fmt, va_list vl)
{
    static int print_prefix = 1;
    static int count;
    static char prev[LINE_SZ];
    AVBPrint part[4];
    char line[LINE_SZ];
    static int is_atty;
    int type[2];
    unsigned tint = 0;

    if (level >= 0) {
        tint = level & 0xff00;
        level &= 0xff;
    }

    if (level > av_log_level)
        return;
#if HAVE_PTHREADS
    pthread_mutex_lock(&mutex);
#endif

    format_line(ptr, level, fmt, vl, part, &print_prefix, type);
    snprintf(line, sizeof(line), "%s%s%s%s", part[0].str, part[1].str, part[2].str, part[3].str);

#if HAVE_ISATTY
    if (!is_atty)
        is_atty = isatty(2) ? 1 : -1;
#endif

    if (print_prefix && (flags & AV_LOG_SKIP_REPEATED) && !strcmp(line, prev) &&
        *line && line[strlen(line) - 1] != '\r'){
        count++;
        if (is_atty == 1)
            fprintf(stderr, "    Last message repeated %d times\r", count);
        goto end;
    }
    if (count > 0) {
        fprintf(stderr, "    Last message repeated %d times\n", count);
        count = 0;
    }
    strcpy(prev, line);
    sanitize(part[0].str);
    colored_fputs(type[0], 0, part[0].str);
    sanitize(part[1].str);
    colored_fputs(type[1], 0, part[1].str);
    sanitize(part[2].str);
    colored_fputs(av_clip(level >> 3, 0, NB_LEVELS - 1), tint >> 8, part[2].str);
    sanitize(part[3].str);
    colored_fputs(av_clip(level >> 3, 0, NB_LEVELS - 1), tint >> 8, part[3].str);

#if CONFIG_VALGRIND_BACKTRACE
    if (level <= BACKTRACE_LOGLEVEL)
        VALGRIND_PRINTF_BACKTRACE("%s", "");
#endif
end:
    av_bprint_finalize(part+3, NULL);
#if HAVE_PTHREADS
    pthread_mutex_unlock(&mutex);
#endif
}

static void (*av_log_callback)(void*, int, const char*, va_list) =
    av_log_default_callback;

void av_log(void* avcl, int level, const char *fmt, ...)
{
    AVClass* avc = avcl ? *(AVClass **) avcl : NULL;
    va_list vl;
    va_start(vl, fmt);
    if (avc && avc->version >= (50 << 16 | 15 << 8 | 2) &&
        avc->log_level_offset_offset && level >= AV_LOG_FATAL)
        level += *(int *) (((uint8_t *) avcl) + avc->log_level_offset_offset);
    av_vlog(avcl, level, fmt, vl);
    va_end(vl);
}

void av_vlog(void* avcl, int level, const char *fmt, va_list vl)
{
    void (*log_callback)(void*, int, const char*, va_list) = av_log_callback;
    if (log_callback)
        log_callback(avcl, level, fmt, vl);
}

int av_log_get_level(void)
{
    return av_log_level;
}

void av_log_set_level(int level)
{
    av_log_level = level;
}

void av_log_set_flags(int arg)
{
    flags = arg;
}

int av_log_get_flags(void)
{
    return flags;
}

void av_log_set_callback(void (*callback)(void*, int, const char*, va_list))
{
    av_log_callback = callback;
}

static void missing_feature_sample(int sample, void *avc, const char *msg,
                                   va_list argument_list)
{
    av_vlog(avc, AV_LOG_WARNING, msg, argument_list);
    av_log(avc, AV_LOG_WARNING, " is not implemented. Update your FFmpeg "
           "version to the newest one from Git. If the problem still "
           "occurs, it means that your file has a feature which has not "
           "been implemented.\n");
    if (sample)
        av_log(avc, AV_LOG_WARNING, "If you want to help, upload a sample "
               "of this file to ftp://upload.ffmpeg.org/incoming/ "
               "and contact the ffmpeg-devel mailing list. (ffmpeg-devel@ffmpeg.org)\n");
}

void avpriv_request_sample(void *avc, const char *msg, ...)
{
    va_list argument_list;

    va_start(argument_list, msg);
    missing_feature_sample(1, avc, msg, argument_list);
    va_end(argument_list);
}

void avpriv_report_missing_feature(void *avc, const char *msg, ...)
{
    va_list argument_list;

    va_start(argument_list, msg);
    missing_feature_sample(0, avc, msg, argument_list);
    va_end(argument_list);
}
/*---------------------------------------------------------------------------------------*/
#ifdef AV_LOG_EXTENSION

#define QUEUE_SIZE 50
#define LOG_SIZE 1024
typedef struct SeqQueue {
    char  data[QUEUE_SIZE][LOG_SIZE];
    int   front;
    int   rear;
    pthread_mutex_t q_lock;
    pthread_cond_t cond;
    int init_flag;
    int type;   //  1:remote   other:local 
    char  remote_addr[1024];
    int   remote_port;
}Queue;

Queue log_queue;

static Queue *InitQueue(Queue *q) {
    q->front = 0;
    q->rear = 0;
    pthread_mutex_init(&q->q_lock, NULL);      
    pthread_cond_init(&q->cond, NULL);
    return q;
}

static Queue *GetQueue() {
    return &log_queue;
}

static int IsFull(Queue *q) {
    return ((q->rear+1)%QUEUE_SIZE == q->front);
}

static int IsEmpty(Queue *q) {
    return (q->front == q->rear);
}

static void Enqueue(Queue *q, char *d) { 
    pthread_mutex_lock(&q->q_lock);
    if(q && d && !IsFull(q)) {
        int len = 0;
        len = strlen(d);
        len = len>LOG_SIZE?LOG_SIZE:len;
        strncpy(q->data[q->rear], d, len);
        q->data[q->rear][len+1] = '/0';
        q->rear = (q->rear+1)%QUEUE_SIZE;
        pthread_cond_signal(&q->cond);
    }
    pthread_mutex_unlock(&q->q_lock);
}

static char* Dequeue(Queue *q) {
    pthread_mutex_lock(&q->q_lock);
    while(IsEmpty(q)) {
        pthread_cond_wait(&q->cond, &q->q_lock);
    }
    char* tmp = q->data[q->front];
    q->front = (q->front+1)%QUEUE_SIZE;
    pthread_mutex_unlock(&q->q_lock);
    return tmp;
}

static void send_log(Queue *q) {
    struct sockaddr_in sendto_addr;
    int PORT = q->remote_port;
    int smt_fd = -1;
    int send_socket = 0;
    int addr_len = sizeof(struct sockaddr_in);
    memset(&sendto_addr,  0, sizeof(sendto_addr));
    sendto_addr.sin_family = AF_INET;
    sendto_addr.sin_port=htons(PORT);
    sendto_addr.sin_addr.s_addr=inet_addr(q->remote_addr);
    send_socket = socket(AF_INET, SOCK_DGRAM, 0);
    if(-1 == send_socket ) {
        av_log(NULL,AV_LOG_ERROR, "socket create fail !!!!!!\n"); 
    }
 
    while(1) {
        struct timeval udp_delay;
        udp_delay.tv_sec = 0;
        udp_delay.tv_usec = 50; // 50 us
        select(0, NULL, NULL, NULL, &udp_delay);
        char* logdata = Dequeue(q);
        sendto(send_socket, logdata,strlen(logdata)+1, 0,(struct sockaddr *)&sendto_addr, addr_len);
    }
}

void init_av_log_ext() {
    pthread_t send_log_thread;
    send_log_thread= pthread_create(&send_log_thread, NULL, send_log, GetQueue());
}



void set_av_log_device_info(char* info) {
    if(!info) return;
    if(NULL != device_information) {
        free(device_information);
        device_information = NULL;
    }
    device_information = (char*)malloc(strlen(info) +1);
    strcpy(device_information, info);
}

char* get_av_log_device_info(void) {
    return device_information; 
}


void set_av_log_ext(char* dest) {
    if(!dest) return;
    Queue *q = GetQueue();
    if(0 == strcmp(dest, "local")) {
        q->type = 0;
    } else {
        char *sep = NULL;
        q->type = 1;
        sep = strstr(dest, ":");
        if(NULL == sep) {
            strcpy(q->remote_addr, dest);
            q->remote_port = 7878;
        } else {
            strncpy(q->remote_addr, dest, sep - dest);
            q->remote_addr[sep - dest] = '\0';
            q->remote_port = atoi(sep+1);
        }
    }
    pthread_mutex_lock(&q->q_lock);
    if(q->init_flag == 0) {
        init_av_log_ext();
        q->init_flag = 1;
    }
    pthread_mutex_unlock(&q->q_lock);
}

void av_log_ext(void* avcl, int level, const char *fmt, ...){
    static int print_prefix = 1;
    Queue *q = GetQueue();
    va_list vl;
    va_start(vl, fmt);
    //av_log_default_callback(NULL, level, fmt, vl);
    if(1 == q->type) {
        pthread_mutex_lock(&q->q_lock);
        if(q && !IsFull(q)) {
            char* line = q->data[q->rear];
            av_log_format_line(NULL, level, fmt, vl, line, LOG_SIZE, &print_prefix);
            q->rear = (q->rear+1)%QUEUE_SIZE;
            pthread_cond_signal(&q->cond);
        } else {
            av_log(NULL,AV_LOG_ERROR, "ext log queue is full!!\n"); 
        }
        pthread_mutex_unlock(&q->q_lock);
    }
    va_end(vl);
}

#endif /*RAV_LOG_EXTENSION*/ 

#ifdef TEST
// LCOV_EXCL_START
#include <string.h>

int main(int argc, char **argv)
{
    int i;
    av_log_set_level(AV_LOG_DEBUG);
    for (use_color=0; use_color<=256; use_color = 255*use_color+1) {
        av_log(NULL, AV_LOG_FATAL, "use_color: %d\n", use_color);
        for (i = AV_LOG_DEBUG; i>=AV_LOG_QUIET; i-=8) {
            av_log(NULL, i, " %d", i);
            av_log(NULL, AV_LOG_INFO, "e ");
            av_log(NULL, i + 256*123, "C%d", i);
            av_log(NULL, AV_LOG_INFO, "e");
        }
        av_log(NULL, AV_LOG_PANIC, "\n");
    }
    return 0;
}
// LCOV_EXCL_STOP
#endif
