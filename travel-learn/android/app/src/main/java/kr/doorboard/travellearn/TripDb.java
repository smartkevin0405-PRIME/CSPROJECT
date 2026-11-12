// 생성형 AI(Claude)의 도움을 받아 작성함.
package kr.doorboard.travellearn;

import android.content.ContentValues;
import android.content.Context;
import android.database.Cursor;
import android.database.sqlite.SQLiteDatabase;
import android.database.sqlite.SQLiteOpenHelper;

import java.util.ArrayList;
import java.util.List;

import kr.doorboard.travellearn.core.Proto;
import kr.doorboard.travellearn.core.TripRecord;

/** 이동 기록 저장소(SQLite). 이동 ID가 기본 키라서 같은 출발 신호를 두 번 받아도 한 번만 기록된다. */
public final class TripDb extends SQLiteOpenHelper {
    private static TripDb inst;

    public static synchronized TripDb get(Context c) {
        if (inst == null) inst = new TripDb(c.getApplicationContext());
        return inst;
    }

    private TripDb(Context c) {
        super(c, "trips.db", null, 1);
    }

    @Override
    public void onCreate(SQLiteDatabase db) {
        db.execSQL("CREATE TABLE trips (trip INTEGER PRIMARY KEY, user INTEGER, stop_hint INTEGER,"
                + " stop INTEGER, wx INTEGER, tb INTEGER, status TEXT, dur_ms INTEGER, q INTEGER,"
                + " src INTEGER, epoch INTEGER, eta_s INTEGER, temp_c INTEGER, note TEXT,"
                + " synced INTEGER DEFAULT 0, created_ms INTEGER)");
    }

    @Override
    public void onUpgrade(SQLiteDatabase db, int oldV, int newV) { }

    private static ContentValues values(TripRecord r) {
        ContentValues v = new ContentValues();
        v.put("trip", r.trip);
        v.put("user", r.user);
        v.put("stop_hint", r.stopHint);
        v.put("stop", r.stop);
        v.put("wx", r.wx);
        v.put("tb", r.tb);
        v.put("status", r.status);
        v.put("dur_ms", r.durMs);
        v.put("q", r.q);
        v.put("src", r.src);
        v.put("epoch", r.epoch);
        v.put("eta_s", r.etaS);
        v.put("temp_c", r.tempC);
        v.put("note", r.note);
        v.put("synced", r.synced ? 1 : 0);
        return v;
    }

    private static TripRecord read(Cursor c) {
        TripRecord r = new TripRecord();
        r.trip = c.getLong(0);
        r.user = c.getInt(1);
        r.stopHint = c.getInt(2);
        r.stop = c.getInt(3);
        r.wx = c.getInt(4);
        r.tb = c.getInt(5);
        r.status = c.getString(6);
        r.durMs = c.getLong(7);
        r.q = c.getInt(8);
        r.src = c.getInt(9);
        r.epoch = c.getLong(10);
        r.etaS = c.getInt(11);
        r.tempC = c.getInt(12);
        r.note = c.getString(13);
        r.synced = c.getInt(14) != 0;
        return r;
    }

    private static final String COLS = "trip,user,stop_hint,stop,wx,tb,status,dur_ms,q,src,epoch,"
            + "eta_s,temp_c,note,synced";

    public synchronized boolean exists(long trip) {
        Cursor c = getReadableDatabase().rawQuery("SELECT 1 FROM trips WHERE trip=?",
                new String[] {String.valueOf(trip)});
        try {
            return c.moveToFirst();
        } finally {
            c.close();
        }
    }

    public synchronized TripRecord find(long trip) {
        Cursor c = getReadableDatabase().rawQuery("SELECT " + COLS + " FROM trips WHERE trip=?",
                new String[] {String.valueOf(trip)});
        try {
            return c.moveToFirst() ? read(c) : null;
        } finally {
            c.close();
        }
    }

    /** 새 기록 추가. 이미 있으면 false. */
    public synchronized boolean insert(TripRecord r) {
        ContentValues v = values(r);
        v.put("created_ms", System.currentTimeMillis());
        return getWritableDatabase().insertWithOnConflict("trips", null, v,
                SQLiteDatabase.CONFLICT_IGNORE) != -1;
    }

    public synchronized void update(TripRecord r) {
        getWritableDatabase().update("trips", values(r), "trip=?",
                new String[] {String.valueOf(r.trip)});
    }

    public synchronized void markSynced(long trip) {
        ContentValues v = new ContentValues();
        v.put("synced", 1);
        getWritableDatabase().update("trips", v, "trip=?", new String[] {String.valueOf(trip)});
    }

    private List<TripRecord> query(String where, String order, int limit) {
        List<TripRecord> out = new ArrayList<TripRecord>();
        Cursor c = getReadableDatabase().rawQuery("SELECT " + COLS + " FROM trips WHERE " + where
                + " ORDER BY " + order + " LIMIT " + limit, null);
        try {
            while (c.moveToNext()) out.add(read(c));
        } finally {
            c.close();
        }
        return out;
    }

    private static final String FINISHED = "status NOT IN ('" + Proto.ST_ACTIVE + "','"
            + Proto.ST_WAIT_USER + "')";

    /** 끝났지만 아직 현관판에 저장되지 않은 기록(이동 ID 오름차순). */
    public synchronized List<TripRecord> unsynced() {
        return query("synced=0 AND " + FINISHED, "trip ASC", 50);
    }

    public synchronized int countUnsynced() {
        Cursor c = getReadableDatabase().rawQuery(
                "SELECT COUNT(*) FROM trips WHERE synced=0 AND " + FINISHED, null);
        try {
            return c.moveToFirst() ? c.getInt(0) : 0;
        } finally {
            c.close();
        }
    }

    public synchronized List<TripRecord> recent(int n) {
        return query("1=1", "created_ms DESC", n);
    }

    /** 끝나지 않은 채 남은 기록(비정상 종료 흔적). */
    public synchronized List<TripRecord> unfinished() {
        return query("NOT (" + FINISHED + ")", "trip ASC", 20);
    }
}
