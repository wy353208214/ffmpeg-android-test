#include <jni.h>
#include <string>
#include <android/native_window.h>
#include <android/native_window_jni.h>
#include <android/log.h>
#include <stdio.h>
#include <unistd.h>
#include "VideoPlayer.h"
#include "AudioPlayer.h"

// Android 打印 Log
#define LOGD(FORMAT, ...) __android_log_print(ANDROID_LOG_ERROR, "player", FORMAT, ##__VA_ARGS__);

extern "C" {
#include "include/libavcodec/avcodec.h"
#include "include/libavformat/avformat.h"
#include "include/libavfilter/avfilter.h"
#include "include/libavutil/imgutils.h"
#include "include/libavutil/samplefmt.h"
#include "include/libswscale/swscale.h"
#include "include/libswresample/swresample.h"
#include "include/libavutil/frame.h"
}

const char *inputPath;
int64_t *totalTime;
VideoPlayer *videoPlayer;
AudioPlayer *audioPlayer;
pthread_t p_tid;
int isPlay;
ANativeWindow *window = 0;
int64_t duration;
AVFormatContext *pFormatCtx;
AVPacket *packet;
int step = 0;
jboolean isSeek = false;

void call_video_play(AVFrame *frame) {
    if (!window) {
        return;
    }
    ANativeWindow_Buffer window_buffer;
    if (ANativeWindow_lock(window, &window_buffer, 0)) {
        return;
    }

    LOGD("绘制 宽%d,高%d", frame->width, frame->height);
    LOGD("绘制 宽%d,高%d  行字节 %d ", window_buffer.width, window_buffer.height, frame->linesize[0]);
    uint8_t *dst = (uint8_t *) window_buffer.bits;
    int dstStride = window_buffer.stride * 4;
    uint8_t *src = frame->data[0];
    int srcStride = frame->linesize[0];
    for (int i = 0; i < window_buffer.height; ++i) {
        memcpy(dst + i * dstStride, src + i * srcStride, srcStride);
    }
    ANativeWindow_unlockAndPost(window);

}

void init() {
    LOGD("开启解码线程")
    //1.注册组件
    av_register_all();
    avformat_network_init();
    //封装格式上下文
    pFormatCtx = avformat_alloc_context();

    //2.打开输入视频文件
    if (avformat_open_input(&pFormatCtx, inputPath, NULL, NULL) != 0) {
        LOGD("%s", "打开输入视频文件失败");
    }
    //3.获取视频信息
    if (avformat_find_stream_info(pFormatCtx, NULL) < 0) {
        LOGD("%s", "获取视频信息失败");
    }

    //得到播放总时间
    if (pFormatCtx->duration != AV_NOPTS_VALUE) {
        duration = pFormatCtx->duration;//微秒
    }
}

void seekTo(int mesc) {
    if (mesc <= 0) {
        mesc = 0;
    }
    //清空vector
    audioPlayer->queue.clear();
    videoPlayer->queue.clear();
    av_seek_frame(pFormatCtx, videoPlayer->index, (int64_t) (mesc / av_q2d(videoPlayer->time_base)), AVSEEK_FLAG_BACKWARD);
    av_seek_frame(pFormatCtx, audioPlayer->index, (int64_t) (mesc / av_q2d(audioPlayer->time_base)), AVSEEK_FLAG_BACKWARD);
}

void *begin(void *args) {

    //找到视频流和音频流
    for (int i = 0; i < pFormatCtx->nb_streams; ++i) {
        //获取解码器
        AVCodecParameters *avCodecParameters = pFormatCtx->streams[i]->codecpar;
        //如果不是音频或视频流就继续查找
//        if (avCodecParameters->codec_type != AVMEDIA_TYPE_VIDEO)
//            continue;

        AVCodec *avCodec = avcodec_find_decoder(avCodecParameters->codec_id);
        //copy一个解码器，
        AVCodecContext *codecContext = avcodec_alloc_context3(avCodec);
        avcodec_parameters_to_context(codecContext, avCodecParameters);
        if (avcodec_open2(codecContext, avCodec, NULL) < 0) {
            LOGD("打开失败")
            continue;
        }

        //如果是视频流
        if (avCodecParameters->codec_type == AVMEDIA_TYPE_VIDEO) {
            videoPlayer->index = i;
            videoPlayer->setAvCodecContext(codecContext);
            videoPlayer->time_base = pFormatCtx->streams[i]->time_base;
            if (window) {
                ANativeWindow_setBuffersGeometry(window, videoPlayer->codec->width, videoPlayer->codec->height, WINDOW_FORMAT_RGBA_8888);
            }
        }//如果是音频流
        else if (avCodecParameters->codec_type == AVMEDIA_TYPE_AUDIO) {
            audioPlayer->index = i;
            audioPlayer->setAvCodecContext(codecContext);
            audioPlayer->time_base = pFormatCtx->streams[i]->time_base;
        }
    }
    //开启播放
    videoPlayer->setAudioPlayer(audioPlayer);
    audioPlayer->play();
    videoPlayer->play();
    isPlay = 1;
    //seekTo(0);
    //解码packet,并压入队列中
    packet = (AVPacket *) av_mallocz(sizeof(AVPacket));
    //跳转到某一个特定的帧上面播放
    int ret;
    while (isPlay) {
        //
        ret = av_read_frame(pFormatCtx, packet);
        if (ret == 0) {
            if (videoPlayer && videoPlayer->isPlay && packet->stream_index == videoPlayer->index) {
                //将视频packet压入队列
                videoPlayer->put(packet);
            } else if (audioPlayer && audioPlayer->isPlay && packet->stream_index == audioPlayer->index) {
                audioPlayer->put(packet);
            }
            av_packet_unref(packet);
        } else if (ret == AVERROR_EOF) {
            // 读完了
            //读取完毕 但是不一定播放完毕
            while (isPlay) {
                if (videoPlayer->queue.empty() && audioPlayer->queue.empty()) {
                    break;
                }
//                LOGD("等待播放完成");
                av_usleep(10000);
            }
        }
    }
    //解码完过后可能还没有播放完
    isPlay = 0;
    if (audioPlayer && audioPlayer->isPlay) {
        audioPlayer->stop();
    }
    if (videoPlayer && videoPlayer->isPlay) {
        videoPlayer->stop();
    }
    //释放
    av_free(packet);
    avformat_free_context(pFormatCtx);
    pthread_exit(0);
}

extern "C"
JNIEXPORT jstring JNICALL
Java_qpidnetwork_com_ffplayer_MainActivity_stringFromJNI(
        JNIEnv *env,
        jobject /* this */) {
    std::string hello = "Hello from C++";
    return env->NewStringUTF(hello.c_str());
}

extern "C" JNIEXPORT void JNICALL
Java_qpidnetwork_com_ffplayer_FFPlayer_play(JNIEnv *env, jobject object, jstring path) {
    if (path == NULL) {
        LOGD("Path is null");
        return;
    }
    inputPath = env->GetStringUTFChars(path, 0);
    init();
    videoPlayer = new VideoPlayer();
    audioPlayer = new AudioPlayer();
    videoPlayer->setPlayCall(call_video_play);
    pthread_create(&p_tid, NULL, begin, NULL);//开启begin线程
    env->ReleaseStringUTFChars(path, inputPath);
}


extern "C" JNIEXPORT void JNICALL
Java_qpidnetwork_com_ffplayer_FFPlayer_pause(JNIEnv *env, jobject object) {

}

extern "C" JNIEXPORT void JNICALL
Java_qpidnetwork_com_ffplayer_FFPlayer_setSurface(JNIEnv *env, jobject object, jobject surface) {
    //得到界面
    if (window) {
        ANativeWindow_release(window);
        window = 0;
    }
    window = ANativeWindow_fromSurface(env, surface);
    if (videoPlayer && videoPlayer->codec) {
        ANativeWindow_setBuffersGeometry(window, videoPlayer->codec->width, videoPlayer->codec->height, WINDOW_FORMAT_RGBA_8888);
    }
}

extern "C" JNIEXPORT void JNICALL
Java_qpidnetwork_com_ffplayer_FFPlayer_release(JNIEnv *env, jobject object) {
    //释放资源
    if (isPlay) {
        isPlay = 0;
        pthread_join(p_tid, 0);
    }
    if (videoPlayer) {
        if (videoPlayer->isPlay) {
            videoPlayer->stop();
        }
        delete (videoPlayer);
        videoPlayer = NULL;
    }
    if (audioPlayer) {
        if (audioPlayer->isPlay) {
            audioPlayer->stop();
        }
        delete (audioPlayer);
        audioPlayer = NULL;
    }
}

