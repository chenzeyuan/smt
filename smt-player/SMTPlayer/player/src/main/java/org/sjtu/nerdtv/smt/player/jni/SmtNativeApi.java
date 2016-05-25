package org.sjtu.nerdtv.smt.player.jni;

/**
 * Created by chenchuantao on 16-5-23.
 */
public class SmtNativeApi {

    static {
        System.loadLibrary("avutil");
        System.loadLibrary("swresample");
        System.loadLibrary("swscale");
        System.loadLibrary("avcodec");
        System.loadLibrary("avformat");
        System.loadLibrary("avfilter");
        System.loadLibrary("avdevice");
        System.loadLibrary("smt_player");
    }

    public static native void play();
}
