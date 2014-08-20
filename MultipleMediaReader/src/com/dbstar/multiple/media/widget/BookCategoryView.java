package com.dbstar.multiple.media.widget;

import java.util.List;

import android.content.Context;
import android.graphics.Rect;
import android.util.AttributeSet;
import android.util.Log;
import android.view.KeyEvent;
import android.view.View;
import android.widget.LinearLayout;
import android.widget.TextView;

import com.dbstar.multiple.media.data.BookCategory;

public class BookCategoryView extends LinearLayout{
    
    private List<BookCategory> mData;
    private int mSelectedIndex = 0;
    private OnItemSelectedListener mOnListener;
    public BookCategoryView(Context context, AttributeSet attrs) {
        super(context, attrs);
    }
    
    public void setSelection(int index ){
        mSelectedIndex = index;
    }
    
    public void setData(List<BookCategory> data){
        this.mData = data;
    }
    
    public List<BookCategory> getData(){
        return mData;
    }
    
    
    @Override
    public boolean onKeyUp(int keyCode, KeyEvent event) {
        if(mData == null || mData.isEmpty())
            return super.onKeyUp(keyCode, event);
        switch (keyCode) {
        case KeyEvent.KEYCODE_DPAD_DOWN:
            mSelectedIndex = ( ++mSelectedIndex + mData.size())% mData.size();
            notifyDataChanged();
            return true;

        case KeyEvent.KEYCODE_DPAD_UP:
            Log.i("Futao", "----" + mSelectedIndex);
            mSelectedIndex--;
            if(mSelectedIndex == -1){
                mSelectedIndex = mData.size() -1;
            }
            Log.i("Futao", "++++" + mSelectedIndex);
            notifyDataChanged();
            return true;
        }
        return super.onKeyUp(keyCode, event);
    }
    @Override
    public boolean onKeyDown(int keyCode, KeyEvent event) {
        switch (keyCode) {
        case KeyEvent.KEYCODE_DPAD_DOWN:
            return true;

        case KeyEvent.KEYCODE_DPAD_UP:
            return true;
        }
        return super.onKeyDown(keyCode, event);
    }
    public void notifyDataChanged(){
        TextView v ;
        if(mData == null || mData.isEmpty()){
            for(int i = 0,count = getChildCount();i< count ;i ++){
                v = (TextView) getChildAt(i);
                v.setText(null);
        }
            return;
        }
        int size = mData.size();
        for(int i = 0,count = getChildCount();i< count ;i ++){
            v = (TextView) getChildAt(i);
            if(size == 1){
                if(i != 3)
                    v.setVisibility(View.INVISIBLE);
                else
                    v.setVisibility(View.VISIBLE);
            }else{
                v.setVisibility(View.VISIBLE);
            }
            int index = (Math.abs(size-3) + mSelectedIndex + i) % size;
            v.setText(mData.get(index).Name);
            if( i == 3){
//                if(hasFocus()){
//                    v.setBackgroundResource(R.drawable.newspaper_category_hightlight_bg); 
//                   
//                   // BlurMaskFilter filter = new BlurMaskFilter(3, Blur.SOLID);
//                    //v.getPaint().setMaskFilter(filter) ;
//                }
//                else{
//                    v.setBackgroundDrawable(null);
//                }
                
                if(mOnListener != null)
                    mOnListener.onSelected(v, mData.get(index));
            }/*else{
                v.setBackgroundDrawable(null);
            }*/
        }
        
        
    }
    
       @Override
    protected void onFocusChanged(boolean gainFocus, int direction, Rect previouslyFocusedRect) {
        super.onFocusChanged(gainFocus, direction, previouslyFocusedRect);
//        if(mData == null || mData.isEmpty())
//            return;
//        TextView v = (TextView) getChildAt(3);
//        if(gainFocus){
//            v.setBackgroundResource(R.drawable.newspaper_category_hightlight_bg);
//            v.setFocusable(true);
//            //BlurMaskFilter filter = new BlurMaskFilter(3, Blur.SOLID);
//            //v.getPaint().setMaskFilter(filter) ;
//        }else{
//            v.setBackgroundDrawable(null);
//            v.setFocusable(false);
//            //v.getPaint().setMaskFilter(null) ;
//        }
        notifyDataChanged();
    }

    public void setOnItemSelectedListener(OnItemSelectedListener onItemSelectedListener) {
        this.mOnListener = onItemSelectedListener;
    }
    
    public interface OnItemSelectedListener{
        
        void onSelected(View v,BookCategory category);
    }
}
