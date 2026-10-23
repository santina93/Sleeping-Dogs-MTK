package com.sdre.mali;

/** JNI bridge implemented by the accompanying local driver bootstrap library. */
public final class MaliBridge {
    static {
        System.loadLibrary("gpudrv");
    }

    private MaliBridge() { }

    public static native String configure(String nativeLibDir, String filesDir, int mode);
    public static native String probe(String nativeLibDir, String filesDir, int mode);
}
