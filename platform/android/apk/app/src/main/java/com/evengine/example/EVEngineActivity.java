package com.evengine.example;

import android.content.res.AssetManager;
import android.os.Bundle;
import android.util.Log;

import org.libsdl.app.SDLActivity;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;

/**
 * SDLActivity subclass that unpacks the embedded example game into internal
 * storage and launches the native CLI entry: eve run &lt;gameDir&gt;.
 */
public class EVEngineActivity extends SDLActivity {
    private static final String TAG = "EVEngineActivity";
    private String gameDir;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        gameDir = new File(getFilesDir(), "game").getAbsolutePath();
        try {
            copyAssetDir("game", gameDir);
            Log.i(TAG, "Game assets ready at " + gameDir);
        } catch (IOException e) {
            Log.e(TAG, "Failed to unpack game assets", e);
        }
        // Make path available to native platform helpers.
        try {
            Class<?> systemClass = Class.forName("java.lang.System");
            java.lang.reflect.Method setenv = null;
            // Prefer Android libc setenv via native later; for now pass via arguments.
        } catch (Throwable ignored) {
        }
        super.onCreate(savedInstanceState);
    }

    @Override
    protected String[] getLibraries() {
        return new String[] {
            "SDL2",
            "c++_shared",
            "main"
        };
    }

    @Override
    protected String[] getArguments() {
        if (gameDir == null) {
            gameDir = new File(getFilesDir(), "game").getAbsolutePath();
        }
        return new String[] {
            "run",
            gameDir
        };
    }

    private void copyAssetDir(String assetPath, String destPath) throws IOException {
        AssetManager am = getAssets();
        String[] children = am.list(assetPath);
        File dest = new File(destPath);
        if (!dest.exists() && !dest.mkdirs()) {
            throw new IOException("Cannot create " + destPath);
        }
        if (children == null || children.length == 0) {
            // Leaf file
            copyAssetFile(assetPath, destPath);
            return;
        }
        for (String child : children) {
            String childAsset = assetPath.isEmpty() ? child : assetPath + "/" + child;
            String childDest = destPath + "/" + child;
            String[] grand = am.list(childAsset);
            if (grand != null && grand.length > 0) {
                copyAssetDir(childAsset, childDest);
            } else {
                copyAssetFile(childAsset, childDest);
            }
        }
    }

    private void copyAssetFile(String assetPath, String destPath) throws IOException {
        File outFile = new File(destPath);
        File parent = outFile.getParentFile();
        if (parent != null && !parent.exists() && !parent.mkdirs()) {
            throw new IOException("Cannot create parent for " + destPath);
        }
        try (InputStream in = getAssets().open(assetPath);
             OutputStream out = new FileOutputStream(outFile)) {
            byte[] buf = new byte[16 * 1024];
            int n;
            while ((n = in.read(buf)) > 0) {
                out.write(buf, 0, n);
            }
        }
    }
}
