package com.revolt.game;

import android.content.Context;
import android.content.res.AssetManager;
import android.util.Log;

import java.io.BufferedReader;
import java.io.File;
import java.io.FileOutputStream;
import java.io.FileReader;
import java.io.InputStream;
import java.io.OutputStream;
import java.util.ArrayList;
import java.util.List;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

/**
 * Game data management: unpacking assets/gamedata to filesDir/gamedata,
 * and parsing carinfo.txt / level .inf files for the launcher UI.
 */
public final class GameData {

    private static final String TAG = "revolt-java";
    static final String DATA_VERSION = "5";   // v5: frontend + wild_west1 levels removed (flag content); stale unpacks now wiped

    private GameData() {}

    public static File root(Context ctx) {
        return new File(ctx.getFilesDir(), "gamedata");
    }

    public static boolean isUnpacked(Context ctx) {
        return new File(root(ctx), ".unpacked-v" + DATA_VERSION).exists();
    }

    /** Copies assets/gamedata -> filesDir/gamedata on first run (or version bump). */
    public static void unpack(Context ctx) {
        File root = root(ctx);
        File marker = new File(root, ".unpacked-v" + DATA_VERSION);
        if (marker.exists()) {
            return;
        }
        // stale (older-version) unpack: wipe it, or files removed from the
        // assets — like deleted levels — would linger on updated installs
        if (root.exists()) {
            deleteRecursive(root);
        }
        Log.i(TAG, "unpacking game data to " + root);
        long start = System.currentTimeMillis();
        try {
            copyAssetDir(ctx.getAssets(), "gamedata", root);
            if (!marker.createNewFile()) {
                Log.w(TAG, "could not create unpack marker");
            }
            Log.i(TAG, "game data unpacked in " + (System.currentTimeMillis() - start) + " ms");
        } catch (Exception e) {
            Log.e(TAG, "game data unpack failed", e);
        }
    }

    private static void deleteRecursive(File f) {
        File[] children = f.listFiles();
        if (children != null) {
            for (File c : children) deleteRecursive(c);
        }
        if (!f.delete()) {
            Log.w(TAG, "could not delete " + f);
        }
    }

    private static void copyAssetDir(AssetManager am, String srcPath, File dst)
            throws Exception {
        String[] children = am.list(srcPath);
        if (children == null || children.length == 0) {
            File parent = dst.getParentFile();
            if (parent != null && !parent.exists() && !parent.mkdirs()) {
                throw new Exception("mkdirs failed: " + parent);
            }
            try (InputStream in = am.open(srcPath);
                 OutputStream out = new FileOutputStream(dst)) {
                byte[] buf = new byte[65536];
                int n;
                while ((n = in.read(buf)) > 0) {
                    out.write(buf, 0, n);
                }
            }
            return;
        }
        for (String child : children) {
            copyAssetDir(am, srcPath + "/" + child, new File(dst, child));
        }
    }

    // ------------------------------------------------------------ launcher data

    public static class Track {
        public final String dir;    // level directory name (e.g. "nhood1")
        public final String name;   // display name from the .inf (e.g. "Toys in the Hood")
        Track(String dir, String name) { this.dir = dir; this.name = name; }
        @Override public String toString() { return name; }
    }

    public static class Car {
        public final int id;        // CarID (index in carinfo.txt)
        public final String name;
        Car(int id, String name) { this.id = id; this.name = name; }
        @Override public String toString() { return name; }
    }

    private static final Pattern INF_NAME = Pattern.compile("^\\s*NAME\\s+'([^']*)'");
    private static final Pattern CAR_HEADER = Pattern.compile("^\\s*CAR\\s+(\\d+)\\s*\\{", Pattern.CASE_INSENSITIVE);
    private static final Pattern CAR_NAME = Pattern.compile("^\\s*Name\\s+\"([^\"]*)\"", Pattern.CASE_INSENSITIVE);

    /** Race tracks: every levels/<dir>/<dir>.inf, named from its NAME line. */
    public static List<Track> listTracks(Context ctx) {
        List<Track> tracks = new ArrayList<>();
        File levels = new File(root(ctx), "levels");
        File[] dirs = levels.listFiles(File::isDirectory);
        if (dirs == null) return tracks;
        for (File dir : dirs) {
            if (dir.getName().equalsIgnoreCase("frontend")) continue;   // 1999 menu scene
            File inf = new File(dir, dir.getName() + ".inf");
            if (!inf.isFile()) continue;
            String name = dir.getName();
            try (BufferedReader r = new BufferedReader(new FileReader(inf))) {
                String line;
                while ((line = r.readLine()) != null) {
                    Matcher m = INF_NAME.matcher(line);
                    if (m.find()) { name = m.group(1); break; }
                }
            } catch (Exception e) {
                Log.w(TAG, "can't read " + inf, e);
            }
            tracks.add(new Track(dir.getName(), name));
        }
        tracks.sort((a, b) -> a.name.compareToIgnoreCase(b.name));
        return tracks;
    }

    /** Playable cars from carinfo.txt: "CAR n {" blocks and their Name lines. */
    public static List<Car> listCars(Context ctx) {
        List<Car> cars = new ArrayList<>();
        File info = new File(root(ctx), "carinfo.txt");
        try (BufferedReader r = new BufferedReader(new FileReader(info))) {
            String line;
            int currentId = -1;
            while ((line = r.readLine()) != null) {
                int semi = line.indexOf(';');
                if (semi >= 0) line = line.substring(0, semi);
                Matcher h = CAR_HEADER.matcher(line);
                if (h.find()) {
                    currentId = Integer.parseInt(h.group(1));
                    continue;
                }
                if (currentId < 0) continue;   // skip the 0-28 defaults template
                Matcher n = CAR_NAME.matcher(line);
                if (n.find()) {
                    cars.add(new Car(currentId, n.group(1)));
                    currentId = -1;
                }
            }
        } catch (Exception e) {
            Log.e(TAG, "can't parse carinfo.txt", e);
        }
        return cars;
    }
}
