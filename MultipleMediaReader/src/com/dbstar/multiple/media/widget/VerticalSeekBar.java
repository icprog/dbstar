package com.dbstar.multiple.media.widget;

import android.content.Context;
import android.graphics.Canvas;
import android.util.AttributeSet;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.widget.SeekBar;

public class VerticalSeekBar extends SeekBar {

    public VerticalSeekBar(Context context) {
        super(context);
    }

    public VerticalSeekBar(Context context, AttributeSet attrs) {
        super(context, attrs);
    }

    public VerticalSeekBar(Context context, AttributeSet attrs, int defStyle) {
        super(context, attrs, defStyle);
    }

    protected void onSizeChanged(int w, int h, int oldw, int oldh) {
        super.onSizeChanged(h, w, oldh, oldw);
    }

    @Override
    protected synchronized void onMeasure(int widthMeasureSpec,
            int heightMeasureSpec) {
        super.onMeasure(widthMeasureSpec, heightMeasureSpec);
        setMeasuredDimension(getMeasuredHeight(), getMeasuredWidth());
    }

    protected void onDraw(Canvas c) {
        c.rotate(-90);
       c.translate(-getHeight(), 0);
        super.onDraw(c);
    }
    @Override
    public synchronized void setProgress(int progress) {
        // TODO Auto-generated method stub
        super.setProgress(progress);
        onSizeChanged(getWidth(), getHeight(), 0, 0);
    }
    @Override
    public boolean onTouchEvent(MotionEvent event) {
        if (!isEnabled()) {
            return false;
        }

        switch (event.getAction()) {
        case MotionEvent.ACTION_DOWN:
        case MotionEvent.ACTION_MOVE:
        case MotionEvent.ACTION_UP:
            int i = 0;
            i = getMax() - (int) (getMax() * event.getY() / getHeight());
            setProgress(i);
            break;
        case MotionEvent.ACTION_CANCEL:
            break;
        }
        return true;
    }
    private OnKeyListener mOnKeyListener;
    @Override
    public void setOnKeyListener(OnKeyListener l) {
        this.mOnKeyListener = l;
    }
    public boolean dispatchKeyEvent(KeyEvent event) {
        if(mOnKeyListener != null)
            return mOnKeyListener.onKey(this, event.getKeyCode(), event);
        return super.dispatchKeyEvent(event);
    }
}
