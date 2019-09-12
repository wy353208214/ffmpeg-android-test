package qpidnetwork.com.ffplayer;


import android.Manifest;
import android.content.Intent;
import android.graphics.PixelFormat;
import android.net.Uri;
import android.os.Bundle;
import android.text.TextUtils;
import android.util.Log;
import android.view.Surface;
import android.view.SurfaceHolder;
import android.view.SurfaceView;
import android.view.View;
import android.widget.Toast;

import androidx.annotation.NonNull;
import androidx.annotation.Nullable;
import androidx.appcompat.app.AppCompatActivity;

import permissions.dispatcher.NeedsPermission;
import permissions.dispatcher.OnPermissionDenied;
import permissions.dispatcher.RuntimePermissions;

@RuntimePermissions
public class MainActivity extends AppCompatActivity {

    private SurfaceHolder surfaceHolder;
    private FFPlayer ffPlayer = FFPlayer.getInstance();
    private String filePath;
    private boolean isSurfaced = false;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_main);
        surfaceHolder = ((SurfaceView) findViewById(R.id.my_surface)).getHolder();
        surfaceHolder.addCallback(new SurfaceHolder.Callback() {
            @Override
            public void surfaceCreated(SurfaceHolder holder) {
                if (TextUtils.isEmpty(filePath))
                    return;
                ffPlayer.setSurface(surfaceHolder.getSurface());
                isSurfaced = true;
//                holder.setFixedSize(1080, 1920);
                new Thread(new Runnable() {
                    @Override
                    public void run() {
                        ffPlayer.play(filePath);
                    }
                }).start();
            }

            @Override
            public void surfaceChanged(SurfaceHolder holder, int format, int width, int height) {
//                Log.e(MainActivity.class.getCanonicalName(), "width:" + width + "---height:" + height);
                ffPlayer.setSurface(surfaceHolder.getSurface());
                isSurfaced = true;
            }

            @Override
            public void surfaceDestroyed(SurfaceHolder holder) {
                isSurfaced = false;
            }
        });

        findViewById(R.id.select_button).setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                if (!isSurfaced) {
                    Toast.makeText(getApplicationContext(), "界面初始化中", Toast.LENGTH_SHORT).show();
                    return;
                }
                MainActivityPermissionsDispatcher.askExternalPermissionWithPermissionCheck(MainActivity.this);
            }
        });
    }

    private void openCameraActivity() {
        filePath = "";
        Intent intent = new Intent(Intent.ACTION_GET_CONTENT);
        intent.setType("video/*;image/*");//设置类型，我这里是任意类型，任意后缀的可以这样写。
        intent.addCategory(Intent.CATEGORY_OPENABLE);
        startActivityForResult(intent,1);
    }


    @NeedsPermission({Manifest.permission.WRITE_EXTERNAL_STORAGE, Manifest.permission.READ_EXTERNAL_STORAGE, Manifest.permission.MODIFY_AUDIO_SETTINGS, Manifest.permission.RECORD_AUDIO})
    public void askExternalPermission() {
        Toast.makeText(this, "Permission is granted", Toast.LENGTH_SHORT).show();
        openCameraActivity();
    }

    @OnPermissionDenied({Manifest.permission.WRITE_EXTERNAL_STORAGE, Manifest.permission.READ_EXTERNAL_STORAGE, Manifest.permission.MODIFY_AUDIO_SETTINGS, Manifest.permission.RECORD_AUDIO})
    public void OnDenied() {
        Toast.makeText(this, "Permission is denied", Toast.LENGTH_SHORT).show();
    }

    @Override
    public void onRequestPermissionsResult(int requestCode, @NonNull String[] permissions, @NonNull int[] grantResults) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults);
        MainActivityPermissionsDispatcher.onRequestPermissionsResult(this, requestCode, grantResults);
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, @Nullable Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (resultCode == RESULT_OK) {//是否选择，没选择就不会继续
            Uri uri = data.getData();//得到uri，后面就是将uri转化成file的过程。
            filePath = UriUtil.getInstance().getPath(this, uri);
//            filePath = "rtmp://172.25.32.229:1935/hls/test";
            Log.d(MainActivity.class.getCanonicalName(), filePath);
        }
    }

}
