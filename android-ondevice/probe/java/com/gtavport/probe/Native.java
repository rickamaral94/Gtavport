package com.gtavport.probe;

final class Native {
    static { System.loadLibrary("odin2probe"); }
    /** {exit (ou -errno), wall_ms, maxrss_kb, user_ms, sys_ms, sinal} */
    static native long[] runTool(String[] argv, String cwd, String log, long cpuMask);
    static native String dlopenCall(String path, String sym, int arg);
    static native String strerror(int errno);
    private Native() {}
}
