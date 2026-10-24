package com.fulldivegames.sm64coopdxvr;

/** UI-thread-only ROM handshake. A rejected/timed-out attempt requires explicit retry. */
final class SteamFrameStartupState {
    private int generation;
    private boolean pending;
    private boolean blocked;
    private long activeMillis;

    boolean canSubmit() { return !pending && !blocked; }
    boolean isPending() { return pending; }
    boolean isPending(int attempt) { return pending && generation == attempt; }
    int begin() {
        if (!canSubmit()) throw new IllegalStateException("ROM setup already pending or blocked");
        pending = true;
        activeMillis = 0;
        return ++generation;
    }
    boolean complete(int attempt, boolean accepted) {
        if (!pending || attempt != generation) return false;
        pending = false;
        blocked = true; // Successful setup is terminal too; never resubmit on resume.
        return true;
    }
    boolean tick(long elapsedMillis, boolean resumed) {
        if (!pending) return false;
        if (resumed) activeMillis += Math.max(0, elapsedMillis);
        if (activeMillis < 20000) return false;
        pending = false;
        blocked = true;
        return true;
    }
    void retry() {
        if (pending) throw new IllegalStateException("Cannot retry a pending ROM");
        blocked = false;
    }
}
