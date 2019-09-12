//
// Created by steven on 2019-07-16.
//
#ifndef FFPLAYER_VIDEOPLAYER_H
#define FFPLAYER_VIDEOPLAYER_H

#include "queue"

extern "C" {
#include "include/libavcodec/avcodec.h"
#include "include/libavutil/rational.h"
#include "include/libswscale/swscale.h"
#include <pthread.h>
#include <libavutil/imgutils.h>
#include <libavutil/frame.h>
#include <libavutil/time.h>
#include "AudioPlayer.h"



class VideoPlayer {
public:
    VideoPlayer();

    ~VideoPlayer();

    void setAvCodecContext(AVCodecContext *avCodecContext);

    void play();//播放
    void stop();//暂停
    void pause();//pause
    double synchronize(AVFrame *frame, double play); //同步时间

    int put(AVPacket *avPacket);//压进队列
    int get(AVPacket *avPacket);//弹出队列

    void setAudioPlayer(AudioPlayer *audioPlayer);

    void setPlayCall(void (*call)(AVFrame* frame));

public:
    int index;//流索引
    int isPlay = -1;//是否正在播放
    int isPause = -1;//是否暂停
    pthread_t playId;//处理线程
    std::vector<AVPacket *> queue;//队列

    AVCodecContext *codec;//解码器上下文

    SwsContext *swsContext;
    //同步锁
    pthread_mutex_t mutex;
    //条件变量
    pthread_cond_t cond;

    AVRational time_base;
    double clock;

    AudioPlayer *audioPlayer;

};
};
#endif //FFPLAYER_VIDEOPLAYER_H