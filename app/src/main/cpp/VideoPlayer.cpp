//
// Created by steven on 2019-07-16.
//



#include "VideoPlayer.h"
#include "AudioPlayer.h"
#include <android/log.h>

#define LOGE(FORMAT, ...) __android_log_print(ANDROID_LOG_ERROR,"LC XXX",FORMAT,##__VA_ARGS__);

struct TimeStruct {
    double  last_play;  //上一帧的播放时间
    double play;             //当前帧的播放时间
    double last_delay;   // 上一次播放视频的两帧视频间隔时间
    double delay;         //两帧视频间隔时间
    double audio_clock; //音频轨道 实际播放时间
    double diff;   //音频帧与视频帧相差时间
    double sync_threshold;
    double start_time;  //从第一帧开始的绝对时间
    double pts;
    double actual_delay;//真正需要延迟时间
} ts;

static void (*video_call)(AVFrame *frame);

void *videoPlay(void *args) {
    VideoPlayer *videoPlayer = (VideoPlayer*) args;
    AVFrame *srcFrame = av_frame_alloc();
    AVFrame *rgbFrame = av_frame_alloc();
    AVPacket *avPacket = (AVPacket*) av_mallocz(sizeof(AVPacket));
    // 数据格式转换准备
    // 输出 Buffer
    //Todo Bug待修复
    int buffer_size = av_image_get_buffer_size(AV_PIX_FMT_RGBA, videoPlayer->codec->width, videoPlayer->codec->height, 1);
    // R8 申请 Buffer 内存
    std::uint8_t *out_buffer = (uint8_t *) av_malloc(buffer_size * sizeof(uint8_t));
    av_image_fill_arrays(rgbFrame->data, rgbFrame->linesize, out_buffer, AV_PIX_FMT_RGBA,
                         videoPlayer->codec->width, videoPlayer->codec->height, 1);

    // R9 数据格式转换上下文
    videoPlayer->swsContext = sws_getContext(videoPlayer->codec->width,videoPlayer->codec->height,videoPlayer->codec->pix_fmt,
                                             videoPlayer->codec->width,videoPlayer->codec->height,AV_PIX_FMT_RGBA,
                                             SWS_BICUBIC,NULL,NULL,NULL);
    //从第一帧开始的绝对时间
    ts.start_time = av_gettime() / 1000000.0;
    while (videoPlayer->isPlay) {
        videoPlayer->get(avPacket);
        avcodec_send_packet(videoPlayer->codec, avPacket);
        avcodec_receive_frame(videoPlayer->codec, srcFrame);
        sws_scale(videoPlayer->swsContext, (const uint8_t *const *) srcFrame->data, srcFrame->linesize,
                  0, srcFrame->height, rgbFrame->data,
                  rgbFrame->linesize);

//        LOGE("frame 宽%d,高%d",srcFrame->width,srcFrame->height);
//        LOGE("rgb格式 宽%d,高%d",rgbFrame->width,rgbFrame->height);

        if((ts.pts=av_frame_get_best_effort_timestamp(srcFrame))==AV_NOPTS_VALUE){
            ts.pts=0;
        }
        ts.play = ts.pts*av_q2d(videoPlayer->time_base);
        //纠正时间
        ts.play = videoPlayer->synchronize(srcFrame, ts.play);
        ts.delay = ts.play - ts.last_play;
        if (ts.delay <= 0 || ts.delay > 1) {
            ts.delay = ts.last_delay;
        }
        ts.audio_clock = videoPlayer->audioPlayer->clock;
        ts.last_delay = ts.delay;
        ts.last_play = ts.play;
        //音频与视频的时间差
        ts.diff = videoPlayer->clock - ts.audio_clock;
        ts.sync_threshold = (ts.delay > 0.01 ? 0.01 : ts.delay);
        if (fabs(ts.diff) < 10) {
            if(ts.diff <= -ts.sync_threshold) {
                ts.delay = 0;
            }else if (ts.diff >= ts.sync_threshold) {
                ts.delay = 2 * ts.delay;
            }
        }
        ts.start_time += ts.delay;
        ts.actual_delay = ts.start_time - av_gettime() / 1000000.0;
        if (ts.actual_delay < 0.01) {
            ts.actual_delay = 0.01;
        }
        av_usleep(ts.actual_delay * 1000000.0 + 6000);
        video_call(rgbFrame);
    }

    av_packet_free(&avPacket);
    av_frame_free(&rgbFrame);
    av_frame_free(&srcFrame);
    sws_freeContext(videoPlayer->swsContext);
    size_t size = videoPlayer->queue.size();
    for (int i = 0; i < size; ++i) {
        AVPacket *pkt = videoPlayer->queue.front();
        av_free(pkt);
        videoPlayer->queue.erase(videoPlayer->queue.begin());
    }
    pthread_exit(0);
}

VideoPlayer::VideoPlayer() {
    pthread_mutex_init(&mutex, NULL);
    pthread_cond_init(&cond, NULL);
}

VideoPlayer::~VideoPlayer() {
    pthread_mutex_destroy(&mutex);
    pthread_cond_destroy(&cond);
}

void VideoPlayer::setAudioPlayer(AudioPlayer *audioPlayer) {
    this->audioPlayer = audioPlayer;
}

void VideoPlayer::setPlayCall(void (*call) (AVFrame *)) {
    video_call=call;
}


int VideoPlayer::put(AVPacket *avPacket) {
    AVPacket *avPacketRef = (AVPacket *) av_mallocz(sizeof(AVPacket));
    if (av_packet_ref(avPacketRef, avPacket))
        return 0;
    pthread_mutex_lock(&mutex);
    queue.push_back(avPacketRef);
    pthread_cond_signal(&cond);
    pthread_mutex_unlock(&mutex);
    return 1;
}

int VideoPlayer::get(AVPacket *avPacket) {
    pthread_mutex_lock(&mutex);
    while (isPlay) {
        //如果队列有数据，且不是暂停状态，则取出数据
        if (!queue.empty() && isPause) {
            //如果队列中有数据可以拿出来
            if(av_packet_ref(avPacket, queue.front())){
                break;
            }
            //取成功了，弹出队列，销毁packet
            AVPacket *packet2 = queue.front();
            queue.erase(queue.begin());
            av_free(packet2);
            break;
        }else {
            pthread_cond_wait(&cond, &mutex);
        }
    }
    pthread_mutex_unlock(&mutex);
    return 0;
}

void VideoPlayer::setAvCodecContext(AVCodecContext *avCodecContext) {
    codec = avCodecContext;
}

void VideoPlayer::play() {
    isPlay=1;
    isPause=1;
    pthread_create(&playId, NULL, videoPlay, this);//开启begin线程
}

void VideoPlayer::pause() {
    if(isPause==1){
        isPause=0;
    } else{
        isPause=1;
        pthread_cond_signal(&cond);
    }
}

void VideoPlayer::stop() {
    pthread_mutex_lock(&mutex);
    isPlay = 0;
    //因为可能卡在 deQueue
    pthread_cond_signal(&cond);
    pthread_mutex_unlock(&mutex);

    pthread_join(playId, 0);
    if (this->codec) {
        if (avcodec_is_open(this->codec))
            avcodec_close(this->codec);
        avcodec_free_context(&this->codec);
        this->codec = 0;
    }
}

double VideoPlayer::synchronize(AVFrame *frame, double play) {
    //clock是当前播放的时间位置
    if (play != 0)
        clock = play;
    else //pst为0 则先把pts设为上一帧时间
        play = clock;
    //可能有pts为0 则主动增加clock
    //frame->repeat_pict = 当解码时，这张图片需要要延迟多少
    //需要求出扩展延时：
    //extra_delay = repeat_pict / (2*fps) 显示这样图片需要延迟这么久来显示
    double repeat_pict = frame->repeat_pict;
    //使用AvCodecContext的而不是stream的
    double frame_delay = av_q2d(codec->time_base);
    //如果time_base是1,25 把1s分成25份，则fps为25
    //fps = 1/(1/25)
    double fps = 1 / frame_delay;
    //pts 加上 这个延迟 是显示时间
    double extra_delay = repeat_pict / (2 * fps);
    double delay = extra_delay + frame_delay;
//    LOGI("extra_delay:%f",extra_delay);
    clock += delay;
    return ts.play;
}