package com.dbstar.multiple.media.shelf.share;

import android.app.Activity;
import android.app.Service;
import android.content.ComponentName;
import android.content.Intent;
import android.content.ServiceConnection;
import android.os.Bundle;
import android.os.IBinder;
import android.util.Log;
import android.view.View;
import android.widget.LinearLayout;
import android.widget.TextView;

import com.dbstar.multiple.media.shelf.R;
import com.dbstar.multiple.media.shelf.share.ShareService.LoaclBinder;

public class MediaShareActivity extends Activity {

    private String mColumnBookId, mColumnNewsPaperPaperId;
    private LinearLayout mGuideView;
    private TextView mIpAddress;
    private TextView mError;
    private ShareService mService;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_media_share);
        
        mColumnBookId = getIntent().getStringExtra("mColumnBookId");
        
        mColumnNewsPaperPaperId = getIntent().getStringExtra("mColumnNewsPaperPaperId");
        
        Log.d("MediaShareActivity", "-----mColumnBookId----- = " + mColumnBookId);
        Log.d("MediaShareActivity", "-----mColumnNewsPaperPaperId----- = " + mColumnNewsPaperPaperId);
        
        mGuideView = (LinearLayout) findViewById(R.id.share_help_guide);
        mIpAddress = (TextView) findViewById(R.id.ipaddress);
        mError = (TextView) findViewById(R.id.no_network);
        
        Intent intent = new Intent(this, ShareService.class);
        bindService(intent,connection,  Service.BIND_AUTO_CREATE);
        
        }

    ServiceConnection connection = new ServiceConnection() {

        @Override
        public void onServiceDisconnected(ComponentName name) {
            mService = null;
        }

        @Override
        public void onServiceConnected(ComponentName name, IBinder service) {
            mService = ((LoaclBinder)service).getService();
            mService.setColumnId(mColumnBookId, mColumnNewsPaperPaperId);
            initData();
        }
    };
    
    private void initData(){
        String ip = mService.getIpAddress();
        Log.i("Futao", "initData = " + ip);
        if(ip == null){
            mError.setVisibility(View.VISIBLE);
            mGuideView.setVisibility(View.INVISIBLE);
        }else{
            mError.setVisibility(View.INVISIBLE);
           
            mGuideView.setVisibility(View.VISIBLE);
            mIpAddress.setText("http://" + ip + ":" + 8080);
        }
    }
    
    @Override
    protected void onDestroy() {
        super.onDestroy();
        unbindService(connection);
    }
}
