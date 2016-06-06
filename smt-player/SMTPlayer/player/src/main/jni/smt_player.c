#include "libavformat/avformat.h"
#include "libswscale/swscale.h"
#include "libswresample/swresample.h"
#include "libavutil/fifo.h"

#include "org_sjtu_nerdtv_smt_player_jni_SmtNativeApi.h"

/********
// definition
********/
#define USE_OPENGL 1
#define USE_SDL 0
#define VIDEO_PICTURE_QUEUE_SIZE 3
#define SAMPLE_QUEUE_SIZE 9
#define FRAME_QUEUE_SIZE FFMAX(SAMPLE_QUEUE_SIZE, VIDEO_PICTURE_QUEUE_SIZE)
#define MAX_QUEUE_SIZE (15 * 1024 * 1024)
#define MAX_AUDIO_FRAME_SIZE 192000 // 1 second of 48khz 32bit audio

#if USE_SDL
#include <SDL2/SDL.h>
static SDL_Window *smt_window = NULL;
#endif

#if USE_OPENGL
#include <pthread.h>
#include "smt_render.h"
#endif

#define SMT_BRAND "SMTPLAYER"

static AVPacket flush_pkt;
//static const char *smt_input_filename = "smt://@:5501";
static const char *smt_input_filename = "/mnt/sdcard/h264.mp4";

typedef struct Frame {
    AVFrame *frame;
    AVSubtitle sub;
    AVSubtitleRect **subrects;  /* rescaled subtitle rectangles in yuva */
    int serial;
    double pts;           /* presentation timestamp for the frame */
    double duration;      /* estimated duration of the frame */
    int64_t pos;          /* byte position of the frame in the input file */
    int allocated;
    int reallocate;
    int width;
    int height;
    AVRational sar;
} Frame;

typedef struct MyAVPacketList {
    AVPacket pkt;
    struct MyAVPacketList *next;
    int serial;
} MyAVPacketList;

typedef struct PacketQueue {
    MyAVPacketList *first_pkt, *last_pkt;
    int nb_packets;
    int size;
    int abort_request;
    int serial;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
} PacketQueue;

typedef struct FrameQueue {
    Frame queue[FRAME_QUEUE_SIZE];
    int rindex;
    int windex;
    int size;
    int max_size;
    int keep_last;
    int rindex_shown;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    PacketQueue *pktq;
} FrameQueue;

typedef struct SmtState {
    pthread_t smt_read_tid, smt_video_decode_tid, smt_audio_decode_tid, smt_video_display_tid;
    int smt_width, smt_height, smt_xleft, smt_ytop;
    FrameQueue smt_pictq, smt_sampq;
    PacketQueue smt_videoq, smt_audioq;
    AVFormatContext *smt_pFormatCtx;
    int smt_video_stream;
    int smt_audio_stream;
    int smt_end;
    SwrContext *swr_ctx;
    AVFifoBuffer *audio_buffer;
} SmtState;



/*****************
// packet queue
*****************/

static int packet_queue_put_private(PacketQueue *q, AVPacket *pkt)
{
    MyAVPacketList *pkt1;

    if (q->abort_request)
       return -1;

    pkt1 = av_malloc(sizeof(MyAVPacketList));
    if (!pkt1)
        return -1;
    pkt1->pkt = *pkt;
    pkt1->next = NULL;
    if (pkt == &flush_pkt)
        q->serial++;
    pkt1->serial = q->serial;

    if (!q->last_pkt)
        q->first_pkt = pkt1;
    else
        q->last_pkt->next = pkt1;
    q->last_pkt = pkt1;
    q->nb_packets++;
    q->size += pkt1->pkt.size + sizeof(*pkt1);
    pthread_cond_signal(&q->cond);
    return 0;
}

static int packet_queue_put(PacketQueue *q, AVPacket *pkt)
{
    int ret;

    pthread_mutex_lock(&q->mutex);
    ret = packet_queue_put_private(q, pkt);
    pthread_mutex_unlock(&q->mutex);

    if (pkt != &flush_pkt && ret < 0)
        av_packet_unref(pkt);

    return ret;
}

/* packet queue handling */
static int packet_queue_init(PacketQueue *q)
{
    int ret;
    memset(q, 0, sizeof(PacketQueue));
    ret = pthread_mutex_init(&q->mutex, NULL);
    if (ret != 0) {
        av_log(NULL, AV_LOG_FATAL, "create packet queue mutex failed.\n");
        return AVERROR(ENOMEM);
    }
    ret = pthread_cond_init(&q->cond, NULL);
    if (ret != 0) {
        av_log(NULL, AV_LOG_FATAL, "create packet queue mutex condition failed\n");
        return AVERROR(ENOMEM);
    }
    q->abort_request = 1;
    return 0;
}

static int packet_queue_put_nullpacket(PacketQueue *q, int stream_index)
{
    AVPacket pkt1, *pkt = &pkt1;
    av_init_packet(pkt);
    pkt->data = NULL;
    pkt->size = 0;
    pkt->stream_index = stream_index;
    return packet_queue_put(q, pkt);
}

static void packet_queue_flush(PacketQueue *q)
{
    MyAVPacketList *pkt, *pkt1;

    pthread_mutex_lock(&q->mutex);
    for (pkt = q->first_pkt; pkt; pkt = pkt1) {
        pkt1 = pkt->next;
        av_packet_unref(&pkt->pkt);
        av_freep(&pkt);
    }
    q->last_pkt = NULL;
    q->first_pkt = NULL;
    q->nb_packets = 0;
    q->size = 0;
    pthread_mutex_unlock(&q->mutex);
}

static void packet_queue_destroy(PacketQueue *q)
{
    packet_queue_flush(q);
    pthread_mutex_destroy(&q->mutex);
    pthread_cond_destroy(&q->cond);
}

static void packet_queue_abort(PacketQueue *q)
{
    pthread_mutex_lock(&q->mutex);

    q->abort_request = 1;

    pthread_cond_signal(&q->cond);

    pthread_mutex_unlock(&q->mutex);
}

static void packet_queue_start(PacketQueue *q)
{
    pthread_mutex_lock(&q->mutex);
    q->abort_request = 0;
    packet_queue_put_private(q, &flush_pkt);
    pthread_mutex_unlock(&q->mutex);
}

/* return < 0 if aborted, 0 if no packet and > 0 if packet.  */
static int packet_queue_get(PacketQueue *q, AVPacket *pkt, int block, int *serial)
{
    MyAVPacketList *pkt1;
    int ret;

    pthread_mutex_lock(&q->mutex);

    for (;;) {
        if (q->abort_request) {
            ret = -1;
            break;
        }

        pkt1 = q->first_pkt;
        if (pkt1) {
            q->first_pkt = pkt1->next;
            if (!q->first_pkt)
                q->last_pkt = NULL;
            q->nb_packets--;
            q->size -= pkt1->pkt.size + sizeof(*pkt1);
            *pkt = pkt1->pkt;
            if (serial)
                *serial = pkt1->serial;
            av_free(pkt1);
            ret = 1;
            break;
        } else if (!block) {
            ret = 0;
            break;
        } else {
            pthread_cond_wait(&q->cond, &q->mutex);
        }
    }
    pthread_mutex_unlock(&q->mutex);
    return ret;
}


/***********************
// frame queue
***********************/

static void frame_queue_unref_item(Frame *vp)
{
    int i;
    for (i = 0; i < vp->sub.num_rects; i++) {
        av_freep(&vp->subrects[i]->data[0]);
        av_freep(&vp->subrects[i]);
    }
    av_freep(&vp->subrects);
    av_frame_unref(vp->frame);
    avsubtitle_free(&vp->sub);
}

static int frame_queue_init(FrameQueue *f, PacketQueue *pktq, int max_size, int keep_last)
{
    int i;
    memset(f, 0, sizeof(FrameQueue));
    if (pthread_mutex_init(&f->mutex, NULL)) {
        av_log(NULL, AV_LOG_FATAL, "create frame queue mutex failed\n");
        return AVERROR(ENOMEM);
    }
    if (pthread_cond_init(&f->cond, NULL)) {
        av_log(NULL, AV_LOG_FATAL, "create frame queue mutex condition failed\n");
        return AVERROR(ENOMEM);
    }
    f->pktq = pktq;
    f->max_size = FFMIN(max_size, FRAME_QUEUE_SIZE);
    f->keep_last = !!keep_last;
    for (i = 0; i < f->max_size; i++)
        if (!(f->queue[i].frame = av_frame_alloc()))
            return AVERROR(ENOMEM);
    return 0;
}

static void frame_queue_destory(FrameQueue *f)
{
    int i;
    for (i = 0; i < f->max_size; i++) {
        Frame *vp = &f->queue[i];
        frame_queue_unref_item(vp);
        av_frame_free(&vp->frame);
    }
    pthread_mutex_destroy(&f->mutex);
    pthread_cond_destroy(&f->cond);
}

static void frame_queue_signal(FrameQueue *f)
{
    pthread_mutex_lock(&f->mutex);
    pthread_cond_signal(&f->cond);
    pthread_mutex_unlock(&f->mutex);
}

static Frame *frame_queue_peek(FrameQueue *f)
{
    return &f->queue[(f->rindex + f->rindex_shown) % f->max_size];
}

static Frame *frame_queue_peek_next(FrameQueue *f)
{
    return &f->queue[(f->rindex + f->rindex_shown + 1) % f->max_size];
}

static Frame *frame_queue_peek_last(FrameQueue *f)
{
    return &f->queue[f->rindex];
}

static Frame *frame_queue_peek_writable(FrameQueue *f)
{
    /* wait until we have space to put a new frame */
    pthread_mutex_lock(&f->mutex);
    while (f->size >= f->max_size &&
           !f->pktq->abort_request) {
        pthread_cond_wait(&f->cond, &f->mutex);
    }
    pthread_mutex_unlock(&f->mutex);

    if (f->pktq->abort_request)
        return NULL;

    return &f->queue[f->windex];
}

static Frame *frame_queue_peek_readable(FrameQueue *f)
{
    /* wait until we have a readable a new frame */
    pthread_mutex_lock(&f->mutex);
    while (f->size - f->rindex_shown <= 0 &&
           !f->pktq->abort_request) {
        pthread_cond_wait(&f->cond, &f->mutex);
    }
    pthread_mutex_unlock(&f->mutex);

    if (f->pktq->abort_request)
        return NULL;

    return &f->queue[(f->rindex + f->rindex_shown) % f->max_size];
}

static void frame_queue_push(FrameQueue *f)
{
    if (++f->windex == f->max_size)
        f->windex = 0;
    pthread_mutex_lock(&f->mutex);
    f->size++;
    pthread_cond_signal(&f->cond);
    pthread_mutex_unlock(&f->mutex);
}

static void frame_queue_next(FrameQueue *f)
{
    if (f->keep_last && !f->rindex_shown) {
        f->rindex_shown = 1;
        return;
    }
    frame_queue_unref_item(&f->queue[f->rindex]);
    if (++f->rindex == f->max_size)
        f->rindex = 0;
    pthread_mutex_lock(&f->mutex);
    f->size--;
    pthread_cond_signal(&f->cond);
    pthread_mutex_unlock(&f->mutex);
}

/* jump back to the previous frame if available by resetting rindex_shown */
static int frame_queue_prev(FrameQueue *f)
{
    int ret = f->rindex_shown;
    f->rindex_shown = 0;
    return ret;
}

/* return the number of undisplayed frames in the queue */
static int frame_queue_nb_remaining(FrameQueue *f)
{
    return f->size - f->rindex_shown;
}

/* return last shown position */
static int64_t frame_queue_last_pos(FrameQueue *f)
{
    Frame *fp = &f->queue[f->rindex];
    if (f->rindex_shown && fp->serial == f->pktq->serial)
        return fp->pos;
    else
        return -1;
}


/************
// player
************/
#if USE_SDL
static void calculate_display_rect(SDL_Rect *rect,
                                   int scr_xleft, int scr_ytop, int scr_width, int scr_height,
                                   int pic_width, int pic_height, AVRational pic_sar)
{
    float aspect_ratio;
    int width, height, x, y;

    if (pic_sar.num == 0)
        aspect_ratio = 0;
    else
        aspect_ratio = av_q2d(pic_sar);

    if (aspect_ratio <= 0.0)
        aspect_ratio = 1.0;
    aspect_ratio *= (float)pic_width / (float)pic_height;

    /* XXX: we suppose the screen has a 1.0 pixel ratio */
    height = scr_height;
    width = lrint(height * aspect_ratio) & ~1;
    if (width > scr_width) {
        width = scr_width;
        height = lrint(width / aspect_ratio) & ~1;
    }
    x = (scr_width - width) / 2;
    y = (scr_height - height) / 2;
    rect->x = scr_xleft + x;
    rect->y = scr_ytop  + y;
    rect->w = FFMAX(width,  1);
    rect->h = FFMAX(height, 1);
}
#endif


static void* smt_video_display_thread(void *arg)
{
    SmtState *is = (SmtState *)arg;
    Frame *pFrameYUV;
    av_log(NULL, AV_LOG_INFO, "smt video display thread start.\n");
    
#if USE_SDL
    SDL_Window *window;   
    SDL_Renderer* renderer;  
    SDL_Texture* texture;  
    SDL_Rect rect;

    window = SDL_CreateWindow("", 0, 0,  
        is->smt_width, is->smt_height,  
        SDL_WINDOW_OPENGL | SDL_WINDOW_BORDERLESS | SDL_WINDOW_INPUT_FOCUS);  
    
    if(!window) {    
        av_log(NULL, AV_LOG_ERROR, "SDL: could not create window - exiting:%s\n",SDL_GetError());    
        return NULL;  
    }

    smt_window = window;
    
    renderer = SDL_CreateRenderer(window, -1, 0);    
 
    texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_IYUV, SDL_TEXTUREACCESS_STREAMING,is->smt_width,is->smt_height);    
    
    rect.x=0;  
    rect.y=0;  
    rect.w=is->smt_width;  
    rect.h=is->smt_height;  
#endif 

#if USE_OPENGL
    int window;
    SMTRenderContext gl_ctx;
    gl_ctx.av_class = &opengl_class;
    gl_ctx.width = is->smt_width;
    gl_ctx.height = is->smt_height;
#if !defined(__ANDROID__)
    glutInitWindowPosition(0, 0);
    glutInitWindowSize(is->smt_width, is->smt_height);
    window = glutCreateWindow("");
#endif
    if(!smt_gl_setup(&gl_ctx, AV_PIX_FMT_YUV420P)){
        av_log(NULL, AV_LOG_INFO, "gl render failed.\n");
        return NULL;
    }
#endif

    while(!is->smt_end){
        unsigned delay;
		if(!(pFrameYUV = frame_queue_peek_readable(&is->smt_pictq)))
			break;
        if(!pFrameYUV->frame)
            break;
#if USE_SDL
        calculate_display_rect(&rect, is->smt_xleft, is->smt_ytop, is->smt_width, is->smt_height, pFrameYUV->width, pFrameYUV->height, pFrameYUV->sar);
        SDL_UpdateYUVTexture(texture, &rect,  
        pFrameYUV->frame->data[0], pFrameYUV->frame->linesize[0],  
        pFrameYUV->frame->data[1], pFrameYUV->frame->linesize[1],  
        pFrameYUV->frame->data[2], pFrameYUV->frame->linesize[2]);  
          
        SDL_RenderClear( renderer );    
        SDL_RenderCopy( renderer, texture,  NULL, &rect);    
        SDL_RenderPresent( renderer );
#endif

#if USE_OPENGL
        smt_gl_draw(&gl_ctx, AV_PIX_FMT_YUV420P, is->smt_width, is->smt_height, (void *)pFrameYUV->frame->data);
#endif
        av_freep(&pFrameYUV->frame->data);
        av_frame_unref(pFrameYUV->frame);
        frame_queue_next(&is->smt_pictq);
        delay = pFrameYUV->duration*1000*1000;
        av_usleep(delay); 
    }
#if USE_SDL
    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
#endif 
    av_log(NULL, AV_LOG_INFO, "smt video display thread end.\n");
    return NULL;
}

static void smt_audio_callback_thread(void *arg, uint8_t * stream, int len)
{
#if USE_SDL
    Frame *pFramePCM;
    int pos = 0;
    SmtState *is = (SmtState *)arg;
    AVCodecContext *pCodecCtx=is->smt_pFormatCtx->streams[is->smt_audio_stream]->codec;
    uint8_t *databuffer = NULL;
    int     datalen = 0;

    SDL_memset(stream, 0, len);
    if(len == 0)
        return;
    
    if(is->audio_buffer){
        int size = av_fifo_size(is->audio_buffer);
        if(size >= len){
            av_fifo_generic_read(is->audio_buffer, stream, len, NULL);
            return;
        }else if(size > 0){
            av_fifo_generic_read(is->audio_buffer, stream, size, NULL);
            len -= size;
            pos += size;
        }
    }
    
    while(len > 0){
        int desired_len, converted_len;
        if(!(pFramePCM = frame_queue_peek_readable(&is->smt_sampq)))
			break;
        if(!pFramePCM->frame)
            break;
        desired_len = av_samples_get_buffer_size(NULL, pCodecCtx->channels, pFramePCM->frame->nb_samples + 256, pCodecCtx->sample_fmt, 1);
        if(datalen < desired_len)
            av_fast_mallocz(&databuffer, &datalen, desired_len);
        if(is->swr_ctx){
            int nu = swr_convert(is->swr_ctx, &databuffer, pFramePCM->frame->nb_samples + 256,(const uint8_t **)pFramePCM->frame->data, pFramePCM->frame->nb_samples);
            if(nu <= 0){
                av_log(NULL, AV_LOG_ERROR, "resample failed. error code: %d\n", nu);
                av_frame_unref(pFramePCM->frame);
                frame_queue_next(&is->smt_sampq);
                continue;
            }
            converted_len = nu * pCodecCtx->channels * av_get_bytes_per_sample(pCodecCtx->sample_fmt);
        }
        av_frame_unref(pFramePCM->frame);
        frame_queue_next(&is->smt_sampq);
        if(len >= converted_len){
            memcpy(stream + pos, databuffer, converted_len);
            len -= converted_len;
            pos += converted_len;
        }else{
            int extra_data_len = converted_len - len;
            memcpy(stream + pos, databuffer, len);
            if(!is->audio_buffer){
                is->audio_buffer = av_fifo_alloc(extra_data_len);
            }else{
                int space = av_fifo_space(is->audio_buffer);
                if(space < extra_data_len)
                    av_fifo_grow(is->audio_buffer, extra_data_len - space);
            }
            av_fifo_generic_write(is->audio_buffer, databuffer + len, extra_data_len, NULL);
            av_log(NULL, AV_LOG_WARNING, "audio has extra data with len = %d\n", extra_data_len);
            len = 0;
        }

    }
    if(databuffer)
        av_freep(&databuffer);
#endif
}

static void* smt_audio_decode_thread(void *arg)
{
#if USE_SDL
    SmtState *is = (SmtState *)arg;
    AVCodecContext  *pCodecCtx; 
    AVCodec         *pCodec;  
    SDL_AudioSpec wanted_spec, obtained_spec;
    int ret, got_sample;
    AVFrame *pFrame = av_frame_alloc(); 
    Frame *pFramePCM;
    int begin = 0;
    
    av_log(NULL, AV_LOG_INFO, "smt audio decode thread start.\n");
    pCodecCtx=is->smt_pFormatCtx->streams[is->smt_audio_stream]->codec; 
    pCodec=avcodec_find_decoder(pCodecCtx->codec_id); 

    if(pCodec==NULL){  
        av_log(NULL, AV_LOG_ERROR, "Codec not found.\n");  
        return NULL;  
    }  
    if(avcodec_open2(pCodecCtx, pCodec,NULL)<0){  
        av_log(NULL, AV_LOG_ERROR, "Could not open codec.\n");  
        return NULL;  
    }

    wanted_spec.freq = pCodecCtx->sample_rate;
    wanted_spec.channels = pCodecCtx->channels;
    if (wanted_spec.freq <= 0 || wanted_spec.channels <= 0) {
        av_log(NULL, AV_LOG_ERROR, "Invalid sample rate or channel count!\n");
        return NULL;
    }

    wanted_spec.format = AUDIO_S16SYS;
    wanted_spec.callback = smt_audio_callback_thread;
    wanted_spec.userdata = is;
    wanted_spec.silence = 0;
    wanted_spec.samples = 4096;

    if(SDL_OpenAudio(&wanted_spec, &obtained_spec) < 0){
        av_log(NULL, AV_LOG_ERROR, "Cannot open audio!\n");
        return NULL;
    }

    if(pCodecCtx->sample_fmt != AV_SAMPLE_FMT_S16){
        is->swr_ctx = swr_alloc_set_opts(NULL, pCodecCtx->channel_layout, AV_SAMPLE_FMT_S16, pCodecCtx->sample_rate, 
                pCodecCtx->channel_layout, pCodecCtx->sample_fmt, pCodecCtx->sample_rate, 0, NULL);

        swr_init(is->swr_ctx);
    }
    
    packet_queue_start(&is->smt_audioq);

    for(;;){
        AVPacket packet;
        if(is->smt_end)
            break;
        if (packet_queue_get(&is->smt_audioq, &packet, 1, &is->smt_audioq.serial) < 0){
            av_log(NULL, AV_LOG_ERROR, "get packet error.\n");  
            return NULL;
        }
        ret = avcodec_decode_audio4(pCodecCtx, pFrame, &got_sample, &packet);
        if(ret < 0){
            frame_queue_push(&is->smt_sampq);
            av_packet_unref(&packet);
            av_frame_unref(pFrame);
            av_log(NULL, AV_LOG_ERROR, "Audio Decode Error.\n");  
            continue;  
        }
        if(got_sample){
            AVRational tb = (AVRational){1, pFrame->sample_rate};
            if (!(pFramePCM = frame_queue_peek_writable(&is->smt_sampq)))
                break;

            pFramePCM->pts = (pFrame->pts == AV_NOPTS_VALUE) ? NAN : pFrame->pts * av_q2d(tb);
            pFramePCM->pos = av_frame_get_pkt_pos(pFrame);
            pFramePCM->serial = is->smt_audioq.serial;
            pFramePCM->duration = av_q2d((AVRational){pFrame->nb_samples, pFrame->sample_rate});

            av_frame_move_ref(pFramePCM->frame, pFrame);
            frame_queue_push(&is->smt_sampq);
            if(!begin){
                SDL_PauseAudio(0);
                begin = 1;
            }
        }
        av_packet_unref(&packet);
    }
    av_frame_unref(pFrame);
    if(is->swr_ctx)
        swr_free(&is->swr_ctx);
    av_log(NULL, AV_LOG_INFO, "smt audio decode thread end.\n");
#endif
}

static void* smt_video_decode_thread(void *arg)
{
    SmtState *is = (SmtState *)arg;
    AVCodecContext  *pCodecCtx; 
    AVCodec         *pCodec;  
    struct SwsContext *img_convert_ctx;
    AVFrame *pFrame = av_frame_alloc(); 
    Frame *pFrameYUV;
    int ret, got_picture;
    unsigned char *out_buffer;
	//AVRational tb = is->smt_pFormatCtx->streams[is->smt_video_stream]->time_base;
    AVRational frame_rate = av_guess_frame_rate(is->smt_pFormatCtx, is->smt_pFormatCtx->streams[is->smt_video_stream], NULL);


    av_log(NULL, AV_LOG_INFO, "smt video decode thread start.\n");
    pCodecCtx=is->smt_pFormatCtx->streams[is->smt_video_stream]->codec;  
    pCodec=avcodec_find_decoder(pCodecCtx->codec_id);  

    is->smt_width = pCodecCtx->width/4;  
    is->smt_height = pCodecCtx->height/4;
    
    if(pCodec==NULL){  
        av_log(NULL, AV_LOG_ERROR, "Codec not found.\n");  
        return NULL;  
    }  
    if(avcodec_open2(pCodecCtx, pCodec,NULL)<0){  
        av_log(NULL, AV_LOG_ERROR, "Could not open codec.\n");  
        return NULL;  
    }  

    img_convert_ctx = sws_getContext(pCodecCtx->width, pCodecCtx->height, pCodecCtx->pix_fmt,   
            is->smt_width, is->smt_height, AV_PIX_FMT_YUV420P, SWS_BICUBIC, NULL, NULL, NULL);  
    
    packet_queue_start(&is->smt_videoq);
    for(;;){
        AVPacket packet;
        if(is->smt_end){
            pthread_join(is->smt_video_display_tid, NULL);
            break;
        }
        if (packet_queue_get(&is->smt_videoq, &packet, 1, &is->smt_videoq.serial) < 0){
            av_log(NULL, AV_LOG_ERROR, "get packet error.\n");  
            return NULL;
        }

        ret = avcodec_decode_video2(pCodecCtx, pFrame, &got_picture, &packet);
        if(ret < 0){
            frame_queue_push(&is->smt_pictq);
            av_packet_unref(&packet);
            av_frame_unref(pFrame);
            av_log(NULL, AV_LOG_ERROR, "Video Decode Error.\n");  
            continue;  
        }

        if(got_picture){
            if (!(pFrameYUV = frame_queue_peek_writable(&is->smt_pictq)))
                break;
            out_buffer=(unsigned char *)av_mallocz(av_image_get_buffer_size(AV_PIX_FMT_YUV420P,  is->smt_width, is->smt_height,1));  
            av_image_fill_arrays(pFrameYUV->frame->data, pFrameYUV->frame->linesize,out_buffer,  
                AV_PIX_FMT_YUV420P,is->smt_width, is->smt_height,1);  
            sws_scale(img_convert_ctx, (const unsigned char* const*)pFrame->data, pFrame->linesize, 0, pCodecCtx->height,   
                pFrameYUV->frame->data, pFrameYUV->frame->linesize);
            pFrameYUV->sar = pFrame->sample_aspect_ratio;
            pFrameYUV->width = is->smt_width;
            pFrameYUV->height = is->smt_height;
			pFrameYUV->duration = (frame_rate.num && frame_rate.den ? av_q2d((AVRational){frame_rate.den, frame_rate.num}) : 0);
			frame_queue_push(&is->smt_pictq);
            if(!is->smt_video_display_tid)
                pthread_create(&is->smt_video_display_tid, NULL, smt_video_display_thread, is);
        }
        av_packet_unref(&packet);
        av_frame_unref(pFrame);
    }
    sws_freeContext(img_convert_ctx);
    avcodec_close(pCodecCtx);
    frame_queue_destory(&is->smt_pictq);
    av_frame_free(&pFrame);
    av_log(NULL, AV_LOG_INFO, "smt video decode thread end.\n");
    return NULL;
}



static void* smt_read_thread(void *arg)
{
        SmtState *is = (SmtState *)arg;
        int i;   
        AVPacket *packet = av_packet_alloc();  
        int y_size;  
        int ret, got_picture;  
        
        av_log(NULL, AV_LOG_INFO, "smt video read thread start.\n");
        is->smt_pFormatCtx = avformat_alloc_context();  
      
        if(avformat_open_input(&is->smt_pFormatCtx,smt_input_filename,NULL,NULL)!=0){  
            av_log(NULL, AV_LOG_ERROR, "Couldn't open input stream.\n");  
            return NULL;  
        }  
        if(avformat_find_stream_info(is->smt_pFormatCtx,NULL)<0){  
            av_log(NULL, AV_LOG_ERROR, "Couldn't find stream information.\n");  
            return NULL;  
        }  
        is->smt_video_stream = -1;  
        is->smt_audio_stream = -1;
        for(i=0; i<is->smt_pFormatCtx->nb_streams; i++)   
            if(is->smt_pFormatCtx->streams[i]->codec->codec_type==AVMEDIA_TYPE_VIDEO)
                is->smt_video_stream = i;
            else if(is->smt_pFormatCtx->streams[i]->codec->codec_type==AVMEDIA_TYPE_AUDIO)
                is->smt_audio_stream = i;
            
        if(is->smt_video_stream == -1)
            av_log(NULL, AV_LOG_WARNING, "Didn't find a video stream.\n");

        if(is->smt_audio_stream == -1)
            av_log(NULL, AV_LOG_WARNING, "Didn't find a audio stream.\n");
      
        av_dump_format(is->smt_pFormatCtx,0,smt_input_filename,0);  
         
        for(;;){
            if (is->smt_videoq.size + is->smt_audioq.size > MAX_QUEUE_SIZE ) {
                /* wait 10 ms */
                av_usleep(10*1000);
                continue;
            }

            if(is->smt_end || av_read_frame(is->smt_pFormatCtx, packet) < 0){
                pthread_join(is->smt_audio_decode_tid, NULL);
                pthread_join(is->smt_video_decode_tid, NULL);
                break;
            }
            if(packet->stream_index==is->smt_video_stream){
                packet_queue_put(&is->smt_videoq, packet);
                if(!is->smt_video_decode_tid)
                    pthread_create(&is->smt_video_decode_tid, NULL, smt_video_decode_thread, is);
            }else if(packet->stream_index==is->smt_audio_stream){
                packet_queue_put(&is->smt_audioq, packet);
                if(!is->smt_audio_decode_tid)
                    pthread_create(&is->smt_audio_decode_tid, NULL, smt_audio_decode_thread, is);
            }else
                av_packet_unref(packet);

        }
        
        av_packet_free(&packet);
        avformat_close_input(&is->smt_pFormatCtx); 
        packet_queue_destroy(&is->smt_videoq);
        av_log(NULL, AV_LOG_INFO, "smt video read thread end.\n");
        return NULL;  

}

static void smt_close(SmtState *is)
{
    is->smt_end = 1;
    av_log(NULL, AV_LOG_QUIET, "%s", "");
    pthread_join(is->smt_read_tid, NULL);
    av_fifo_freep(&is->audio_buffer);
    packet_queue_destroy(&is->smt_videoq);
    frame_queue_destory(&is->smt_pictq);
    packet_queue_destroy(&is->smt_audioq);
    frame_queue_destory(&is->smt_sampq);
#if USE_SDL
    SDL_Quit();
#endif
    exit(0);
}

#if USE_SDL
static void smt_event_loop(SmtState *is)
{
    SDL_Event event;
    while(!is->smt_end){
        SDL_PumpEvents();
        while (!SDL_PeepEvents(&event, 1, SDL_GETEVENT, SDL_FIRSTEVENT, SDL_LASTEVENT)) {
            SDL_PumpEvents();
        }
        switch (event.type) {
            case SDL_KEYDOWN:
            {
                switch (event.key.keysym.sym) {
                    case SDLK_ESCAPE:
                        smt_close(is);
                        break;
                }
            }
            break;
        }
        av_usleep(1000*1000);
    }
}
#endif

static int smt_open(int argc, char** argv)
{
    int flags;
    SmtState *is = av_mallocz(sizeof(SmtState));
    
    av_log_set_flags(AV_LOG_SKIP_REPEATED);
    avdevice_register_all();
    avfilter_register_all();
    av_register_all();
    avformat_network_init();

#if USE_SDL
    flags = SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER;
    if (SDL_Init (flags))
        return -1;      
    SDL_EventState(SDL_SYSWMEVENT, SDL_IGNORE);
    SDL_EventState(SDL_USEREVENT, SDL_IGNORE);
#endif

#if USE_OPENGL && !defined(__ANDROID__)
    flags = GLUT_DOUBLE | GLUT_RGB;
    glutInit(&argc, argv);
    glutInitDisplayMode(flags);
#endif
    is->smt_end = 0;
    if (frame_queue_init(&is->smt_pictq, &is->smt_videoq, VIDEO_PICTURE_QUEUE_SIZE, 1) < 0 ||
        frame_queue_init(&is->smt_sampq, &is->smt_audioq, SAMPLE_QUEUE_SIZE, 1) < 0)
        return -1;

    if (packet_queue_init(&is->smt_videoq) < 0 ||
        packet_queue_init(&is->smt_audioq) < 0){
        frame_queue_destory(&is->smt_pictq);
        frame_queue_destory(&is->smt_sampq);
        return -1;
    }
    packet_queue_flush(&is->smt_videoq);
    packet_queue_flush(&is->smt_audioq);
    packet_queue_put(&is->smt_videoq, &flush_pkt);
    packet_queue_put(&is->smt_audioq, &flush_pkt);
    is->smt_video_display_tid = is->smt_video_decode_tid = is->smt_read_tid = 0;
    pthread_create(&is->smt_read_tid, NULL, smt_read_thread, is);
#if USE_SDL
    smt_event_loop(is);
#endif

#if USE_OPENGL && !defined(__ANDROID__)
    glutMainLoop();
#endif
    pthread_join(is->smt_read_tid, NULL);
    return 0;
}

int main(int argc, char** argv){
    
    smt_open(argc, argv);
    return 0;

}

//jni interface
JNIEXPORT jstring JNICALL Java_org_sjtu_nerdtv_smt_player_jni_SmtNativeApi_getSmt(JNIEnv *env, jclass obj)
{
    return (*env)->NewStringUTF(env, SMT_BRAND);
}

JNIEXPORT void JNICALL Java_org_sjtu_nerdtv_smt_player_jni_SmtNativeApi_play(JNIEnv * env, jobject obj)
{
    char *program_name = "smtplayer";
    av_log(NULL, AV_LOG_WARNING, "start up program smt player!!!!");
    smt_open(1, &program_name);
}

