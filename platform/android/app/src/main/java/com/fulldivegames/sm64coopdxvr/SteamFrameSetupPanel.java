package com.fulldivegames.sm64coopdxvr;

import android.graphics.Bitmap;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.Typeface;

/** Software text is uploaded only when changed; OpenXR owns visibility/input. */
final class SteamFrameSetupPanel {
    interface Output { void publish(int[] pixels, int generation); }
    private final Output output;
    private String title = "Starting SM64 VR";
    private String[] names = new String[0];
    private Runnable[] actions = new Runnable[0];
    private int selected, generation;
    SteamFrameSetupPanel(Output output) { this.output = output; }
    void show(String title, String[] names, Runnable[] actions) {
        if (names.length != actions.length) throw new IllegalArgumentException("Menu actions");
        this.title = title == null ? "SM64 VR setup" : title; this.names = names; this.actions = actions;
        selected = 0;
        draw();
    }
    void input(int direction, boolean select, int expectedGeneration) {
        if (expectedGeneration != generation || names.length == 0) return;
        if (direction != 0) {
            selected = Math.max(0, Math.min(names.length - 1, selected + direction));
            draw();
        } else if (select) actions[selected].run();
    }
    private void draw() {
        Bitmap bitmap = Bitmap.createBitmap(1024, 768, Bitmap.Config.ARGB_8888);
        Canvas canvas = new Canvas(bitmap);
        canvas.drawColor(Color.rgb(12, 18, 30));
        Paint paint = new Paint(Paint.ANTI_ALIAS_FLAG);
        paint.setTypeface(Typeface.create(Typeface.SANS_SERIF, Typeface.BOLD));
        paint.setTextSize(30); paint.setColor(Color.WHITE);
        // Long directory names remain readable instead of overflowing the panel.
        String heading = title;
        while (paint.measureText(heading) > 940 && heading.length() > 3)
            heading = heading.substring(0, heading.length() - 2) + "…";
        canvas.drawText(heading, 40, 58, paint);
        int first = selected / 8 * 8;
        paint.setTypeface(Typeface.create(Typeface.SANS_SERIF, Typeface.NORMAL));
        paint.setTextSize(27);
        for (int i = first; i < names.length && i < first + 8; i++) {
            int y = 100 + (i - first) * 70;
            if (i == selected) {
                paint.setColor(Color.rgb(45, 78, 125));
                canvas.drawRect(24, y - 4, 1000, y + 53, paint);
            }
            paint.setColor(Color.WHITE);
            String text = names[i];
            while (paint.measureText(text) > 910 && text.length() > 3)
                text = text.substring(0, text.length() - 2) + "…";
            canvas.drawText((i == selected ? "> " : "  ") + text, 40, y + 34, paint);
        }
        paint.setTextSize(23); paint.setColor(Color.rgb(190, 210, 240));
        canvas.drawText("Thumbstick up/down • Trigger or A/X to select", 40, 725, paint);
        int[] pixels = new int[1024 * 768];
        bitmap.getPixels(pixels, 0, 1024, 0, 0, 1024, 768);
        bitmap.recycle();
        output.publish(pixels, ++generation);
    }
}
