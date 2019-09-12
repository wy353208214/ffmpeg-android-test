package qpidnetwork.com.ffplayer;

import android.view.Surface;

public class FFPlayer {

    static {
        System.loadLibrary("avcodec");
        System.loadLibrary("avfilter");
        System.loadLibrary("avformat");
        System.loadLibrary("avutil");
        System.loadLibrary("swresample");
        System.loadLibrary("swscale");
        System.loadLibrary("native-lib");
    }

    public static FFPlayer getInstance() {
        return new FFPlayer();
    }
    public native void play(String path);

    public native void pause();

    public native void release();

    public native void setSurface(Surface surface);
}
