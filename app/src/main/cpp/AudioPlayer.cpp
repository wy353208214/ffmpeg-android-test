//
// Created by steven on 2019-07-16.
//

#include "AudioPlayer.h"
#include <android/log.h>

#define LOGE(FORMAT, ...) __android_log_print(ANDROID_LOG_ERROR,"LC XXX",FORMAT,##__VA_ARGS__);

void *musicPlay(void *arg) {
    AudioPlayer *audioPlayer = (AudioPlayer *) arg;
    audioPlayer->CreatePlayer();
    pthread_exit(0);
}

int getPcm(AudioPlayer *audioPlayer) {
    AVPacket *avPacket = (AVPacket *) av_mallocz(sizeof(AVPacket));
    AVFrame *avFrame = av_frame_alloc();
    int size = 0;
    int gotFrame;
    while (audioPlayer->isPlay) {
        size = 0;
        audioPlayer->get(avPacket);
        if (avPacket->pts != AV_NOPTS_VALUE) {
            audioPlayer->clock = avPacket->pts * av_q2d(audioPlayer->time_base);
        }
//        LOGE("解码")
        avcodec_send_packet(audioPlayer->codec, avPacket);
        avcodec_receive_frame(audioPlayer->codec, avFrame);
        swr_convert(audioPlayer->swrContext, &audioPlayer->out_buffer, 44100 * 2,
                    (const uint8_t **) avFrame->data, avFrame->nb_samples);
        size = av_samples_get_buffer_size(NULL, audioPlayer->out_channer_nb,
                                          avFrame->nb_samples, AV_SAMPLE_FMT_S16, 1);
        if (size != 0)
            break;
    }
    av_free(avPacket);
    av_frame_free(&avFrame);
    return size;
}

//回调函数
void bqPlayerCallback(SLAndroidSimpleBufferQueueItf bq, void *context) {
    //得到pcm数据
    AudioPlayer *musicplay = (AudioPlayer *) context;
    int datasize = getPcm(musicplay);
//    LOGE("datasize is %d", datasize);
    if (datasize > 0) {
        //第一针所需要时间采样字节/采样率
        double time = datasize / (44100 * 2 * 2);
        musicplay->clock = time + musicplay->clock;
//        LOGE("当前一帧声音时间%f   播放时间%f", time, musicplay->clock);

        (*bq)->Enqueue(bq, musicplay->out_buffer, datasize);
//        LOGE("播放 %d ", musicplay->queue.size());
    }
}

int creatFFmpeg(AudioPlayer *audioPlayer) {
    audioPlayer->swrContext = swr_alloc();
    audioPlayer->out_buffer = (uint8_t *) av_mallocz(44100 * 2);

    uint64_t out_ch_layout = AV_CH_LAYOUT_STEREO;
    //输出采样位数  16位
    enum AVSampleFormat out_formart = AV_SAMPLE_FMT_S16;
    //输出的采样率必须与输入相同
    int out_sample_rate = audioPlayer->codec->sample_rate;
    swr_alloc_set_opts(audioPlayer->swrContext, out_ch_layout, out_formart, out_sample_rate,
                       audioPlayer->codec->channel_layout, audioPlayer->codec->sample_fmt,
                       out_sample_rate, 0,
                       NULL);

    swr_init(audioPlayer->swrContext);
    //获取通道数  2
    audioPlayer->out_channer_nb = av_get_channel_layout_nb_channels(AV_CH_LAYOUT_STEREO);
    LOGE("------>通道数%d  ", audioPlayer->out_channer_nb);
    return 0;
}

AudioPlayer::AudioPlayer() {
    clock = 0;
    //初始化锁
    pthread_mutex_init(&mutex, NULL);
    pthread_cond_init(&cond, NULL);
}

AudioPlayer::~AudioPlayer() {
    if (out_buffer) {
        free(out_buffer);
    }
    for (int i = 0; i < queue.size(); ++i) {
        AVPacket *pkt = queue.front();

        queue.erase(queue.begin());
        LOGE("销毁音频帧%d", queue.size());
        av_free(pkt);
    }
    pthread_cond_destroy(&cond);
    pthread_mutex_destroy(&mutex);
}

void AudioPlayer::setAvCodecContext(AVCodecContext *avCodecContext) {
    codec = avCodecContext;
    creatFFmpeg(this);
}

int AudioPlayer::put(AVPacket *avPacket) {
    AVPacket *avPacket1 = (AVPacket *) av_mallocz(sizeof(AVPacket));
    if (av_packet_ref(avPacket1, avPacket)) {
        return 0;
    }
    pthread_mutex_lock(&mutex);
    queue.push_back(avPacket1);
    pthread_cond_signal(&cond);
    pthread_mutex_unlock(&mutex);
    return 1;
}

int AudioPlayer::get(AVPacket *avPacket) {
//    LOGE("取出队列");
    pthread_mutex_lock(&mutex);
    while (isPlay) {
        if (!queue.empty() && isPause) {
//            LOGE("is Pause %d", isPause);
            AVPacket *avPacket1 = queue.front();
            if (av_packet_ref(avPacket, avPacket1)) {
                break;
            }
            //取出数据成功
            queue.erase(queue.begin());
            av_free(avPacket1);
            break;
        } else {
//            LOGE("音频执行wait");
//            LOGE("is Pause %d", isPause);
            pthread_cond_wait(&cond, &mutex);
        }
    }
    pthread_mutex_unlock(&mutex);
    return 0;
}

void AudioPlayer::play() {
    isPause = 1;
    isPlay = 1;
    pthread_create(&playId, NULL, musicPlay, this);//开启begin线程
}

void AudioPlayer::stop() {
    LOGE("声音暂停");
    //因为可能卡在 deQueue
    pthread_mutex_lock(&mutex);
    isPlay = 0;
    pthread_cond_signal(&cond);
    pthread_mutex_unlock(&mutex);
    pthread_join(playId, 0);
    if (bqPlayerPlay) {
        (*bqPlayerPlay)->SetPlayState(bqPlayerPlay, SL_PLAYSTATE_STOPPED);
        bqPlayerPlay = 0;
    }
    if (bqPlayerObject) {
        (*bqPlayerObject)->Destroy(bqPlayerObject);
        bqPlayerObject = 0;

        bqPlayerBufferQueue = 0;
        bqPlayerVolume = 0;
    }

    if (outputMixObject) {
        (*outputMixObject)->Destroy(outputMixObject);
        outputMixObject = 0;
    }

    if (engineObject) {
        (*engineObject)->Destroy(engineObject);
        engineObject = 0;
        engineEngine = 0;
    }
    if (swrContext)
        swr_free(&swrContext);
    if (this->codec) {
        if (avcodec_is_open(this->codec))
            avcodec_close(this->codec);
        avcodec_free_context(&this->codec);
        this->codec = 0;
    }
    LOGE("AUDIO clear");
}

void AudioPlayer::pause() {
    if (isPause == 1) {
        isPause = 0;
    } else {
        isPause = 1;
        pthread_cond_signal(&cond);
    }
}


int AudioPlayer::CreatePlayer() {
    LOGE("创建opnsl es播放器")
    //创建播放器
    SLresult result;
    // 创建引擎engineObject
    result = slCreateEngine(&engineObject, 0, NULL, 0, NULL, NULL);
    if (SL_RESULT_SUCCESS != result) {
        return 0;
    }
    // 实现引擎engineObject
    result = (*engineObject)->Realize(engineObject, SL_BOOLEAN_FALSE);
    if (SL_RESULT_SUCCESS != result) {
        return 0;
    }
    // 获取引擎接口engineEngine
    result = (*engineObject)->GetInterface(engineObject, SL_IID_ENGINE,
                                           &engineEngine);
    if (SL_RESULT_SUCCESS != result) {
        return 0;
    }
    // 创建混音器outputMixObject
    result = (*engineEngine)->CreateOutputMix(engineEngine, &outputMixObject, 0,
                                              0, 0);
    if (SL_RESULT_SUCCESS != result) {
        return 0;
    }
    // 实现混音器outputMixObject
    result = (*outputMixObject)->Realize(outputMixObject, SL_BOOLEAN_FALSE);
    if (SL_RESULT_SUCCESS != result) {
        return 0;
    }
    result = (*outputMixObject)->GetInterface(outputMixObject, SL_IID_ENVIRONMENTALREVERB,
                                              &outputMixEnvironmentalReverb);
    const SLEnvironmentalReverbSettings settings = SL_I3DL2_ENVIRONMENT_PRESET_DEFAULT;
    if (SL_RESULT_SUCCESS == result) {
        (*outputMixEnvironmentalReverb)->SetEnvironmentalReverbProperties(
                outputMixEnvironmentalReverb, &settings);
    }


    //======================
    SLDataLocator_AndroidSimpleBufferQueue android_queue = {SL_DATALOCATOR_ANDROIDSIMPLEBUFFERQUEUE,
                                                            2};
    SLDataFormat_PCM pcm = {SL_DATAFORMAT_PCM, 2, SL_SAMPLINGRATE_44_1, SL_PCMSAMPLEFORMAT_FIXED_16,
                            SL_PCMSAMPLEFORMAT_FIXED_16,
                            SL_SPEAKER_FRONT_LEFT | SL_SPEAKER_FRONT_RIGHT,
                            SL_BYTEORDER_LITTLEENDIAN};
//   新建一个数据源 将上述配置信息放到这个数据源中
    SLDataSource slDataSource = {&android_queue, &pcm};
//    设置混音器
    SLDataLocator_OutputMix outputMix = {SL_DATALOCATOR_OUTPUTMIX, outputMixObject};

    SLDataSink audioSnk = {&outputMix, NULL};
    const SLInterfaceID ids[3] = {SL_IID_BUFFERQUEUE, SL_IID_EFFECTSEND,
            /*SL_IID_MUTESOLO,*/ SL_IID_VOLUME};
    const SLboolean req[3] = {SL_BOOLEAN_TRUE, SL_BOOLEAN_TRUE,
            /*SL_BOOLEAN_TRUE,*/ SL_BOOLEAN_TRUE};
    //先讲这个
    (*engineEngine)->CreateAudioPlayer(engineEngine, &bqPlayerObject, &slDataSource,
                                       &audioSnk, 2,
                                       ids, req);
    //初始化播放器
    (*bqPlayerObject)->Realize(bqPlayerObject, SL_BOOLEAN_FALSE);

    //得到接口后调用  获取Player接口
    (*bqPlayerObject)->GetInterface(bqPlayerObject, SL_IID_PLAY, &bqPlayerPlay);

    //注册回调缓冲区 //获取缓冲队列接口
    (*bqPlayerObject)->GetInterface(bqPlayerObject, SL_IID_BUFFERQUEUE,
                                    &bqPlayerBufferQueue);
    //缓冲接口回调
    (*bqPlayerBufferQueue)->RegisterCallback(bqPlayerBufferQueue, bqPlayerCallback, this);
    //获取音量接口
    (*bqPlayerObject)->GetInterface(bqPlayerObject, SL_IID_VOLUME, &bqPlayerVolume);

    //获取播放状态接口
    (*bqPlayerPlay)->SetPlayState(bqPlayerPlay, SL_PLAYSTATE_PLAYING);

    bqPlayerCallback(bqPlayerBufferQueue, this);

    return 1;
}