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
 * Android host for the unit_test suite.
 *
 * Unpacks the APK's {@code assets/test} + {@code assets/examples} into internal
 * storage ({@code <files>/evengine_test}) and launches the zeroerr runner built
 * into {@code libmain.so}. SDL_main chdirs to that tree, so the compile-time
 * {@code EVENGINE_SOURCE_DIR="."} / {@code EVENGINE_TEST_BINARY_DIR="."} paths
 * resolve on-device. A test filter can be passed as the
 * {@code evengine.test.filter} intent extra, e.g.
 * {@code adb shell am start -n com.evengine.example/.EVTestActivity --es evengine.test.filter "math.*"}.
 */
public class EVTestActivity extends SDLActivity {
    private static final String TAG = "EVTestActivity";
    private String testDir;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        testDir = new File(getFilesDir(), "evengine_test").getAbsolutePath();
        try {
            // Wipe first so assets removed from the package do not linger across upgrades.
            deleteRecursive(new File(testDir));
            copyAssetDir("test", testDir);
            copyAssetDir("examples", testDir + "/examples");
            Log.i(TAG, "Test assets ready at " + testDir);
        } catch (IOException e) {
            Log.e(TAG, "Failed to unpack test assets", e);
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
        if (getIntent() != null) {
            String filter = getIntent().getStringExtra("evengine.test.filter");
            if (filter != null && !filter.isEmpty()) {
                return new String[] { "--testcase=" + filter };
            }
        }
        return new String[0];
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

    private static void deleteRecursive(File f) {
        if (f == null || !f.exists()) return;
        File[] children = f.listFiles();
        if (children != null) {
            for (File c : children) deleteRecursive(c);
        }
        //noinspection ResultOfMethodCallIgnored
        f.delete();
    }
}
