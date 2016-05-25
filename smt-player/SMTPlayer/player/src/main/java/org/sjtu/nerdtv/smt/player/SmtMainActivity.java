package org.sjtu.nerdtv.smt.player;

import android.support.v7.app.AppCompatActivity;
import android.os.Bundle;

import org.sjtu.nerdtv.smt.player.jni.SmtNativeApi;

public class SmtMainActivity extends AppCompatActivity {

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_smt_main);
        SmtNativeApi.play();
    }
}
